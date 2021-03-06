//-*-C++-*-
/*!
  Compute ECG at given locations from the output generated by a solve on 
  the cardiac Model. This code has to be executed after output has been 
  generated. This is more a post processing routine.
*/
#ifndef _ComputeECG_h_
#define _ComputeECG_h_
#include "Mesh.h"
#include <unistd.h>

using namespace std;
using namespace voom;

struct GradVals {
  vector<Real> gradient;
};

struct Diffusion {
  vector< vector<Real> > diffusion;
};

struct GradVoltage {
  vector<Real> gradient;
  Real weight; // Integration weight
};


//! Compute gradOneByR
void computeGradOneByR(Mesh* myMesh, vector<vector<Real> >& points,
		       vector< GradVals >& grad) {
  //Loop over all entities
  int i,j,k, numElems = myMesh->getIDTableSize();
  const int *indexMap = myMesh->getIndexMap();
  const Mesh::Position& nodes = myMesh->getNodes();
  const Mesh::Position& ghNodes = myMesh->getGhostNodes();
  const int dim = myMesh->getDimension();
  const Mesh::QuadPoints& QuadVals = myMesh->getPoints();
  const Real Toler = 1E-6;

  for(i = 0; i < numElems; i++) {
    Mesh::IDList conn = myMesh->getIDList(i);
    Mesh::IDList indices(2, 0);
    // Ignoring Beam and Constraint Elements
    //    if ( (conn.size() == 2) || (conn.size() >= 9 )) continue;
    if ( conn.size() >= 9 ) continue;

    // Get the nodal coordinates now
    vector< vector<Real> > nds(conn.size(), vector<Real> (dim) );
    for(j = 0; j < conn.size(); j++) {
      int id = indexMap[ conn[j] - 1];
      if ( id == 0) continue; // Not a relevant node
      for( k = 0; k < dim; k++) {
	if ( id > 0 ) nds[j][k] = nodes[id - 1][k];
	else nds[j][k] = ghNodes[-id - 1][k];
      }// k loop
    }// j loop
    // Relevant index from quad point data
    myMesh->getIndices(i, indices);

    // Compute Location of each quad point
    for(k = indices[0]; k < indices[1]; k++) {
      Real position[dim];
      for(int m = 0; m < dim; m++) position[m] = 0.;
      for(int m = 0; m < conn.size(); m++)
	for(int n = 0; n < dim; n++) 
	  position[n] += QuadVals[k].shapeFunctions[m]*nds[m][n];

      vector<Real> Gradient(dim);
      // Loop over Leads
      for( j = 0; j < points.size(); j++) {
	Real dist = 0.;
	for(int m = 0; m < dim; m++)
	  dist += pow(position[m] - points[j][m],2);
	dist = sqrt(dist);
	// Gradient = -(x_i - x_p)/dist^3, etc
	if ( dist < Toler) {
	  cout << "** Point Overlaps with mesh: \n";
	  for(int m = 0; m < dim; m++) Gradient[m] = 0.;
	} else 
	  for(int m = 0; m < dim; m++) 
	    Gradient[m] = -(position[m] - points[j][m])/pow(dist, 3);
	
	struct GradVals gV;
	gV.gradient = Gradient;
	grad.push_back(gV);
      } // j loop - over number of leads
    }// k loop - Index values of QuadVals
  }// i loop
}

void computeDiffusionTensor(Mesh* myMesh, Real Diff[], Real Dp[],
			    vector<Diffusion>& DiffMat) {
  const int numElems = myMesh->getIDTableSize();
  const int dim = myMesh->getDimension();
  const vector< vector<Real> >&diffusion = myMesh->getDiffusionVectors();
  const int isIsotropic = myMesh->isIsotropic();
  Real D[3];
  Mesh::IDList indices(2, 0);

  for(int i = 0; i < numElems; i++) {
    myMesh->getIndices(i, indices);
    const int numCon = myMesh->getIDList(i).size();
    // Ignoring Beam and Constraint Elements
    //    if ( (numCon == 2) || (numCon >= 9 )) continue;
    if (numCon >= 9) continue;

    if (isIsotropic) {
      D[0] = D[1] = D[2] = Diff[0];
    } else {
      D[0] = Diff[0]; D[1] = Diff[1]; D[2] = Diff[2];
    }
    for(int j = indices[0]; j < indices[1]; j++) {
      vector< vector<Real> > diffusion_matrix(dim, vector<Real> (dim));
      for(int m = 0; m < dim; m++)
	for(int n = 0; n < dim; n++) diffusion_matrix[m][n] = 0.;
      if (isIsotropic) {
	for(int p = 0; p < dim; p++) diffusion_matrix[p][p] = D[p];
      }
      else {
	Real f[dim];
	for(int p = 0; p < dim; p++){
	  for(int l = 0; l < dim; l++)
	    f[l] = diffusion[i][p*dim + l];
	  for(int m = 0; m < dim; m++)
	    for(int n = 0; n < dim; n++)
	      diffusion_matrix[m][n] += D[p]*f[m]*f[n];
	} // p loop
      } // else loop 
      struct Diffusion myDiffusion;
      myDiffusion.diffusion = diffusion_matrix;
      DiffMat.push_back(myDiffusion);

    }// j loop
  }// i loop
}

void computeVoltageAtQuadPoints(Mesh* myMesh, Real result[], 
				vector< GradVoltage >& voltage) {
  int i,j,k, numElems = myMesh->getIDTableSize();
  const int dim = myMesh->getDimension();
  const Mesh::QuadPoints& QuadVals = myMesh->getPoints();
  Mesh::IDList indices(2, 0);

  for(i = 0; i < numElems; i++) {
    Mesh::IDList conn = myMesh->getIDList(i);
    // Ignoring Beam and Constraint Elements
    //    if ( (conn.size() == 2) || (conn.size() >= 9 )) continue;
    if (conn.size() >= 9) continue;

    // Get the nodal coordinates now
    Real volt[conn.size()];
    for(j = 0; j < conn.size(); j++) volt[j] = result[ conn[j] - 1];

    // Relevant index from quad point data
    myMesh->getIndices(i, indices);
    // Compute gradient of V at each quad point
    for(k = indices[0]; k < indices[1]; k++) {
      struct GradVoltage gv;
      for(int n = 0; n < dim; n++) {
	Real value = 0.;
	for(int m = 0; m < conn.size(); m++) 
	  value += QuadVals[k].shapeDerivatives[m][n]*volt[m];	
	gv.gradient.push_back(value);
      } // n loop
      gv.weight = QuadVals[k].weight;
      voltage.push_back(gv);
    } // k loop
  } // i loop
}

Real* computeECG(vector< Diffusion>& diffusion_matrix, 
		 vector< GradVoltage>& voltage, 
		 vector< GradVals>& gradOneByR, const int dim ) {
  const int numEntity = voltage.size();
  const int numPoints = gradOneByR.size()/numEntity;
  Real *ECG = new Real[numPoints];
  
  for(int np = 0; np < numPoints; np++) {
    ECG[np ] =0.;
    for(int i = 0; i < voltage.size(); i++)
      for(int a = 0; a < dim; a++)
	for(int b = 0; b < dim; b++)
	  ECG[np] += diffusion_matrix[i].diffusion[a][b] * 
	    voltage[i].gradient[a] * gradOneByR[np + i*numPoints].gradient[b] 
	    * voltage[i].weight;
  }  
  return ECG;
}

#endif

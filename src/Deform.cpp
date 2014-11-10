#include "Deform.h"
using namespace Eigen;

Deform::Deform(double *P_data, int P_Num, vector<vector<int>> &adj_list, vector<Vector3i> &face_list) : adj_list(adj_list), max_iter(2000), min_delta(1e-3), lamd_deform(5)
{
	P.resize(3, P_Num);
	for (int i = 0; i != P_Num; ++i) {P.col(i) << P_data[3*i], P_data[3*i+1], P_data[3*i+2];}
	P_Prime = P;
	R = vector<Matrix3f>(P_Num, Matrix3f::Identity());

	vector<Triplet<float>> weight_list;
	weight_list.reserve(7*P_Num); // each vertex may have about 7 vertices connected
	vector<Triplet<float>> weight_sum;
	weight_sum.reserve(P_Num);
	for (decltype(adj_list.size()) i = 0; i != adj_list.size(); ++i) {
		float wi = 0;
		for (decltype(adj_list[i].size()) j = 0; j != adj_list[i].size(); ++j) {
			int id_j = adj_list[i][j];
			//if (i < id_j) {
			vector<int> share_Vertex;
			
			find_share_Vertex(i, id_j, adj_list, face_list, share_Vertex);			
			float wij = 0;
			if (share_Vertex.size()==2) wij = compute_wij(&P_data[3*i], &P_data[3*id_j], &P_data[3*share_Vertex[0]], &P_data[3*share_Vertex[1]]);
			else wij = compute_wij(&P_data[3*i], &P_data[3*id_j], &P_data[3*share_Vertex[0]]);

			//set_intersection(adj_list[i].begin(), adj_list[i].end(), adj_list[id_j].begin(), adj_list[id_j].end(), back_inserter(share_Vertex));
   //         float wij = 0;
   //         if (share_Vertex.size()==2) wij = compute_wij(&P_data[3*i], &P_data[3*id_j], &P_data[3*share_Vertex[0]], &P_data[3*share_Vertex[1]]);
   //         else if (share_Vertex.size()==1) wij = compute_wij(&P_data[3*i], &P_data[3*id_j], &P_data[3*share_Vertex[0]]);
   //         else cout << "Error: shared vertices can only be 1 or 2." << endl;

			wi += wij;
			weight_list.push_back(Triplet<float>(i, id_j, wij));

				//weight_list.push_back(Triplet<float>(id_j, i, wij));
			//}
		}
		if (wi < 0.1) cout << "Edge Weight Sum Warning: " << wi << endl;
		weight_sum.push_back(Triplet<float>(i, i, wi));
		//cout << R[i] << endl;
	}
	SparseMatrix<float> Weight_sum(P_Num, P_Num);
	Weight_sum.setFromTriplets(weight_sum.begin(), weight_sum.end());
	Weight.resize(P_Num, P_Num);
	Weight.setFromTriplets(weight_list.begin(), weight_list.end());
	L =  Weight_sum - Weight;
}

float *Deform::do_Deform(vector<double> &T, vector<int> &idx_T)
{
	int iter = 0;
	double delta = 0;
	do {
		update_Ri();
		++iter;
		delta = update_P_Prime(T, idx_T);
		cout << "iter: " << iter << "\tdelta: " << delta << endl;
	}while(delta > min_delta && iter <= max_iter);
	return P_Prime.data();
}

float *Deform::get_P_Prime()
{
	return P_Prime.data();
}

float* Deform::do_Deform_Iter(vector<double> &T, vector<int> &idx_T, double &delta)
{
	update_Ri();
	delta = update_P_Prime(T, idx_T);
	return P_Prime.data();
}

void Deform::update_Ri()
{
	Matrix3f Si;
	MatrixXf Di;
	Matrix3Xf Pi_Prime;
	Matrix3Xf Pi;
	for (decltype(adj_list.size()) i = 0; i != adj_list.size(); ++i) {
		Di = MatrixXf::Zero(adj_list[i].size(), adj_list[i].size());
		Pi_Prime.resize(3, adj_list[i].size());
		Pi.resize(3, adj_list[i].size());
		// if there is not any single unconnected point this for loop can have a more efficient representation
		for (decltype(adj_list[i].size()) j = 0; j != adj_list[i].size(); ++j) {
			Di(j, j) = Weight.coeffRef(i, adj_list[i][j]);
			Pi.col(j) = P.col(i) - P.col(adj_list[i][j]);
			Pi_Prime.col(j) = P_Prime.col(i) - P_Prime.col(adj_list[i][j]);
		}
		Si = Pi * Di * Pi_Prime.transpose();
		Matrix3f Ui;
		Vector3f Wi;
		Matrix3f Vi;
		wunderSVD3x3(Si, Ui, Wi, Vi);
		R[i] = Vi * Ui.transpose();
	}
}

double Deform::update_P_Prime(vector<double> &T, vector<int> &idx_T)
{
	MatrixX3f d = MatrixX3f::Zero(adj_list.size(), 3);
	for (decltype(adj_list.size()) i = 0; i != adj_list.size(); ++i) {
		// if there is not any single unconnected point this for loop can have a more efficient representation
		for (decltype(adj_list[i].size()) j = 0; j != adj_list[i].size(); ++j) {
			d.row(i) += ((Weight.coeffRef(i, adj_list[i][j])/2)*(R[i]+R[adj_list[i][j]])*(P.col(i) - P.col(adj_list[i][j]))).transpose();
		}
	}
	SparseMatrix<float> L_cur = L;
	for (decltype(idx_T.size()) i = 0; i != idx_T.size(); ++i) {
		d.row(idx_T[i]) += lamd_deform*RowVector3f(T[3*i], T[3*i+1], T[3*i+2])/2;
		L_cur.coeffRef(idx_T[i], idx_T[i]) += lamd_deform/2;
	}
	SimplicialCholesky<SparseMatrix<float>> chol(L_cur);
	Matrix3Xf P_Prime_last = P_Prime;
	P_Prime = chol.solve(d).transpose();
	return (P_Prime - P_Prime_last).norm();
}

float Deform::compute_wij(double *p1, double *p2, double *p3, double *p4)
{
	double e1 = sqrt((p1[0]-p2[0])*(p1[0]-p2[0])+(p1[1]-p2[1])*(p1[1]-p2[1])+(p1[2]-p2[2])*(p1[2]-p2[2]));
	double e2 = sqrt((p1[0]-p3[0])*(p1[0]-p3[0])+(p1[1]-p3[1])*(p1[1]-p3[1])+(p1[2]-p3[2])*(p1[2]-p3[2]));
	double e3 = sqrt((p3[0]-p2[0])*(p3[0]-p2[0])+(p3[1]-p2[1])*(p3[1]-p2[1])+(p3[2]-p2[2])*(p3[2]-p2[2]));
	double alpha_cos = (e3*e3+e2*e2-e1*e1)/(2*e3*e2);
	double beta_cos = 0;
	if (p4 != nullptr) {
		double e4 = sqrt((p1[0]-p4[0])*(p1[0]-p4[0])+(p1[1]-p4[1])*(p1[1]-p4[1])+(p1[2]-p4[2])*(p1[2]-p4[2]));
		double e5 = sqrt((p4[0]-p2[0])*(p4[0]-p2[0])+(p4[1]-p2[1])*(p4[1]-p2[1])+(p4[2]-p2[2])*(p4[2]-p2[2]));
		beta_cos = (e4*e4+e5*e5-e1*e1)/(2*e4*e5);
	}
	return ((alpha_cos/sqrt(1-alpha_cos*alpha_cos))+(beta_cos/sqrt(1-beta_cos*beta_cos)))/2;
}

void Deform::find_share_Vertex(int pi, int pj, vector<vector<int>> &adj_list, vector<Vector3i> &face_list, vector<int> &share_Vertex)
{
	vector<int> vertices;
	set_intersection(adj_list[pi].begin(), adj_list[pi].end(), adj_list[pj].begin(), adj_list[pj].end(), back_inserter(vertices));
	for (auto &i : vertices) {
		vector<int> f;
		f.push_back(pi);
		f.push_back(pj);
		f.push_back(i);
		sort(f.begin(), f.end());
		vector<Vector3i>::iterator it = find(face_list.begin(), face_list.end(), Map<Vector3i>(&f[0]));
		if (it != face_list.end()) {
			if ((*it)(0) != pi && (*it)(0) != pj) share_Vertex.push_back((*it)(0));
			else if ((*it)(1) != pi && (*it)(1) != pj) share_Vertex.push_back((*it)(1));
			else share_Vertex.push_back((*it)(2));
		}
	}
	if (share_Vertex.size() > 2) {
		cout << "share vertices number warning: " << share_Vertex.size() << endl;
	}
}
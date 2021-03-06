#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "clcg4.h"

int rank;
int comm_size;
int num_clusters;
int x_bound;
int y_bound;
long long data_points;

int** data; //has 2 rows, used to map coordinates
	    //index: 0  1  2  3  4  5  6 ...
	    //x:    [a][b][c][d][e][f][g]...
	    //y:    [a][b][c][d][e][f][g]...
long long data_size;

float** centers; // like data**
			   // index: 0  1  2  3  ... num_clusters
			   // x:    [a][b][c][d] ...
			   // y:	[a][b][c][d] ...
			   
int** new_centers; //used when centers are changed
				   // index:         0  1  2  3  ... num_clusters
				   // sum x values: [a][b][c][d] ...
				   // sum y values: [a][b][c][d] ...
				   // total values: [a][b][c][d] ...

			   
			   
int* membership; //has indexing equal to that of data**
				 //each value corresponds to an index in centers

void allocate_data(long long points_per_rank);

void print_data();

void print_centers();

void k_cluster();

int find_nearest_cluster(long long index);

float point_distance(int* point, float* center);

int main(int argc, char** argv){

	int dummy;	

	MPI_Init_thread( &argc, &argv, MPI_THREAD_MULTIPLE, &dummy);	
	MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	InitDefault();

	num_clusters = atoi(argv[1]);
	data_points = atoll(argv[2]);
	x_bound = atoi(argv[3]);
	y_bound = atoi(argv[4]);

	printf("rank :%d, %d reporting for duty! (num_clusters: %d, data_points: %lld) \n", rank, argc, num_clusters, data_points);

	//sets up and generates the data points inside data
	allocate_data(data_points/(long long)comm_size);

	//print_data();

	//start computing k cluster
	
	k_cluster();

	MPI_Barrier(MPI_COMM_WORLD);
	if(rank == 0){
		print_centers();
	}
	MPI_Finalize();

	return 0;
}

void print_centers(){
	printf("There are %d clusters\n", num_clusters);
	int i;
	for(i = 0; i < num_clusters; i++){
		printf(" %d - (%f, %f)\n", i, centers[i][0], centers[i][1]);
	}
}

void k_cluster(){
	float delta = 0.0, delta_tot=0.0;
	float threshold = 0.001;
	float distance;
	float distance_min = -1.0;
	long long cluster_size[num_clusters];
	int x_sum[num_clusters], y_sum[num_clusters];
	
	int i=0, j=0;
	int shortest = -1;
	
	int iters = 0;	
	while (delta_tot/data_points > threshold || iters == 0 ){
		iters++;
		delta = 0.0;
		for (i = 0; i<data_size; i++){
			for (j=0; j<num_clusters; j++){
				distance = point_distance(data[i], centers[j]);
				if (distance < distance_min || distance_min == -1.0){
					distance_min = distance;
					shortest = j;
				}
			}
			if (membership[i] != shortest){
				delta = delta +1;
				membership[i] = shortest;
			}
			new_centers[shortest][0] += data[i][0];
			new_centers[shortest][1] += data[i][1];
			new_centers[shortest][2] += 1;			
		}
		for(j=0; j<num_clusters; j++){
			MPI_Allreduce(&new_centers[j][2], &cluster_size[j], 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
			MPI_Allreduce(&new_centers[j][0], &x_sum[j], 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
			MPI_Allreduce(&new_centers[j][1], &y_sum[j], 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
		}
		
		for (j=0; j<num_clusters; j++){
			centers[j][0] = 1.0*x_sum[j]/cluster_size[j];
			centers[j][1] = 1.0*y_sum[j]/cluster_size[j];
			new_centers[j][0] = 0;
			new_centers[j][1] = 0;
			new_centers[j][2] = 0;
		}
		MPI_Allreduce(&delta, &delta_tot, 1, MPI_FLOAT, MPI_SUM, MPI_COMM_WORLD);
		
	}
	printf(" (%d) number of iterations = %d\n", rank, iters);
	
}






void print_data(){
	int i;
	for(i = 0; i < data_size; i++){
		if(i != 0 && i % 16 == 0) printf("\n");
			printf("(%d,%d) ", data[i][0], data[i][1]);
	}
	printf("\n");
}


void allocate_data(long long points_per_rank){

	data_size = points_per_rank;

	data = malloc(data_size * sizeof(int*));

	//input the random numbers for x and y coordinates
	int i;
	for(i = 0; i < data_size; i++){
		data[i] = malloc(2 * sizeof(int));
		float x = GenVal(rank);
		float y = GenVal(rank);
		data[i][0] = x * x_bound * (rank + 1);
		data[i][1] = y * y_bound * (rank + 1);
	}	
	
	centers = malloc(num_clusters * sizeof(float*));
	new_centers = malloc(num_clusters * sizeof(int*));
	//start cluster centers at first num_clusters points
	for (i=0; i < num_clusters; i++){
		centers[i] = malloc(2 * sizeof(float));
		new_centers[i] = calloc(3, sizeof(int));
		centers[i][0] = data[i][0];
		centers[i][1] = data[i][1];
		MPI_Bcast(&centers[i][0], 1, MPI_FLOAT, 0, MPI_COMM_WORLD);
		MPI_Bcast(&centers[i][1], 1, MPI_FLOAT, 0, MPI_COMM_WORLD);
	}
	
	membership = malloc(data_size * sizeof(int));
	for (i=0; i<data_size; i++){
		membership[i] = find_nearest_cluster(i);
	}

	if(rank == 1){
	//	print_data();
	}
}





//finds nearest cluster to given point, index in data**
int find_nearest_cluster(long long index){
	
	int   cluster_id, i;
    float dist, min_dist;

    /* find the cluster id that has min distance to object */
    cluster_id    = 0;
    min_dist = point_distance(data[index], centers[0]);

    for (i=1; i<num_clusters; i++) {
        dist = point_distance(data[index], centers[i]);
        if (dist < min_dist) { /* find the min and its array index */
            min_dist = dist;
            cluster_id = i;
        }
    }
    return(cluster_id);
	
}

//finds distance between point and center
float point_distance(int* point, float* center){
	float result = 0;
	
	result += (point[0] - center[0]) * (point[0] - center[0]);
	result += (point[1] - center[1]) * (point[1] - center[1]);
	
	return result;
}

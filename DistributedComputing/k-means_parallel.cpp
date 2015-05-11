#include <iostream>
#include <time.h>
#include <mpi.h>
#include<omp.h>

using std::cout;
using std::endl;

/**
 * Parallel one-dimensional k-means hybrid implementation.
 * -------------------------------------------------------
 * Author: Hazem Hamdy AbuMostafa,Hossam Khalil
*/

//Declarations:
float randomF(float);
int random(int, int, int);

//Main:
void main(int argc, char **argv)
{
	/*
	* DEFINITIONS
	*/
	const int MASTER = 0,
			  MAX_ITER = 20, //k-means iterations.
			  TAG_VALUES = 0, TAG_MEANS = 1, TAG_COUNTS = 2;
	
	double *values, *means; //The stars.

	int rank, size;
	double total_time;
	int source, dest;
	MPI_Status statusReceive;

	int totalElements, totalClusters, elementsPerProcess;


	/*
	* START
	*/
	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	omp_set_num_threads(size);
	/*** TESTING BLOCK ***
	totalElements = 10;
	totalClusters = 3;
	*/

	//Getting arguments (if exist):
	if(argc > 2)
		totalElements = atoi(argv[1]),
		totalClusters = atoi(argv[2]);
	else //Set defaults:
		totalElements = 1000000,
		totalClusters = 5;
	elementsPerProcess = totalElements / (size - 1);


	if(rank == MASTER)
	{
		/* INITIALIZATIONS */
		double *summed_means;
		int *summed_counts;

		//** TESTING BLOCK ***
		values = new double[totalElements] { 0.0932, 3.9414, 0.2997, 4.6878, 3.8272,
											 4.6998, 1.8072, 1.5256, 2.3518, 4.4015 };
		means = new double[totalClusters]  { values[1], values[0], values[2] };
		//

		//Initializing:
		srand(time(NULL));

		values = new double[totalElements];
		means = new double[totalClusters];
		int i;
		#pragma omp parallel private(i)
		{
			#pragma omp for private(i) schedule(dynamic,elementsPerProcess) 
				for (i = 0; i < totalElements; i++)
					values[i] = randomF(10);
			#pragma omp for private(i) schedule(dynamic,elementsPerProcess) 
				for (i = 0; i < totalClusters; i++)
					means[i] = values[random(i, 0, totalElements)];
		}
		/* REAL WORK */
		total_time = MPI_Wtime();

		double *partial_means;
		int *partial_counts;
		for(int iteration = 0; iteration < MAX_ITER; iteration++)
		{
			partial_means = new double[totalClusters];
			partial_counts = new int[totalClusters];

			summed_means  = new double[totalClusters] {/*(zeros)*/};
			summed_counts = new int[totalClusters]	  {/*(zeros)*/};


			//Sending loop:
			int startIndex;
			int i;
			#pragma omp for private(i) schedule(dynamic,elementsPerProcess)
			for( i=1; i < size; i++)
			{
				startIndex = (i-1) * elementsPerProcess;
				MPI_Send( &(values[startIndex]), elementsPerProcess, MPI_DOUBLE, i, TAG_VALUES, MPI_COMM_WORLD );
				MPI_Send(means, totalClusters, MPI_DOUBLE, i, TAG_MEANS, MPI_COMM_WORLD);
			}

			//Receiving and summing loop:
			#pragma omp for private(i) schedule(dynamic,elementsPerProcess)
			for(i=1; i < size; i++)
			{
				MPI_Recv(partial_means, totalClusters, MPI_DOUBLE, MPI_ANY_SOURCE, TAG_MEANS, MPI_COMM_WORLD, &statusReceive);
				MPI_Recv(partial_counts, totalClusters, MPI_INT, MPI_ANY_SOURCE, TAG_COUNTS, MPI_COMM_WORLD, &statusReceive);

				for(size_t i=0; i < totalClusters; i++)
					summed_means[i] += partial_means[i],
					summed_counts[i] += partial_counts[i];
			}

			//Iteration done. Calculating final values & displaying output:
			cout << "#" << iteration << ": ";

			int j;
			#pragma omp for private(j) schedule(dynamic,elementsPerProcess)
			for(j=0; j < totalClusters; j++)
#pragma omp critical
				means[j] = summed_means[j] / summed_counts[j],
				cout << means[j] << ", ";
			cout << "\n";
		}

		//All done. Displaying total time:
		#pragma omp barrier
		printf("\nTOTAL TIME: %f seconds.\n", MPI_Wtime() - total_time);
	}

	else
	{
		double *new_means;
		int *elem_per_cluster;
		for(int iteration = 0; iteration < MAX_ITER; iteration++)
		{
			new_means = new double[totalClusters]	  {/*(zeros)*/}; //All specific-cluster related means summed-up to be averaged.
			elem_per_cluster = new int[totalClusters] {/*(zeros)*/}; //Holds number of elements found in every cluster.

			values = new double[elementsPerProcess];
			means = new double[totalClusters];

			MPI_Recv(values, elementsPerProcess, MPI_DOUBLE, MASTER, TAG_VALUES, MPI_COMM_WORLD, &statusReceive);
			MPI_Recv(means, totalClusters, MPI_DOUBLE, MASTER, TAG_MEANS, MPI_COMM_WORLD, &statusReceive);


			/*MAIN LOOP*/
			int	   min_cluster = 0;
			double local_value=0,
				   min_value;
			int i;
			#pragma omp for private(i) schedule(dynamic,elementsPerProcess)
			for( i=0; i < elementsPerProcess; i++){
				min_value = INFINITY;
				//printf("%f (%d), ", values[i], rank); //DEBUGGING.
				//printf("VALUE: %f,	MEAN: %f\n", values[i], means[i]); //DEBUGGING.
				int j;
				//#pragma omp parallel shared(local_value) private(j)
				//#pragma omp for  private(j)  schedule(dynamic,elementsPerProcess) reduction(-:local_value)
				for(j=0; j < totalClusters; j++)
				{
					local_value = values[i] - means[j];
					local_value = abs(local_value);
					if(local_value < min_value)
						min_value = local_value, min_cluster = j;
				}

				//printf("VALUE: %f	CLASS: %d\n", values[i], min_cluster); //DEBUGGING.

				//New cluster found for this value:
				new_means[min_cluster] += values[i];
				elem_per_cluster[min_cluster]++;
			}

			//Done with this chunk. Sending results to master:
			MPI_Send(new_means, totalClusters, MPI_DOUBLE, MASTER, TAG_MEANS, MPI_COMM_WORLD);
			MPI_Send(elem_per_cluster, totalClusters, MPI_INT, MASTER, TAG_COUNTS, MPI_COMM_WORLD);
		}
	}

	MPI_Finalize();
}

float randomF(float max){
	return (float) (rand()) / ((float) (RAND_MAX / max));
}

int random(int seed, int min, int max){ //Maximum is excluded.
	srand(time(NULL) + seed);
	return rand() % (max - min) + min;
}
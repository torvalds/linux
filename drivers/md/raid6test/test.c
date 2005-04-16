/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6test.c
 *
 * Test RAID-6 recovery with various algorithms
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "raid6.h"

#define NDISKS		16	/* Including P and Q */

const char raid6_empty_zero_page[PAGE_SIZE] __attribute__((aligned(256)));
struct raid6_calls raid6_call;

char *dataptrs[NDISKS];
char data[NDISKS][PAGE_SIZE];
char recovi[PAGE_SIZE], recovj[PAGE_SIZE];

void makedata(void)
{
	int i, j;

	for (  i = 0 ; i < NDISKS ; i++ ) {
		for ( j = 0 ; j < PAGE_SIZE ; j++ ) {
			data[i][j] = rand();
		}
		dataptrs[i] = data[i];
	}
}

int main(int argc, char *argv[])
{
	const struct raid6_calls * const * algo;
	int i, j;
	int erra, errb;

	makedata();

	for ( algo = raid6_algos ; *algo ; algo++ ) {
		if ( !(*algo)->valid || (*algo)->valid() ) {
			raid6_call = **algo;

			/* Nuke syndromes */
			memset(data[NDISKS-2], 0xee, 2*PAGE_SIZE);

			/* Generate assumed good syndrome */
			raid6_call.gen_syndrome(NDISKS, PAGE_SIZE, (void **)&dataptrs);

			for ( i = 0 ; i < NDISKS-1 ; i++ ) {
				for ( j = i+1 ; j < NDISKS ; j++ ) {
					memset(recovi, 0xf0, PAGE_SIZE);
					memset(recovj, 0xba, PAGE_SIZE);

					dataptrs[i] = recovi;
					dataptrs[j] = recovj;

					raid6_dual_recov(NDISKS, PAGE_SIZE, i, j, (void **)&dataptrs);

					erra = memcmp(data[i], recovi, PAGE_SIZE);
					errb = memcmp(data[j], recovj, PAGE_SIZE);

					if ( i < NDISKS-2 && j == NDISKS-1 ) {
						/* We don't implement the DQ failure scenario, since it's
						   equivalent to a RAID-5 failure (XOR, then recompute Q) */
					} else {
						printf("algo=%-8s  faila=%3d(%c)  failb=%3d(%c)  %s\n",
						       raid6_call.name,
						       i, (i==NDISKS-2)?'P':'D',
						       j, (j==NDISKS-1)?'Q':(j==NDISKS-2)?'P':'D',
						       (!erra && !errb) ? "OK" :
						       !erra ? "ERRB" :
						       !errb ? "ERRA" :
						       "ERRAB");
					}

					dataptrs[i] = data[i];
					dataptrs[j] = data[j];
				}
			}
		}
		printf("\n");
	}

	printf("\n");
	/* Pick the best algorithm test */
	raid6_select_algo();

	return 0;
}

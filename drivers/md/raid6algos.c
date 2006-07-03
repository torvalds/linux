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
 * raid6algos.c
 *
 * Algorithm list and algorithm selection for RAID-6
 */

#include "raid6.h"
#ifndef __KERNEL__
#include <sys/mman.h>
#include <stdio.h>
#endif

struct raid6_calls raid6_call;

/* Various routine sets */
extern const struct raid6_calls raid6_intx1;
extern const struct raid6_calls raid6_intx2;
extern const struct raid6_calls raid6_intx4;
extern const struct raid6_calls raid6_intx8;
extern const struct raid6_calls raid6_intx16;
extern const struct raid6_calls raid6_intx32;
extern const struct raid6_calls raid6_mmxx1;
extern const struct raid6_calls raid6_mmxx2;
extern const struct raid6_calls raid6_sse1x1;
extern const struct raid6_calls raid6_sse1x2;
extern const struct raid6_calls raid6_sse2x1;
extern const struct raid6_calls raid6_sse2x2;
extern const struct raid6_calls raid6_sse2x4;
extern const struct raid6_calls raid6_altivec1;
extern const struct raid6_calls raid6_altivec2;
extern const struct raid6_calls raid6_altivec4;
extern const struct raid6_calls raid6_altivec8;

const struct raid6_calls * const raid6_algos[] = {
	&raid6_intx1,
	&raid6_intx2,
	&raid6_intx4,
	&raid6_intx8,
#if defined(__ia64__)
	&raid6_intx16,
	&raid6_intx32,
#endif
#if defined(__i386__)
	&raid6_mmxx1,
	&raid6_mmxx2,
	&raid6_sse1x1,
	&raid6_sse1x2,
	&raid6_sse2x1,
	&raid6_sse2x2,
#endif
#if defined(__x86_64__)
	&raid6_sse2x1,
	&raid6_sse2x2,
	&raid6_sse2x4,
#endif
#ifdef CONFIG_ALTIVEC
	&raid6_altivec1,
	&raid6_altivec2,
	&raid6_altivec4,
	&raid6_altivec8,
#endif
	NULL
};

#ifdef __KERNEL__
#define RAID6_TIME_JIFFIES_LG2	4
#else
/* Need more time to be stable in userspace */
#define RAID6_TIME_JIFFIES_LG2	9
#endif

/* Try to pick the best algorithm */
/* This code uses the gfmul table as convenient data set to abuse */

int __init raid6_select_algo(void)
{
	const struct raid6_calls * const * algo;
	const struct raid6_calls * best;
	char *syndromes;
	void *dptrs[(65536/PAGE_SIZE)+2];
	int i, disks;
	unsigned long perf, bestperf;
	int bestprefer;
	unsigned long j0, j1;

	disks = (65536/PAGE_SIZE)+2;
	for ( i = 0 ; i < disks-2 ; i++ ) {
		dptrs[i] = ((char *)raid6_gfmul) + PAGE_SIZE*i;
	}

	/* Normal code - use a 2-page allocation to avoid D$ conflict */
	syndromes = (void *) __get_free_pages(GFP_KERNEL, 1);

	if ( !syndromes ) {
		printk("raid6: Yikes!  No memory available.\n");
		return -ENOMEM;
	}

	dptrs[disks-2] = syndromes;
	dptrs[disks-1] = syndromes + PAGE_SIZE;

	bestperf = 0;  bestprefer = 0;  best = NULL;

	for ( algo = raid6_algos ; *algo ; algo++ ) {
		if ( !(*algo)->valid || (*algo)->valid() ) {
			perf = 0;

			preempt_disable();
			j0 = jiffies;
			while ( (j1 = jiffies) == j0 )
				cpu_relax();
			while ( (jiffies-j1) < (1 << RAID6_TIME_JIFFIES_LG2) ) {
				(*algo)->gen_syndrome(disks, PAGE_SIZE, dptrs);
				perf++;
			}
			preempt_enable();

			if ( (*algo)->prefer > bestprefer ||
			     ((*algo)->prefer == bestprefer &&
			      perf > bestperf) ) {
				best = *algo;
				bestprefer = best->prefer;
				bestperf = perf;
			}
			printk("raid6: %-8s %5ld MB/s\n", (*algo)->name,
			       (perf*HZ) >> (20-16+RAID6_TIME_JIFFIES_LG2));
		}
	}

	if (best) {
		printk("raid6: using algorithm %s (%ld MB/s)\n",
		       best->name,
		       (bestperf*HZ) >> (20-16+RAID6_TIME_JIFFIES_LG2));
		raid6_call = *best;
	} else
		printk("raid6: Yikes!  No algorithm found!\n");

	free_pages((unsigned long)syndromes, 1);

	return best ? 0 : -EINVAL;
}

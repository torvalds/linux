/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lvfs/lvfs_lib.c
 *
 * Lustre filesystem abstraction routines
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 */
#include <linux/module.h>
#include <lustre_lib.h>
#include <lprocfs_status.h>

#ifdef LPROCFS
void lprocfs_counter_add(struct lprocfs_stats *stats, int idx, long amount)
{
	struct lprocfs_counter		*percpu_cntr;
	struct lprocfs_counter_header	*header;
	int				smp_id;
	unsigned long			flags = 0;

	if (stats == NULL)
		return;

	/* With per-client stats, statistics are allocated only for
	 * single CPU area, so the smp_id should be 0 always. */
	smp_id = lprocfs_stats_lock(stats, LPROCFS_GET_SMP_ID, &flags);
	if (smp_id < 0)
		return;

	header = &stats->ls_cnt_header[idx];
	percpu_cntr = lprocfs_stats_counter_get(stats, smp_id, idx);
	percpu_cntr->lc_count++;

	if (header->lc_config & LPROCFS_CNTR_AVGMINMAX) {
		/*
		 * lprocfs_counter_add() can be called in interrupt context,
		 * as memory allocation could trigger memory shrinker call
		 * ldlm_pool_shrink(), which calls lprocfs_counter_add().
		 * LU-1727.
		 *
		 * Only obd_memory uses LPROCFS_STATS_FLAG_IRQ_SAFE
		 * flag, because it needs accurate counting lest memory leak
		 * check reports error.
		 */
		if (in_interrupt() &&
		    (stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE) != 0)
			percpu_cntr->lc_sum_irq += amount;
		else
			percpu_cntr->lc_sum += amount;

		if (header->lc_config & LPROCFS_CNTR_STDDEV)
			percpu_cntr->lc_sumsquare += (__s64)amount * amount;
		if (amount < percpu_cntr->lc_min)
			percpu_cntr->lc_min = amount;
		if (amount > percpu_cntr->lc_max)
			percpu_cntr->lc_max = amount;
	}
	lprocfs_stats_unlock(stats, LPROCFS_GET_SMP_ID, &flags);
}
EXPORT_SYMBOL(lprocfs_counter_add);

void lprocfs_counter_sub(struct lprocfs_stats *stats, int idx, long amount)
{
	struct lprocfs_counter		*percpu_cntr;
	struct lprocfs_counter_header	*header;
	int				smp_id;
	unsigned long			flags = 0;

	if (stats == NULL)
		return;

	/* With per-client stats, statistics are allocated only for
	 * single CPU area, so the smp_id should be 0 always. */
	smp_id = lprocfs_stats_lock(stats, LPROCFS_GET_SMP_ID, &flags);
	if (smp_id < 0)
		return;

	header = &stats->ls_cnt_header[idx];
	percpu_cntr = lprocfs_stats_counter_get(stats, smp_id, idx);
	if (header->lc_config & LPROCFS_CNTR_AVGMINMAX) {
		/*
		 * Sometimes we use RCU callbacks to free memory which calls
		 * lprocfs_counter_sub(), and RCU callbacks may execute in
		 * softirq context - right now that's the only case we're in
		 * softirq context here, use separate counter for that.
		 * bz20650.
		 *
		 * Only obd_memory uses LPROCFS_STATS_FLAG_IRQ_SAFE
		 * flag, because it needs accurate counting lest memory leak
		 * check reports error.
		 */
		if (in_interrupt() &&
		    (stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE) != 0)
			percpu_cntr->lc_sum_irq -= amount;
		else
			percpu_cntr->lc_sum -= amount;
	}
	lprocfs_stats_unlock(stats, LPROCFS_GET_SMP_ID, &flags);
}
EXPORT_SYMBOL(lprocfs_counter_sub);

int lprocfs_stats_alloc_one(struct lprocfs_stats *stats, unsigned int cpuid)
{
	struct lprocfs_counter	*cntr;
	unsigned int		percpusize;
	int			rc = -ENOMEM;
	unsigned long		flags = 0;
	int			i;

	LASSERT(stats->ls_percpu[cpuid] == NULL);
	LASSERT((stats->ls_flags & LPROCFS_STATS_FLAG_NOPERCPU) == 0);

	percpusize = lprocfs_stats_counter_size(stats);
	LIBCFS_ALLOC_ATOMIC(stats->ls_percpu[cpuid], percpusize);
	if (stats->ls_percpu[cpuid] != NULL) {
		rc = 0;
		if (unlikely(stats->ls_biggest_alloc_num <= cpuid)) {
			if (stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE)
				spin_lock_irqsave(&stats->ls_lock, flags);
			else
				spin_lock(&stats->ls_lock);
			if (stats->ls_biggest_alloc_num <= cpuid)
				stats->ls_biggest_alloc_num = cpuid + 1;
			if (stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE) {
				spin_unlock_irqrestore(&stats->ls_lock, flags);
			} else {
				spin_unlock(&stats->ls_lock);
			}
		}
		/* initialize the ls_percpu[cpuid] non-zero counter */
		for (i = 0; i < stats->ls_num; ++i) {
			cntr = lprocfs_stats_counter_get(stats, cpuid, i);
			cntr->lc_min = LC_MIN_INIT;
		}
	}

	return rc;
}
EXPORT_SYMBOL(lprocfs_stats_alloc_one);
#endif  /* LPROCFS */

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
 *
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2013, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/lprocfs_counters.c
 *
 * Lustre lprocfs counter routines
 *
 * Author: Andreas Dilger <andreas.dilger@intel.com>
 */

#include <linux/module.h>
#include "../include/lprocfs_status.h"
#include "../include/obd_support.h"

struct lprocfs_stats *obd_memory = NULL;
EXPORT_SYMBOL(obd_memory);

void lprocfs_counter_add(struct lprocfs_stats *stats, int idx, long amount)
{
	struct lprocfs_counter		*percpu_cntr;
	struct lprocfs_counter_header	*header;
	int				smp_id;
	unsigned long			flags = 0;

	if (stats == NULL)
		return;

	LASSERTF(0 <= idx && idx < stats->ls_num,
		 "idx %d, ls_num %hu\n", idx, stats->ls_num);

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

	LASSERTF(0 <= idx && idx < stats->ls_num,
		 "idx %d, ls_num %hu\n", idx, stats->ls_num);

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

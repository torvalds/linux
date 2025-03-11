// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/atomic.h>

#include <drm/drm_print.h>

#include "xe_gt.h"
#include "xe_gt_stats.h"

/**
 * xe_gt_stats_incr - Increments the specified stats counter
 * @gt: GT structure
 * @id: xe_gt_stats_id type id that needs to be incremented
 * @incr: value to be incremented with
 *
 * Increments the specified stats counter.
 */
void xe_gt_stats_incr(struct xe_gt *gt, const enum xe_gt_stats_id id, int incr)
{
	if (id >= __XE_GT_STATS_NUM_IDS)
		return;

	atomic64_add(incr, &gt->stats.counters[id]);
}

static const char *const stat_description[__XE_GT_STATS_NUM_IDS] = {
	"tlb_inval_count",
	"vma_pagefault_count",
	"vma_pagefault_kb",
};

/**
 * xe_gt_stats_print_info - Print the GT stats
 * @gt: GT structure
 * @p: drm_printer where it will be printed out.
 *
 * This prints out all the available GT stats.
 */
int xe_gt_stats_print_info(struct xe_gt *gt, struct drm_printer *p)
{
	enum xe_gt_stats_id id;

	for (id = 0; id < __XE_GT_STATS_NUM_IDS; ++id)
		drm_printf(p, "%s: %lld\n", stat_description[id],
			   atomic64_read(&gt->stats.counters[id]));

	return 0;
}

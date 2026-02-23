/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_STATS_H_
#define _XE_GT_STATS_H_

#include <linux/ktime.h>

#include "xe_gt_stats_types.h"

struct xe_gt;
struct drm_printer;

#ifdef CONFIG_DEBUG_FS
int xe_gt_stats_print_info(struct xe_gt *gt, struct drm_printer *p);
void xe_gt_stats_clear(struct xe_gt *gt);
void xe_gt_stats_incr(struct xe_gt *gt, const enum xe_gt_stats_id id, int incr);
#else
static inline void
xe_gt_stats_incr(struct xe_gt *gt, const enum xe_gt_stats_id id,
		 int incr)
{
}

#endif

/**
 * xe_gt_stats_ktime_us_delta() - Get delta in microseconds between now and a
 * start time
 * @start: Start time
 *
 * Helper for GT stats to get delta in microseconds between now and a start
 * time, compiles out if GT stats are disabled.
 *
 * Return: Delta in microseconds between now and a start time
 */
static inline s64 xe_gt_stats_ktime_us_delta(ktime_t start)
{
	return IS_ENABLED(CONFIG_DEBUG_FS) ?
		ktime_us_delta(ktime_get(), start) : 0;
}

/**
 * xe_gt_stats_ktime_get() - Get current ktime
 *
 * Helper for GT stats to get current ktime, compiles out if GT stats are
 * disabled.
 *
 * Return: Get current ktime
 */
static inline ktime_t xe_gt_stats_ktime_get(void)
{
	return IS_ENABLED(CONFIG_DEBUG_FS) ? ktime_get() : 0;
}

#endif

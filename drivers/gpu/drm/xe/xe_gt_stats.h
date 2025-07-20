/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_STATS_H_
#define _XE_GT_STATS_H_

#include "xe_gt_stats_types.h"

struct xe_gt;
struct drm_printer;

#ifdef CONFIG_DEBUG_FS
int xe_gt_stats_print_info(struct xe_gt *gt, struct drm_printer *p);
void xe_gt_stats_incr(struct xe_gt *gt, const enum xe_gt_stats_id id, int incr);
#else
static inline void
xe_gt_stats_incr(struct xe_gt *gt, const enum xe_gt_stats_id id,
		 int incr)
{
}

#endif
#endif

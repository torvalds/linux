/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_IDLE_H_
#define _XE_GT_IDLE_H_

#include "xe_gt_idle_types.h"

struct drm_printer;
struct xe_gt;

int xe_gt_idle_init(struct xe_gt_idle *gtidle);
void xe_gt_idle_enable_c6(struct xe_gt *gt);
void xe_gt_idle_disable_c6(struct xe_gt *gt);
void xe_gt_idle_enable_pg(struct xe_gt *gt);
void xe_gt_idle_disable_pg(struct xe_gt *gt);
int xe_gt_idle_pg_print(struct xe_gt *gt, struct drm_printer *p);
u64 xe_gt_idle_residency_msec(struct xe_gt_idle *gtidle);

#endif /* _XE_GT_IDLE_H_ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_CCS_MODE_H_
#define _XE_GT_CCS_MODE_H_

#include "xe_device_types.h"
#include "xe_gt.h"
#include "xe_gt_types.h"
#include "xe_platform_types.h"

void xe_gt_apply_ccs_mode(struct xe_gt *gt);
void xe_gt_ccs_mode_sysfs_init(struct xe_gt *gt);

static inline bool xe_gt_ccs_mode_enabled(const struct xe_gt *gt)
{
	/* Check if there are more than one compute engines available */
	return hweight32(CCS_MASK(gt)) > 1;
}

#endif


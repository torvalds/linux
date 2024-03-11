/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_TOPOLOGY_H_
#define _XE_GT_TOPOLOGY_H_

#include "xe_gt_types.h"

struct drm_printer;

void xe_gt_topology_init(struct xe_gt *gt);

void xe_gt_topology_dump(struct xe_gt *gt, struct drm_printer *p);

unsigned int
xe_dss_mask_group_ffs(const xe_dss_mask_t mask, int groupsize, int groupnum);

bool xe_dss_mask_empty(const xe_dss_mask_t mask);

bool
xe_gt_topology_has_dss_in_quadrant(struct xe_gt *gt, int quad);

#endif /* _XE_GT_TOPOLOGY_H_ */

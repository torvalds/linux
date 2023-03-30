/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __XE_GT_TOPOLOGY_H__
#define __XE_GT_TOPOLOGY_H__

#include "xe_gt_types.h"

struct drm_printer;

void xe_gt_topology_init(struct xe_gt *gt);

void xe_gt_topology_dump(struct xe_gt *gt, struct drm_printer *p);

unsigned int
xe_dss_mask_group_ffs(xe_dss_mask_t mask, int groupsize, int groupnum);

#endif /* __XE_GT_TOPOLOGY_H__ */

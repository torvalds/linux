/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_TOPOLOGY_H_
#define _XE_GT_TOPOLOGY_H_

#include "xe_gt_types.h"

/*
 * Loop over each DSS with the bit is 1 in geometry or compute mask
 * @dss: iterated DSS bit from the DSS mask
 * @gt: GT structure
 */
#define for_each_dss(dss, gt) \
	for_each_or_bit((dss), \
			(gt)->fuse_topo.g_dss_mask, \
			(gt)->fuse_topo.c_dss_mask, \
			XE_MAX_DSS_FUSE_BITS)

struct drm_printer;

void xe_gt_topology_init(struct xe_gt *gt);

void xe_gt_topology_dump(struct xe_gt *gt, struct drm_printer *p);

/**
 * xe_gt_topology_mask_last_dss() - Returns the index of the last DSS in a mask.
 * @mask: Input DSS mask
 *
 * Return: Index of the last DSS in the input DSS mask,
 *	   XE_MAX_DSS_FUSE_BITS if DSS mask is empty.
 */
static inline unsigned int
xe_gt_topology_mask_last_dss(const xe_dss_mask_t mask)
{
	return find_last_bit(mask, XE_MAX_DSS_FUSE_BITS);
}

unsigned int
xe_dss_mask_group_ffs(const xe_dss_mask_t mask, int groupsize, int groupnum);

bool xe_dss_mask_empty(const xe_dss_mask_t mask);

bool
xe_gt_topology_has_dss_in_quadrant(struct xe_gt *gt, int quad);

bool xe_gt_has_geometry_dss(struct xe_gt *gt, unsigned int dss);
bool xe_gt_has_compute_dss(struct xe_gt *gt, unsigned int dss);

#endif /* _XE_GT_TOPOLOGY_H_ */

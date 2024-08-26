/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_MCR_H_
#define _XE_GT_MCR_H_

#include "regs/xe_reg_defs.h"
#include "xe_gt_topology.h"

struct drm_printer;
struct xe_gt;

void xe_gt_mcr_init_early(struct xe_gt *gt);
void xe_gt_mcr_init(struct xe_gt *gt);

void xe_gt_mcr_set_implicit_defaults(struct xe_gt *gt);

u32 xe_gt_mcr_unicast_read(struct xe_gt *gt, struct xe_reg_mcr mcr_reg,
			   int group, int instance);
u32 xe_gt_mcr_unicast_read_any(struct xe_gt *gt, struct xe_reg_mcr mcr_reg);

void xe_gt_mcr_unicast_write(struct xe_gt *gt, struct xe_reg_mcr mcr_reg,
			     u32 value, int group, int instance);
void xe_gt_mcr_multicast_write(struct xe_gt *gt, struct xe_reg_mcr mcr_reg,
			       u32 value);

void xe_gt_mcr_steering_dump(struct xe_gt *gt, struct drm_printer *p);
void xe_gt_mcr_get_dss_steering(struct xe_gt *gt, unsigned int dss, u16 *group, u16 *instance);

/*
 * Loop over each DSS and determine the group and instance IDs that
 * should be used to steer MCR accesses toward this DSS.
 * @dss: DSS ID to obtain steering for
 * @gt: GT structure
 * @group: steering group ID, data type: u16
 * @instance: steering instance ID, data type: u16
 */
#define for_each_dss_steering(dss, gt, group, instance) \
	for_each_dss((dss), (gt)) \
		for_each_if((xe_gt_mcr_get_dss_steering((gt), (dss), &(group), &(instance)), true))

/*
 * Loop over each DSS available for geometry and determine the group and
 * instance IDs that should be used to steer MCR accesses toward this DSS.
 * @dss: DSS ID to obtain steering for
 * @gt: GT structure
 * @group: steering group ID, data type: u16
 * @instance: steering instance ID, data type: u16
 */
#define for_each_geometry_dss(dss, gt, group, instance) \
		for_each_dss_steering(dss, gt, group, instance) \
			if (xe_gt_has_geometry_dss(gt, dss))

/*
 * Loop over each DSS available for compute and determine the group and
 * instance IDs that should be used to steer MCR accesses toward this DSS.
 * @dss: DSS ID to obtain steering for
 * @gt: GT structure
 * @group: steering group ID, data type: u16
 * @instance: steering instance ID, data type: u16
 */
#define for_each_compute_dss(dss, gt, group, instance) \
		for_each_dss_steering(dss, gt, group, instance) \
			if (xe_gt_has_compute_dss(gt, dss))

#endif /* _XE_GT_MCR_H_ */

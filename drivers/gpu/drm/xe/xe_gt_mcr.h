/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_MCR_H_
#define _XE_GT_MCR_H_

#include "regs/xe_reg_defs.h"

struct drm_printer;
struct xe_gt;

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

#endif /* _XE_GT_MCR_H_ */

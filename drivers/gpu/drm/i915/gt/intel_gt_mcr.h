/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_GT_MCR__
#define __INTEL_GT_MCR__

#include "intel_gt_types.h"

void intel_gt_mcr_init(struct intel_gt *gt);

u32 intel_uncore_read_with_mcr_steering_fw(struct intel_uncore *uncore,
					   i915_reg_t reg,
					   int slice, int subslice);
u32 intel_uncore_read_with_mcr_steering(struct intel_uncore *uncore,
					i915_reg_t reg,	int slice, int subslice);
void intel_uncore_write_with_mcr_steering(struct intel_uncore *uncore,
					  i915_reg_t reg, u32 value,
					  int slice, int subslice);

u32 intel_gt_read_register_fw(struct intel_gt *gt, i915_reg_t reg);
u32 intel_gt_read_register(struct intel_gt *gt, i915_reg_t reg);

static inline bool intel_gt_needs_read_steering(struct intel_gt *gt,
						enum intel_steering_type type)
{
	return gt->steering_table[type];
}

void intel_gt_get_valid_steering_for_reg(struct intel_gt *gt, i915_reg_t reg,
					 u8 *sliceid, u8 *subsliceid);

void intel_gt_report_steering(struct drm_printer *p, struct intel_gt *gt,
			      bool dump_table);

#endif /* __INTEL_GT_MCR__ */

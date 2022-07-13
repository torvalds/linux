/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_GT_MCR__
#define __INTEL_GT_MCR__

#include "intel_gt_types.h"

void intel_gt_mcr_init(struct intel_gt *gt);

u32 intel_gt_mcr_read(struct intel_gt *gt,
		      i915_reg_t reg,
		      int group, int instance);
u32 intel_gt_mcr_read_any_fw(struct intel_gt *gt, i915_reg_t reg);
u32 intel_gt_mcr_read_any(struct intel_gt *gt, i915_reg_t reg);

void intel_gt_mcr_unicast_write(struct intel_gt *gt,
				i915_reg_t reg, u32 value,
				int group, int instance);
void intel_gt_mcr_multicast_write(struct intel_gt *gt,
				  i915_reg_t reg, u32 value);
void intel_gt_mcr_multicast_write_fw(struct intel_gt *gt,
				     i915_reg_t reg, u32 value);

void intel_gt_mcr_get_nonterminated_steering(struct intel_gt *gt,
					     i915_reg_t reg,
					     u8 *group, u8 *instance);

void intel_gt_mcr_report_steering(struct drm_printer *p, struct intel_gt *gt,
				  bool dump_table);

#endif /* __INTEL_GT_MCR__ */

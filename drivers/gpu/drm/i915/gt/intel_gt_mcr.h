/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_GT_MCR__
#define __INTEL_GT_MCR__

#include "intel_gt_types.h"

void intel_gt_mcr_init(struct intel_gt *gt);

u32 intel_gt_mcr_read(struct intel_gt *gt,
		      i915_mcr_reg_t reg,
		      int group, int instance);
u32 intel_gt_mcr_read_any_fw(struct intel_gt *gt, i915_mcr_reg_t reg);
u32 intel_gt_mcr_read_any(struct intel_gt *gt, i915_mcr_reg_t reg);

void intel_gt_mcr_unicast_write(struct intel_gt *gt,
				i915_mcr_reg_t reg, u32 value,
				int group, int instance);
void intel_gt_mcr_multicast_write(struct intel_gt *gt,
				  i915_mcr_reg_t reg, u32 value);
void intel_gt_mcr_multicast_write_fw(struct intel_gt *gt,
				     i915_mcr_reg_t reg, u32 value);

u32 intel_gt_mcr_multicast_rmw(struct intel_gt *gt, i915_mcr_reg_t reg,
			       u32 clear, u32 set);

void intel_gt_mcr_get_nonterminated_steering(struct intel_gt *gt,
					     i915_mcr_reg_t reg,
					     u8 *group, u8 *instance);

void intel_gt_mcr_report_steering(struct drm_printer *p, struct intel_gt *gt,
				  bool dump_table);

void intel_gt_mcr_get_ss_steering(struct intel_gt *gt, unsigned int dss,
				  unsigned int *group, unsigned int *instance);

int intel_gt_mcr_wait_for_reg(struct intel_gt *gt,
			      i915_mcr_reg_t reg,
			      u32 mask,
			      u32 value,
			      unsigned int fast_timeout_us,
			      unsigned int slow_timeout_ms);

/*
 * Helper for for_each_ss_steering loop.  On pre-Xe_HP platforms, subslice
 * presence is determined by using the group/instance as direct lookups in the
 * slice/subslice topology.  On Xe_HP and beyond, the steering is unrelated to
 * the topology, so we lookup the DSS ID directly in "slice 0."
 */
#define _HAS_SS(ss_, gt_, group_, instance_) ( \
	GRAPHICS_VER_FULL(gt_->i915) >= IP_VER(12, 50) ? \
		intel_sseu_has_subslice(&(gt_)->info.sseu, 0, ss_) : \
		intel_sseu_has_subslice(&(gt_)->info.sseu, group_, instance_))

/*
 * Loop over each subslice/DSS and determine the group and instance IDs that
 * should be used to steer MCR accesses toward this DSS.
 */
#define for_each_ss_steering(ss_, gt_, group_, instance_) \
	for (ss_ = 0, intel_gt_mcr_get_ss_steering(gt_, 0, &group_, &instance_); \
	     ss_ < I915_MAX_SS_FUSE_BITS; \
	     ss_++, intel_gt_mcr_get_ss_steering(gt_, ss_, &group_, &instance_)) \
		for_each_if(_HAS_SS(ss_, gt_, group_, instance_))

#endif /* __INTEL_GT_MCR__ */

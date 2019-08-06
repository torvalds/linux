/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_GT__
#define __INTEL_GT__

#include "intel_engine_types.h"
#include "intel_gt_types.h"
#include "intel_reset.h"

struct drm_i915_private;

static inline struct intel_gt *uc_to_gt(struct intel_uc *uc)
{
	return container_of(uc, struct intel_gt, uc);
}

static inline struct intel_gt *guc_to_gt(struct intel_guc *guc)
{
	return container_of(guc, struct intel_gt, uc.guc);
}

static inline struct intel_gt *huc_to_gt(struct intel_huc *huc)
{
	return container_of(huc, struct intel_gt, uc.huc);
}

void intel_gt_init_early(struct intel_gt *gt, struct drm_i915_private *i915);
void intel_gt_init_hw(struct drm_i915_private *i915);

void intel_gt_cleanup_early(struct intel_gt *gt);

void intel_gt_check_and_clear_faults(struct intel_gt *gt);
void intel_gt_clear_error_registers(struct intel_gt *gt,
				    intel_engine_mask_t engine_mask);

void intel_gt_flush_ggtt_writes(struct intel_gt *gt);
void intel_gt_chipset_flush(struct intel_gt *gt);

void intel_gt_init_hangcheck(struct intel_gt *gt);

int intel_gt_init_scratch(struct intel_gt *gt, unsigned int size);
void intel_gt_fini_scratch(struct intel_gt *gt);

static inline u32 intel_gt_scratch_offset(const struct intel_gt *gt,
					  enum intel_gt_scratch_field field)
{
	return i915_ggtt_offset(gt->scratch) + field;
}

static inline bool intel_gt_is_wedged(struct intel_gt *gt)
{
	return __intel_reset_failed(&gt->reset);
}

void intel_gt_queue_hangcheck(struct intel_gt *gt);

#endif /* __INTEL_GT_H__ */

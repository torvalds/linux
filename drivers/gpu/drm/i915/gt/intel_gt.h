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

#define GT_TRACE(gt, fmt, ...) do {					\
	const struct intel_gt *gt__ __maybe_unused = (gt);		\
	GEM_TRACE("%s " fmt, dev_name(gt__->i915->drm.dev),		\
		  ##__VA_ARGS__);					\
} while (0)

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
void intel_gt_init_hw_early(struct intel_gt *gt, struct i915_ggtt *ggtt);
int __must_check intel_gt_init_hw(struct intel_gt *gt);
int intel_gt_init(struct intel_gt *gt);
void intel_gt_driver_register(struct intel_gt *gt);

void intel_gt_driver_unregister(struct intel_gt *gt);
void intel_gt_driver_remove(struct intel_gt *gt);
void intel_gt_driver_release(struct intel_gt *gt);

void intel_gt_driver_late_release(struct intel_gt *gt);

void intel_gt_check_and_clear_faults(struct intel_gt *gt);
void intel_gt_clear_error_registers(struct intel_gt *gt,
				    intel_engine_mask_t engine_mask);

void intel_gt_flush_ggtt_writes(struct intel_gt *gt);
void intel_gt_chipset_flush(struct intel_gt *gt);

static inline u32 intel_gt_scratch_offset(const struct intel_gt *gt,
					  enum intel_gt_scratch_field field)
{
	return i915_ggtt_offset(gt->scratch) + field;
}

static inline bool intel_gt_is_wedged(const struct intel_gt *gt)
{
	return __intel_reset_failed(&gt->reset);
}

static inline bool intel_gt_has_init_error(const struct intel_gt *gt)
{
	return test_bit(I915_WEDGED_ON_INIT, &gt->reset.flags);
}

#endif /* __INTEL_GT_H__ */

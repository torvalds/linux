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
struct drm_printer;

#define GT_TRACE(gt, fmt, ...) do {					\
	const struct intel_gt *gt__ __maybe_unused = (gt);		\
	GEM_TRACE("%s " fmt, dev_name(gt__->i915->drm.dev),		\
		  ##__VA_ARGS__);					\
} while (0)

static inline bool gt_is_root(struct intel_gt *gt)
{
	return !gt->info.id;
}

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

static inline struct intel_gt *gsc_to_gt(struct intel_gsc *gsc)
{
	return container_of(gsc, struct intel_gt, gsc);
}

void intel_gt_common_init_early(struct intel_gt *gt);
int intel_root_gt_init_early(struct drm_i915_private *i915);
int intel_gt_assign_ggtt(struct intel_gt *gt);
int intel_gt_init_mmio(struct intel_gt *gt);
int __must_check intel_gt_init_hw(struct intel_gt *gt);
int intel_gt_init(struct intel_gt *gt);
void intel_gt_driver_register(struct intel_gt *gt);

void intel_gt_driver_unregister(struct intel_gt *gt);
void intel_gt_driver_remove(struct intel_gt *gt);
void intel_gt_driver_release(struct intel_gt *gt);
void intel_gt_driver_late_release_all(struct drm_i915_private *i915);

int intel_gt_wait_for_idle(struct intel_gt *gt, long timeout);

void intel_gt_check_and_clear_faults(struct intel_gt *gt);
i915_reg_t intel_gt_perf_limit_reasons_reg(struct intel_gt *gt);
void intel_gt_clear_error_registers(struct intel_gt *gt,
				    intel_engine_mask_t engine_mask);

void intel_gt_flush_ggtt_writes(struct intel_gt *gt);
void intel_gt_chipset_flush(struct intel_gt *gt);

static inline u32 intel_gt_scratch_offset(const struct intel_gt *gt,
					  enum intel_gt_scratch_field field)
{
	return i915_ggtt_offset(gt->scratch) + field;
}

static inline bool intel_gt_has_unrecoverable_error(const struct intel_gt *gt)
{
	return test_bit(I915_WEDGED_ON_INIT, &gt->reset.flags) ||
	       test_bit(I915_WEDGED_ON_FINI, &gt->reset.flags);
}

static inline bool intel_gt_is_wedged(const struct intel_gt *gt)
{
	GEM_BUG_ON(intel_gt_has_unrecoverable_error(gt) &&
		   !test_bit(I915_WEDGED, &gt->reset.flags));

	return unlikely(test_bit(I915_WEDGED, &gt->reset.flags));
}

int intel_gt_probe_all(struct drm_i915_private *i915);
int intel_gt_tiles_init(struct drm_i915_private *i915);
void intel_gt_release_all(struct drm_i915_private *i915);

#define for_each_gt(gt__, i915__, id__) \
	for ((id__) = 0; \
	     (id__) < I915_MAX_GT; \
	     (id__)++) \
		for_each_if(((gt__) = (i915__)->gt[(id__)]))

void intel_gt_info_print(const struct intel_gt_info *info,
			 struct drm_printer *p);

void intel_gt_watchdog_work(struct work_struct *work);

static inline u32 intel_gt_tlb_seqno(const struct intel_gt *gt)
{
	return seqprop_sequence(&gt->tlb.seqno);
}

static inline u32 intel_gt_next_invalidate_tlb_full(const struct intel_gt *gt)
{
	return intel_gt_tlb_seqno(gt) | 1;
}

void intel_gt_invalidate_tlb(struct intel_gt *gt, u32 seqno);

#endif /* __INTEL_GT_H__ */

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

/*
 * Check that the GT is a graphics GT and has an IP version within the
 * specified range (inclusive).
 */
#define IS_GFX_GT_IP_RANGE(gt, from, until) ( \
	BUILD_BUG_ON_ZERO((from) < IP_VER(2, 0)) + \
	BUILD_BUG_ON_ZERO((until) < (from)) + \
	((gt)->type != GT_MEDIA && \
	 GRAPHICS_VER_FULL((gt)->i915) >= (from) && \
	 GRAPHICS_VER_FULL((gt)->i915) <= (until)))

/*
 * Check that the GT is a media GT and has an IP version within the
 * specified range (inclusive).
 *
 * Only usable on platforms with a standalone media design (i.e., IP version 13
 * and higher).
 */
#define IS_MEDIA_GT_IP_RANGE(gt, from, until) ( \
	BUILD_BUG_ON_ZERO((from) < IP_VER(13, 0)) + \
	BUILD_BUG_ON_ZERO((until) < (from)) + \
	((gt) && (gt)->type == GT_MEDIA && \
	 MEDIA_VER_FULL((gt)->i915) >= (from) && \
	 MEDIA_VER_FULL((gt)->i915) <= (until)))

/*
 * Check that the GT is a graphics GT with a specific IP version and has
 * a stepping in the range [from, until).  The lower stepping bound is
 * inclusive, the upper bound is exclusive.  The most common use-case of this
 * macro is for checking bounds for workarounds, which usually have a stepping
 * ("from") at which the hardware issue is first present and another stepping
 * ("until") at which a hardware fix is present and the software workaround is
 * no longer necessary.  E.g.,
 *
 *    IS_GFX_GT_IP_STEP(gt, IP_VER(12, 70), STEP_A0, STEP_B0)
 *    IS_GFX_GT_IP_STEP(gt, IP_VER(12, 71), STEP_B1, STEP_FOREVER)
 *
 * "STEP_FOREVER" can be passed as "until" for workarounds that have no upper
 * stepping bound for the specified IP version.
 */
#define IS_GFX_GT_IP_STEP(gt, ipver, from, until) ( \
	BUILD_BUG_ON_ZERO((until) <= (from)) + \
	(IS_GFX_GT_IP_RANGE((gt), (ipver), (ipver)) && \
	 IS_GRAPHICS_STEP((gt)->i915, (from), (until))))

/*
 * Check that the GT is a media GT with a specific IP version and has
 * a stepping in the range [from, until).  The lower stepping bound is
 * inclusive, the upper bound is exclusive.  The most common use-case of this
 * macro is for checking bounds for workarounds, which usually have a stepping
 * ("from") at which the hardware issue is first present and another stepping
 * ("until") at which a hardware fix is present and the software workaround is
 * no longer necessary.  "STEP_FOREVER" can be passed as "until" for
 * workarounds that have no upper stepping bound for the specified IP version.
 *
 * This macro may only be used to match on platforms that have a standalone
 * media design (i.e., media version 13 or higher).
 */
#define IS_MEDIA_GT_IP_STEP(gt, ipver, from, until) ( \
	BUILD_BUG_ON_ZERO((until) <= (from)) + \
	(IS_MEDIA_GT_IP_RANGE((gt), (ipver), (ipver)) && \
	 IS_MEDIA_STEP((gt)->i915, (from), (until))))

#define GT_TRACE(gt, fmt, ...) do {					\
	const struct intel_gt *gt__ __maybe_unused = (gt);		\
	GEM_TRACE("%s " fmt, dev_name(gt__->i915->drm.dev),		\
		  ##__VA_ARGS__);					\
} while (0)

static inline bool gt_is_root(struct intel_gt *gt)
{
	return !gt->info.id;
}

bool intel_gt_needs_wa_22016122933(struct intel_gt *gt);

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

static inline struct intel_gt *gsc_uc_to_gt(struct intel_gsc_uc *gsc_uc)
{
	return container_of(gsc_uc, struct intel_gt, uc.gsc);
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

enum i915_map_type intel_gt_coherent_map_type(struct intel_gt *gt,
					      struct drm_i915_gem_object *obj,
					      bool always_coherent);

void intel_gt_bind_context_set_ready(struct intel_gt *gt);
void intel_gt_bind_context_set_unready(struct intel_gt *gt);
bool intel_gt_is_bind_context_ready(struct intel_gt *gt);
#endif /* __INTEL_GT_H__ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2016 Intel Corporation
 */

#ifndef __INTEL_GGTT_FENCING_H__
#define __INTEL_GGTT_FENCING_H__

#include <linux/list.h>
#include <linux/types.h>

#include "i915_active.h"

struct drm_i915_gem_object;
struct i915_ggtt;
struct i915_vma;
struct intel_gt;
struct sg_table;

#define I965_FENCE_PAGE 4096UL

struct i915_fence_reg {
	struct list_head link;
	struct i915_ggtt *ggtt;
	struct i915_vma *vma;
	atomic_t pin_count;
	struct i915_active active;
	int id;
	/**
	 * Whether the tiling parameters for the currently
	 * associated fence register have changed. Note that
	 * for the purposes of tracking tiling changes we also
	 * treat the unfenced register, the register slot that
	 * the object occupies whilst it executes a fenced
	 * command (such as BLT on gen2/3), as a "fence".
	 */
	bool dirty;
	u32 start;
	u32 size;
	u32 tiling;
	u32 stride;
};

struct i915_fence_reg *i915_reserve_fence(struct i915_ggtt *ggtt);
void i915_unreserve_fence(struct i915_fence_reg *fence);

void intel_ggtt_restore_fences(struct i915_ggtt *ggtt);

void i915_gem_object_do_bit_17_swizzle(struct drm_i915_gem_object *obj,
				       struct sg_table *pages);
void i915_gem_object_save_bit_17_swizzle(struct drm_i915_gem_object *obj,
					 struct sg_table *pages);

void intel_ggtt_init_fences(struct i915_ggtt *ggtt);
void intel_ggtt_fini_fences(struct i915_ggtt *ggtt);

void intel_gt_init_swizzling(struct intel_gt *gt);

#endif

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef I915_VMA_H
#define I915_VMA_H

#include <uapi/drm/i915_drm.h>

#include "xe_ggtt_types.h"

#include <linux/refcount.h>

/* We don't want these from i915_drm.h in case of Xe */
#undef I915_TILING_X
#undef I915_TILING_Y
#define I915_TILING_X 0
#define I915_TILING_Y 0

struct xe_bo;

struct i915_vma {
	refcount_t ref;
	struct xe_bo *bo, *dpt;
	struct xe_ggtt_node *node;
};

#define i915_ggtt_clear_scanout(bo) do { } while (0)

#define i915_vma_fence_id(vma) -1

static inline u32 i915_ggtt_offset(const struct i915_vma *vma)
{
	return vma->node->base.start;
}

#endif

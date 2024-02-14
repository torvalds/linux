/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_GEM_TILING_H__
#define __I915_GEM_TILING_H__

#include <linux/types.h>

struct drm_i915_gem_object;
struct drm_i915_private;

bool i915_gem_object_needs_bit17_swizzle(struct drm_i915_gem_object *obj);
u32 i915_gem_fence_size(struct drm_i915_private *i915, u32 size,
			unsigned int tiling, unsigned int stride);
u32 i915_gem_fence_alignment(struct drm_i915_private *i915, u32 size,
			     unsigned int tiling, unsigned int stride);

#endif /* __I915_GEM_TILING_H__ */

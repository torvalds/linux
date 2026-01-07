/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_STOLEN_H__
#define __I915_GEM_STOLEN_H__

#include <linux/types.h>

struct drm_i915_gem_object;
struct drm_i915_private;

struct intel_memory_region *
i915_gem_stolen_smem_setup(struct drm_i915_private *i915, u16 type,
			   u16 instance);
struct intel_memory_region *
i915_gem_stolen_lmem_setup(struct drm_i915_private *i915, u16 type,
			   u16 instance);

struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_i915_private *i915,
			      resource_size_t size);

bool i915_gem_object_is_stolen(const struct drm_i915_gem_object *obj);

#define I915_GEM_STOLEN_BIAS SZ_128K

extern const struct intel_display_stolen_interface i915_display_stolen_interface;

#endif /* __I915_GEM_STOLEN_H__ */

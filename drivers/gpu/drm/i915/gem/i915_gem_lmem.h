/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_LMEM_H
#define __I915_GEM_LMEM_H

#include <linux/types.h>

struct drm_i915_private;
struct drm_i915_gem_object;
struct intel_memory_region;

extern const struct drm_i915_gem_object_ops i915_gem_lmem_obj_ops;

bool i915_gem_object_is_lmem(struct drm_i915_gem_object *obj);

struct drm_i915_gem_object *
i915_gem_object_create_lmem(struct drm_i915_private *i915,
			    resource_size_t size,
			    unsigned int flags);

struct drm_i915_gem_object *
__i915_gem_lmem_object_create(struct intel_memory_region *mem,
			      resource_size_t size,
			      unsigned int flags);

#endif /* !__I915_GEM_LMEM_H */

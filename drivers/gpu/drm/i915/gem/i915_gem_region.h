/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_REGION_H__
#define __I915_GEM_REGION_H__

#include <linux/types.h>

struct intel_memory_region;
struct drm_i915_gem_object;
struct sg_table;

int i915_gem_object_get_pages_buddy(struct drm_i915_gem_object *obj);
void i915_gem_object_put_pages_buddy(struct drm_i915_gem_object *obj,
				     struct sg_table *pages);

void i915_gem_object_init_memory_region(struct drm_i915_gem_object *obj,
					struct intel_memory_region *mem);
void i915_gem_object_release_memory_region(struct drm_i915_gem_object *obj);

struct drm_i915_gem_object *
i915_gem_object_create_region(struct intel_memory_region *mem,
			      resource_size_t size,
			      unsigned int flags);

#endif

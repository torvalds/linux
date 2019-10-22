// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "gem/i915_gem_region.h"
#include "intel_memory_region.h"

#include "mock_region.h"

static const struct drm_i915_gem_object_ops mock_region_obj_ops = {
	.get_pages = i915_gem_object_get_pages_buddy,
	.put_pages = i915_gem_object_put_pages_buddy,
	.release = i915_gem_object_release_memory_region,
};

static struct drm_i915_gem_object *
mock_object_create(struct intel_memory_region *mem,
		   resource_size_t size,
		   unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;
	struct drm_i915_gem_object *obj;

	if (size > BIT(mem->mm.max_order) * mem->mm.chunk_size)
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc();
	if (!obj)
		return ERR_PTR(-ENOMEM);

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &mock_region_obj_ops, &lock_class);

	obj->read_domains = I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT;

	i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);

	i915_gem_object_init_memory_region(obj, mem, flags);

	return obj;
}

static const struct intel_memory_region_ops mock_region_ops = {
	.init = intel_memory_region_init_buddy,
	.release = intel_memory_region_release_buddy,
	.create_object = mock_object_create,
};

struct intel_memory_region *
mock_region_create(struct drm_i915_private *i915,
		   resource_size_t start,
		   resource_size_t size,
		   resource_size_t min_page_size,
		   resource_size_t io_start)
{
	return intel_memory_region_create(i915, start, size, min_page_size,
					  io_start, &mock_region_ops);
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019-2021 Intel Corporation
 */

#include <drm/ttm/ttm_placement.h>
#include <linux/scatterlist.h>

#include <drm/ttm/ttm_placement.h>

#include "gem/i915_gem_region.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "mock_region.h"

static void mock_region_put_pages(struct drm_i915_gem_object *obj,
				  struct sg_table *pages)
{
	intel_region_ttm_resource_free(obj->mm.region, obj->mm.res);
	sg_free_table(pages);
	kfree(pages);
}

static int mock_region_get_pages(struct drm_i915_gem_object *obj)
{
	unsigned int flags;
	struct sg_table *pages;
	int err;

	flags = 0;
	if (obj->flags & I915_BO_ALLOC_CONTIGUOUS)
		flags |= TTM_PL_FLAG_CONTIGUOUS;

	obj->mm.res = intel_region_ttm_resource_alloc(obj->mm.region,
						      obj->base.size,
						      flags);
	if (IS_ERR(obj->mm.res))
		return PTR_ERR(obj->mm.res);

	pages = intel_region_ttm_resource_to_st(obj->mm.region, obj->mm.res);
	if (IS_ERR(pages)) {
		err = PTR_ERR(pages);
		goto err_free_resource;
	}

	__i915_gem_object_set_pages(obj, pages, i915_sg_dma_sizes(pages->sgl));

	return 0;

err_free_resource:
	intel_region_ttm_resource_free(obj->mm.region, obj->mm.res);
	return err;
}

static const struct drm_i915_gem_object_ops mock_region_obj_ops = {
	.name = "mock-region",
	.get_pages = mock_region_get_pages,
	.put_pages = mock_region_put_pages,
	.release = i915_gem_object_release_memory_region,
};

static int mock_object_init(struct intel_memory_region *mem,
			    struct drm_i915_gem_object *obj,
			    resource_size_t size,
			    resource_size_t page_size,
			    unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;

	if (size > resource_size(&mem->region))
		return -E2BIG;

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &mock_region_obj_ops, &lock_class, flags);

	obj->read_domains = I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT;

	i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);

	i915_gem_object_init_memory_region(obj, mem);

	return 0;
}

static void mock_region_fini(struct intel_memory_region *mem)
{
	struct drm_i915_private *i915 = mem->i915;
	int instance = mem->instance;

	intel_region_ttm_fini(mem);
	ida_free(&i915->selftest.mock_region_instances, instance);
}

static const struct intel_memory_region_ops mock_region_ops = {
	.init = intel_region_ttm_init,
	.release = mock_region_fini,
	.init_object = mock_object_init,
};

struct intel_memory_region *
mock_region_create(struct drm_i915_private *i915,
		   resource_size_t start,
		   resource_size_t size,
		   resource_size_t min_page_size,
		   resource_size_t io_start)
{
	int instance = ida_alloc_max(&i915->selftest.mock_region_instances,
				     TTM_NUM_MEM_TYPES - TTM_PL_PRIV - 1,
				     GFP_KERNEL);

	if (instance < 0)
		return ERR_PTR(instance);

	return intel_memory_region_create(i915, start, size, min_page_size,
					  io_start, INTEL_MEMORY_MOCK, instance,
					  &mock_region_ops);
}

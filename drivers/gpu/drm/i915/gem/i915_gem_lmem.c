// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_memory_region.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_lmem.h"
#include "i915_drv.h"

const struct drm_i915_gem_object_ops i915_gem_lmem_obj_ops = {
	.flags = I915_GEM_OBJECT_HAS_IOMEM,

	.get_pages = i915_gem_object_get_pages_buddy,
	.put_pages = i915_gem_object_put_pages_buddy,
	.release = i915_gem_object_release_memory_region,
};

/* XXX: Time to vfunc your life up? */
void __iomem *
i915_gem_object_lmem_io_map_page(struct drm_i915_gem_object *obj,
				 unsigned long n)
{
	resource_size_t offset;

	offset = i915_gem_object_get_dma_address(obj, n);
	offset -= obj->mm.region->region.start;

	return io_mapping_map_wc(&obj->mm.region->iomap, offset, PAGE_SIZE);
}

void __iomem *
i915_gem_object_lmem_io_map_page_atomic(struct drm_i915_gem_object *obj,
					unsigned long n)
{
	resource_size_t offset;

	offset = i915_gem_object_get_dma_address(obj, n);
	offset -= obj->mm.region->region.start;

	return io_mapping_map_atomic_wc(&obj->mm.region->iomap, offset);
}

void __iomem *
i915_gem_object_lmem_io_map(struct drm_i915_gem_object *obj,
			    unsigned long n,
			    unsigned long size)
{
	resource_size_t offset;

	GEM_BUG_ON(!i915_gem_object_is_contiguous(obj));

	offset = i915_gem_object_get_dma_address(obj, n);
	offset -= obj->mm.region->region.start;

	return io_mapping_map_wc(&obj->mm.region->iomap, offset, size);
}

bool i915_gem_object_is_lmem(struct drm_i915_gem_object *obj)
{
	return obj->ops == &i915_gem_lmem_obj_ops;
}

struct drm_i915_gem_object *
i915_gem_object_create_lmem(struct drm_i915_private *i915,
			    resource_size_t size,
			    unsigned int flags)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_LMEM],
					     size, flags);
}

struct drm_i915_gem_object *
__i915_gem_lmem_object_create(struct intel_memory_region *mem,
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
	i915_gem_object_init(obj, &i915_gem_lmem_obj_ops, &lock_class);

	obj->read_domains = I915_GEM_DOMAIN_WC | I915_GEM_DOMAIN_GTT;

	i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);

	i915_gem_object_init_memory_region(obj, mem, flags);

	return obj;
}

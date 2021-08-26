// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_memory_region.h"
#include "intel_region_ttm.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_lmem.h"
#include "i915_drv.h"

static void lmem_put_pages(struct drm_i915_gem_object *obj,
			   struct sg_table *pages)
{
	intel_region_ttm_node_free(obj->mm.region, obj->mm.st_mm_node);
	obj->mm.dirty = false;
	sg_free_table(pages);
	kfree(pages);
}

static int lmem_get_pages(struct drm_i915_gem_object *obj)
{
	unsigned int flags;
	struct sg_table *pages;

	flags = I915_ALLOC_MIN_PAGE_SIZE;
	if (obj->flags & I915_BO_ALLOC_CONTIGUOUS)
		flags |= I915_ALLOC_CONTIGUOUS;

	obj->mm.st_mm_node = intel_region_ttm_node_alloc(obj->mm.region,
							 obj->base.size,
							 flags);
	if (IS_ERR(obj->mm.st_mm_node))
		return PTR_ERR(obj->mm.st_mm_node);

	/* Range manager is always contigous */
	if (obj->mm.region->is_range_manager)
		obj->flags |= I915_BO_ALLOC_CONTIGUOUS;
	pages = intel_region_ttm_node_to_st(obj->mm.region, obj->mm.st_mm_node);
	if (IS_ERR(pages)) {
		intel_region_ttm_node_free(obj->mm.region, obj->mm.st_mm_node);
		return PTR_ERR(pages);
	}

	__i915_gem_object_set_pages(obj, pages, i915_sg_dma_sizes(pages->sgl));

	if (obj->flags & I915_BO_ALLOC_CPU_CLEAR) {
		void __iomem *vaddr =
			i915_gem_object_lmem_io_map(obj, 0, obj->base.size);

		if (!vaddr) {
			struct sg_table *pages =
				__i915_gem_object_unset_pages(obj);

			if (!IS_ERR_OR_NULL(pages))
				lmem_put_pages(obj, pages);
		}

		memset_io(vaddr, 0, obj->base.size);
		io_mapping_unmap(vaddr);
	}

	return 0;
}

const struct drm_i915_gem_object_ops i915_gem_lmem_obj_ops = {
	.name = "i915_gem_object_lmem",
	.flags = I915_GEM_OBJECT_HAS_IOMEM,

	.get_pages = lmem_get_pages,
	.put_pages = lmem_put_pages,
	.release = i915_gem_object_release_memory_region,
};

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
	struct intel_memory_region *mr = obj->mm.region;

	return mr && (mr->type == INTEL_MEMORY_LOCAL ||
		      mr->type == INTEL_MEMORY_STOLEN_LOCAL);
}

struct drm_i915_gem_object *
i915_gem_object_create_lmem(struct drm_i915_private *i915,
			    resource_size_t size,
			    unsigned int flags)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_LMEM],
					     size, flags);
}

int __i915_gem_lmem_object_init(struct intel_memory_region *mem,
				struct drm_i915_gem_object *obj,
				resource_size_t size,
				unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &i915_gem_lmem_obj_ops, &lock_class, flags);

	obj->read_domains = I915_GEM_DOMAIN_WC | I915_GEM_DOMAIN_GTT;

	i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);

	i915_gem_object_init_memory_region(obj, mem);

	return 0;
}

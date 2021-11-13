// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_memory_region.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_lmem.h"
#include "i915_drv.h"

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

/**
 * i915_gem_object_is_lmem - Whether the object is resident in
 * lmem
 * @obj: The object to check.
 *
 * Even if an object is allowed to migrate and change memory region,
 * this function checks whether it will always be present in lmem when
 * valid *or* if that's not the case, whether it's currently resident in lmem.
 * For migratable and evictable objects, the latter only makes sense when
 * the object is locked.
 *
 * Return: Whether the object migratable but resident in lmem, or not
 * migratable and will be present in lmem when valid.
 */
bool i915_gem_object_is_lmem(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mr = READ_ONCE(obj->mm.region);

#ifdef CONFIG_LOCKDEP
	if (i915_gem_object_migratable(obj) &&
	    i915_gem_object_evictable(obj))
		assert_object_held(obj);
#endif
	return mr && (mr->type == INTEL_MEMORY_LOCAL ||
		      mr->type == INTEL_MEMORY_STOLEN_LOCAL);
}

/**
 * __i915_gem_object_is_lmem - Whether the object is resident in
 * lmem while in the fence signaling critical path.
 * @obj: The object to check.
 *
 * This function is intended to be called from within the fence signaling
 * path where the fence, or a pin, keeps the object from being migrated. For
 * example during gpu reset or similar.
 *
 * Return: Whether the object is resident in lmem.
 */
bool __i915_gem_object_is_lmem(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mr = READ_ONCE(obj->mm.region);

#ifdef CONFIG_LOCKDEP
	GEM_WARN_ON(dma_resv_test_signaled(obj->base.resv, true) &&
		    i915_gem_object_evictable(obj));
#endif
	return mr && (mr->type == INTEL_MEMORY_LOCAL ||
		      mr->type == INTEL_MEMORY_STOLEN_LOCAL);
}

/**
 * __i915_gem_object_create_lmem_with_ps - Create lmem object and force the
 * minimum page size for the backing pages.
 * @i915: The i915 instance.
 * @size: The size in bytes for the object. Note that we need to round the size
 * up depending on the @page_size. The final object size can be fished out from
 * the drm GEM object.
 * @page_size: The requested minimum page size in bytes for this object. This is
 * useful if we need something bigger than the regions min_page_size due to some
 * hw restriction, or in some very specialised cases where it needs to be
 * smaller, where the internal fragmentation cost is too great when rounding up
 * the object size.
 * @flags: The optional BO allocation flags.
 *
 * Note that this interface assumes you know what you are doing when forcing the
 * @page_size. If this is smaller than the regions min_page_size then it can
 * never be inserted into any GTT, otherwise it might lead to undefined
 * behaviour.
 *
 * Return: The object pointer, which might be an ERR_PTR in the case of failure.
 */
struct drm_i915_gem_object *
__i915_gem_object_create_lmem_with_ps(struct drm_i915_private *i915,
				      resource_size_t size,
				      resource_size_t page_size,
				      unsigned int flags)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_LMEM],
					     size, page_size, flags);
}

struct drm_i915_gem_object *
i915_gem_object_create_lmem_from_data(struct drm_i915_private *i915,
				      const void *data, size_t size)
{
	struct drm_i915_gem_object *obj;
	void *map;

	obj = i915_gem_object_create_lmem(i915,
					  round_up(size, PAGE_SIZE),
					  I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj))
		return obj;

	map = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WC);
	if (IS_ERR(map)) {
		i915_gem_object_put(obj);
		return map;
	}

	memcpy(map, data, size);

	i915_gem_object_unpin_map(obj);

	return obj;
}

struct drm_i915_gem_object *
i915_gem_object_create_lmem(struct drm_i915_private *i915,
			    resource_size_t size,
			    unsigned int flags)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_LMEM],
					     size, 0, flags);
}

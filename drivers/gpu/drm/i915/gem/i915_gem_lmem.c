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
 * i915_gem_object_validates_to_lmem - Whether the object is resident in
 * lmem when pages are present.
 * @obj: The object to check.
 *
 * Migratable objects residency may change from under us if the object is
 * not pinned or locked. This function is intended to be used to check whether
 * the object can only reside in lmem when pages are present.
 *
 * Return: Whether the object is always resident in lmem when pages are
 * present.
 */
bool i915_gem_object_validates_to_lmem(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mr = READ_ONCE(obj->mm.region);

	return !i915_gem_object_migratable(obj) &&
		mr && (mr->type == INTEL_MEMORY_LOCAL ||
		       mr->type == INTEL_MEMORY_STOLEN_LOCAL);
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

struct drm_i915_gem_object *
i915_gem_object_create_lmem(struct drm_i915_private *i915,
			    resource_size_t size,
			    unsigned int flags)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_LMEM],
					     size, flags);
}

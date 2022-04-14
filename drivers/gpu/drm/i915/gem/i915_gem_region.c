// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <uapi/drm/i915_drm.h>

#include "intel_memory_region.h"
#include "i915_gem_region.h"
#include "i915_drv.h"
#include "i915_trace.h"

void i915_gem_object_init_memory_region(struct drm_i915_gem_object *obj,
					struct intel_memory_region *mem)
{
	obj->mm.region = mem;

	mutex_lock(&mem->objects.lock);
	list_add(&obj->mm.region_link, &mem->objects.list);
	mutex_unlock(&mem->objects.lock);
}

void i915_gem_object_release_memory_region(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mem = obj->mm.region;

	mutex_lock(&mem->objects.lock);
	list_del(&obj->mm.region_link);
	mutex_unlock(&mem->objects.lock);
}

struct drm_i915_gem_object *
i915_gem_object_create_region(struct intel_memory_region *mem,
			      resource_size_t size,
			      resource_size_t page_size,
			      unsigned int flags)
{
	struct drm_i915_gem_object *obj;
	resource_size_t default_page_size;
	int err;

	/*
	 * NB: Our use of resource_size_t for the size stems from using struct
	 * resource for the mem->region. We might need to revisit this in the
	 * future.
	 */

	GEM_BUG_ON(flags & ~I915_BO_ALLOC_FLAGS);

	if (WARN_ON_ONCE(flags & I915_BO_ALLOC_GPU_ONLY &&
			 (flags & I915_BO_ALLOC_CPU_CLEAR ||
			  flags & I915_BO_ALLOC_PM_EARLY)))
		return ERR_PTR(-EINVAL);

	if (!mem)
		return ERR_PTR(-ENODEV);

	default_page_size = mem->min_page_size;
	if (page_size)
		default_page_size = page_size;

	GEM_BUG_ON(!is_power_of_2_u64(default_page_size));
	GEM_BUG_ON(default_page_size < PAGE_SIZE);

	size = round_up(size, default_page_size);

	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_MIN_ALIGNMENT));

	if (i915_gem_object_size_2big(size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc();
	if (!obj)
		return ERR_PTR(-ENOMEM);

	/*
	 * Anything smaller than the min_page_size can't be freely inserted into
	 * the GTT, due to alignemnt restrictions. For such special objects,
	 * make sure we force memcpy based suspend-resume. In the future we can
	 * revisit this, either by allowing special mis-aligned objects in the
	 * migration path, or by mapping all of LMEM upfront using cheap 1G
	 * GTT entries.
	 */
	if (default_page_size < mem->min_page_size)
		flags |= I915_BO_ALLOC_PM_EARLY;

	err = mem->ops->init_object(mem, obj, size, page_size, flags);
	if (err)
		goto err_object_free;

	trace_i915_gem_object_create(obj);
	return obj;

err_object_free:
	i915_gem_object_free(obj);
	return ERR_PTR(err);
}

/**
 * i915_gem_process_region - Iterate over all objects of a region using ops
 * to process and optionally skip objects
 * @mr: The memory region
 * @apply: ops and private data
 *
 * This function can be used to iterate over the regions object list,
 * checking whether to skip objects, and, if not, lock the objects and
 * process them using the supplied ops. Note that this function temporarily
 * removes objects from the region list while iterating, so that if run
 * concurrently with itself may not iterate over all objects.
 *
 * Return: 0 if successful, negative error code on failure.
 */
int i915_gem_process_region(struct intel_memory_region *mr,
			    struct i915_gem_apply_to_region *apply)
{
	const struct i915_gem_apply_to_region_ops *ops = apply->ops;
	struct drm_i915_gem_object *obj;
	struct list_head still_in_list;
	int ret = 0;

	/*
	 * In the future, a non-NULL apply->ww could mean the caller is
	 * already in a locking transaction and provides its own context.
	 */
	GEM_WARN_ON(apply->ww);

	INIT_LIST_HEAD(&still_in_list);
	mutex_lock(&mr->objects.lock);
	for (;;) {
		struct i915_gem_ww_ctx ww;

		obj = list_first_entry_or_null(&mr->objects.list, typeof(*obj),
					       mm.region_link);
		if (!obj)
			break;

		list_move_tail(&obj->mm.region_link, &still_in_list);
		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		/*
		 * Note: Someone else might be migrating the object at this
		 * point. The object's region is not stable until we lock
		 * the object.
		 */
		mutex_unlock(&mr->objects.lock);
		apply->ww = &ww;
		for_i915_gem_ww(&ww, ret, apply->interruptible) {
			ret = i915_gem_object_lock(obj, apply->ww);
			if (ret)
				continue;

			if (obj->mm.region == mr)
				ret = ops->process_obj(apply, obj);
			/* Implicit object unlock */
		}

		i915_gem_object_put(obj);
		mutex_lock(&mr->objects.lock);
		if (ret)
			break;
	}
	list_splice_tail(&still_in_list, &mr->objects.list);
	mutex_unlock(&mr->objects.lock);

	return ret;
}

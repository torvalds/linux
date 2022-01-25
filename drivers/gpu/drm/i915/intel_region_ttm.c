// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_range_manager.h>

#include "i915_drv.h"
#include "i915_scatterlist.h"
#include "i915_ttm_buddy_manager.h"

#include "intel_region_ttm.h"

#include "gem/i915_gem_ttm.h" /* For the funcs/ops export only */
/**
 * DOC: TTM support structure
 *
 * The code in this file deals with setting up memory managers for TTM
 * LMEM and MOCK regions and converting the output from
 * the managers to struct sg_table, Basically providing the mapping from
 * i915 GEM regions to TTM memory types and resource managers.
 */

/**
 * intel_region_ttm_device_init - Initialize a TTM device
 * @dev_priv: Pointer to an i915 device private structure.
 *
 * Return: 0 on success, negative error code on failure.
 */
int intel_region_ttm_device_init(struct drm_i915_private *dev_priv)
{
	struct drm_device *drm = &dev_priv->drm;

	return ttm_device_init(&dev_priv->bdev, i915_ttm_driver(),
			       drm->dev, drm->anon_inode->i_mapping,
			       drm->vma_offset_manager, false, false);
}

/**
 * intel_region_ttm_device_fini - Finalize a TTM device
 * @dev_priv: Pointer to an i915 device private structure.
 */
void intel_region_ttm_device_fini(struct drm_i915_private *dev_priv)
{
	ttm_device_fini(&dev_priv->bdev);
}

/*
 * Map the i915 memory regions to TTM memory types. We use the
 * driver-private types for now, reserving TTM_PL_VRAM for stolen
 * memory and TTM_PL_TT for GGTT use if decided to implement this.
 */
int intel_region_to_ttm_type(const struct intel_memory_region *mem)
{
	int type;

	GEM_BUG_ON(mem->type != INTEL_MEMORY_LOCAL &&
		   mem->type != INTEL_MEMORY_MOCK &&
		   mem->type != INTEL_MEMORY_SYSTEM);

	if (mem->type == INTEL_MEMORY_SYSTEM)
		return TTM_PL_SYSTEM;

	type = mem->instance + TTM_PL_PRIV;
	GEM_BUG_ON(type >= TTM_NUM_MEM_TYPES);

	return type;
}

/**
 * intel_region_ttm_init - Initialize a memory region for TTM.
 * @mem: The region to initialize.
 *
 * This function initializes a suitable TTM resource manager for the
 * region, and if it's a LMEM region type, attaches it to the TTM
 * device. MOCK regions are NOT attached to the TTM device, since we don't
 * have one for the mock selftests.
 *
 * Return: 0 on success, negative error code on failure.
 */
int intel_region_ttm_init(struct intel_memory_region *mem)
{
	struct ttm_device *bdev = &mem->i915->bdev;
	int mem_type = intel_region_to_ttm_type(mem);
	int ret;

	ret = i915_ttm_buddy_man_init(bdev, mem_type, false,
				      resource_size(&mem->region),
				      mem->min_page_size, PAGE_SIZE);
	if (ret)
		return ret;

	mem->region_private = ttm_manager_type(bdev, mem_type);

	return 0;
}

/**
 * intel_region_ttm_fini - Finalize a TTM region.
 * @mem: The memory region
 *
 * This functions takes down the TTM resource manager associated with the
 * memory region, and if it was registered with the TTM device,
 * removes that registration.
 */
int intel_region_ttm_fini(struct intel_memory_region *mem)
{
	struct ttm_resource_manager *man = mem->region_private;
	int ret = -EBUSY;
	int count;

	/*
	 * Put the region's move fences. This releases requests that
	 * may hold on to contexts and vms that may hold on to buffer
	 * objects placed in this region.
	 */
	if (man)
		ttm_resource_manager_cleanup(man);

	/* Flush objects from region. */
	for (count = 0; count < 10; ++count) {
		i915_gem_flush_free_objects(mem->i915);

		mutex_lock(&mem->objects.lock);
		if (list_empty(&mem->objects.list))
			ret = 0;
		mutex_unlock(&mem->objects.lock);
		if (!ret)
			break;

		msleep(20);
		flush_delayed_work(&mem->i915->bdev.wq);
	}

	/* If we leaked objects, Don't free the region causing use after free */
	if (ret || !man)
		return ret;

	ret = i915_ttm_buddy_man_fini(&mem->i915->bdev,
				      intel_region_to_ttm_type(mem));
	GEM_WARN_ON(ret);
	mem->region_private = NULL;

	return ret;
}

/**
 * intel_region_ttm_resource_to_rsgt -
 * Convert an opaque TTM resource manager resource to a refcounted sg_table.
 * @mem: The memory region.
 * @res: The resource manager resource obtained from the TTM resource manager.
 *
 * The gem backends typically use sg-tables for operations on the underlying
 * io_memory. So provide a way for the backends to translate the
 * nodes they are handed from TTM to sg-tables.
 *
 * Return: A malloced sg_table on success, an error pointer on failure.
 */
struct i915_refct_sgt *
intel_region_ttm_resource_to_rsgt(struct intel_memory_region *mem,
				  struct ttm_resource *res)
{
	if (mem->is_range_manager) {
		struct ttm_range_mgr_node *range_node =
			to_ttm_range_mgr_node(res);

		return i915_rsgt_from_mm_node(&range_node->mm_nodes[0],
					      mem->region.start);
	} else {
		return i915_rsgt_from_buddy_resource(res, mem->region.start);
	}
}

#ifdef CONFIG_DRM_I915_SELFTEST
/**
 * intel_region_ttm_resource_alloc - Allocate memory resources from a region
 * @mem: The memory region,
 * @size: The requested size in bytes
 * @flags: Allocation flags
 *
 * This functionality is provided only for callers that need to allocate
 * memory from standalone TTM range managers, without the TTM eviction
 * functionality. Don't use if you are not completely sure that's the
 * case. The returned opaque node can be converted to an sg_table using
 * intel_region_ttm_resource_to_st(), and can be freed using
 * intel_region_ttm_resource_free().
 *
 * Return: A valid pointer on success, an error pointer on failure.
 */
struct ttm_resource *
intel_region_ttm_resource_alloc(struct intel_memory_region *mem,
				resource_size_t size,
				unsigned int flags)
{
	struct ttm_resource_manager *man = mem->region_private;
	struct ttm_place place = {};
	struct ttm_buffer_object mock_bo = {};
	struct ttm_resource *res;
	int ret;

	mock_bo.base.size = size;
	place.flags = flags;

	ret = man->func->alloc(man, &mock_bo, &place, &res);
	if (ret == -ENOSPC)
		ret = -ENXIO;
	return ret ? ERR_PTR(ret) : res;
}

#endif

/**
 * intel_region_ttm_resource_free - Free a resource allocated from a resource manager
 * @mem: The region the resource was allocated from.
 * @res: The opaque resource representing an allocation.
 */
void intel_region_ttm_resource_free(struct intel_memory_region *mem,
				    struct ttm_resource *res)
{
	struct ttm_resource_manager *man = mem->region_private;

	man->func->free(man, res);
}

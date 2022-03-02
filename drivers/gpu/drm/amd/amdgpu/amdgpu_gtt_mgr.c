/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#include <drm/ttm/ttm_range_manager.h>

#include "amdgpu.h"

struct amdgpu_gtt_node {
	struct ttm_buffer_object *tbo;
	struct ttm_range_mgr_node base;
};

static inline struct amdgpu_gtt_mgr *
to_gtt_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct amdgpu_gtt_mgr, manager);
}

static inline struct amdgpu_gtt_node *
to_amdgpu_gtt_node(struct ttm_resource *res)
{
	return container_of(res, struct amdgpu_gtt_node, base.base);
}

/**
 * DOC: mem_info_gtt_total
 *
 * The amdgpu driver provides a sysfs API for reporting current total size of
 * the GTT.
 * The file mem_info_gtt_total is used for this, and returns the total size of
 * the GTT block, in bytes
 */
static ssize_t amdgpu_mem_info_gtt_total_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct ttm_resource_manager *man;

	man = ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);
	return sysfs_emit(buf, "%llu\n", man->size);
}

/**
 * DOC: mem_info_gtt_used
 *
 * The amdgpu driver provides a sysfs API for reporting current total amount of
 * used GTT.
 * The file mem_info_gtt_used is used for this, and returns the current used
 * size of the GTT block, in bytes
 */
static ssize_t amdgpu_mem_info_gtt_used_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct ttm_resource_manager *man = &adev->mman.gtt_mgr.manager;

	return sysfs_emit(buf, "%llu\n", ttm_resource_manager_usage(man));
}

static DEVICE_ATTR(mem_info_gtt_total, S_IRUGO,
	           amdgpu_mem_info_gtt_total_show, NULL);
static DEVICE_ATTR(mem_info_gtt_used, S_IRUGO,
	           amdgpu_mem_info_gtt_used_show, NULL);

static struct attribute *amdgpu_gtt_mgr_attributes[] = {
	&dev_attr_mem_info_gtt_total.attr,
	&dev_attr_mem_info_gtt_used.attr,
	NULL
};

const struct attribute_group amdgpu_gtt_mgr_attr_group = {
	.attrs = amdgpu_gtt_mgr_attributes
};

/**
 * amdgpu_gtt_mgr_has_gart_addr - Check if mem has address space
 *
 * @res: the mem object to check
 *
 * Check if a mem object has already address space allocated.
 */
bool amdgpu_gtt_mgr_has_gart_addr(struct ttm_resource *res)
{
	struct amdgpu_gtt_node *node = to_amdgpu_gtt_node(res);

	return drm_mm_node_allocated(&node->base.mm_nodes[0]);
}

/**
 * amdgpu_gtt_mgr_new - allocate a new node
 *
 * @man: TTM memory type manager
 * @tbo: TTM BO we need this range for
 * @place: placement flags and restrictions
 * @res: the resulting mem object
 *
 * Dummy, allocate the node but no space for it yet.
 */
static int amdgpu_gtt_mgr_new(struct ttm_resource_manager *man,
			      struct ttm_buffer_object *tbo,
			      const struct ttm_place *place,
			      struct ttm_resource **res)
{
	struct amdgpu_gtt_mgr *mgr = to_gtt_mgr(man);
	uint32_t num_pages = PFN_UP(tbo->base.size);
	struct amdgpu_gtt_node *node;
	int r;

	node = kzalloc(struct_size(node, base.mm_nodes, 1), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->tbo = tbo;
	ttm_resource_init(tbo, place, &node->base.base);
	if (!(place->flags & TTM_PL_FLAG_TEMPORARY) &&
	    ttm_resource_manager_usage(man) > man->size) {
		r = -ENOSPC;
		goto err_free;
	}

	if (place->lpfn) {
		spin_lock(&mgr->lock);
		r = drm_mm_insert_node_in_range(&mgr->mm,
						&node->base.mm_nodes[0],
						num_pages, tbo->page_alignment,
						0, place->fpfn, place->lpfn,
						DRM_MM_INSERT_BEST);
		spin_unlock(&mgr->lock);
		if (unlikely(r))
			goto err_free;

		node->base.base.start = node->base.mm_nodes[0].start;
	} else {
		node->base.mm_nodes[0].start = 0;
		node->base.mm_nodes[0].size = node->base.base.num_pages;
		node->base.base.start = AMDGPU_BO_INVALID_OFFSET;
	}

	*res = &node->base.base;
	return 0;

err_free:
	ttm_resource_fini(man, &node->base.base);
	kfree(node);
	return r;
}

/**
 * amdgpu_gtt_mgr_del - free ranges
 *
 * @man: TTM memory type manager
 * @res: TTM memory object
 *
 * Free the allocated GTT again.
 */
static void amdgpu_gtt_mgr_del(struct ttm_resource_manager *man,
			       struct ttm_resource *res)
{
	struct amdgpu_gtt_node *node = to_amdgpu_gtt_node(res);
	struct amdgpu_gtt_mgr *mgr = to_gtt_mgr(man);

	spin_lock(&mgr->lock);
	if (drm_mm_node_allocated(&node->base.mm_nodes[0]))
		drm_mm_remove_node(&node->base.mm_nodes[0]);
	spin_unlock(&mgr->lock);

	ttm_resource_fini(man, res);
	kfree(node);
}

/**
 * amdgpu_gtt_mgr_recover - re-init gart
 *
 * @mgr: amdgpu_gtt_mgr pointer
 *
 * Re-init the gart for each known BO in the GTT.
 */
void amdgpu_gtt_mgr_recover(struct amdgpu_gtt_mgr *mgr)
{
	struct amdgpu_gtt_node *node;
	struct drm_mm_node *mm_node;
	struct amdgpu_device *adev;

	adev = container_of(mgr, typeof(*adev), mman.gtt_mgr);
	spin_lock(&mgr->lock);
	drm_mm_for_each_node(mm_node, &mgr->mm) {
		node = container_of(mm_node, typeof(*node), base.mm_nodes[0]);
		amdgpu_ttm_recover_gart(node->tbo);
	}
	spin_unlock(&mgr->lock);

	amdgpu_gart_invalidate_tlb(adev);
}

/**
 * amdgpu_gtt_mgr_debug - dump VRAM table
 *
 * @man: TTM memory type manager
 * @printer: DRM printer to use
 *
 * Dump the table content using printk.
 */
static void amdgpu_gtt_mgr_debug(struct ttm_resource_manager *man,
				 struct drm_printer *printer)
{
	struct amdgpu_gtt_mgr *mgr = to_gtt_mgr(man);

	spin_lock(&mgr->lock);
	drm_mm_print(&mgr->mm, printer);
	spin_unlock(&mgr->lock);
}

static const struct ttm_resource_manager_func amdgpu_gtt_mgr_func = {
	.alloc = amdgpu_gtt_mgr_new,
	.free = amdgpu_gtt_mgr_del,
	.debug = amdgpu_gtt_mgr_debug
};

/**
 * amdgpu_gtt_mgr_init - init GTT manager and DRM MM
 *
 * @adev: amdgpu_device pointer
 * @gtt_size: maximum size of GTT
 *
 * Allocate and initialize the GTT manager.
 */
int amdgpu_gtt_mgr_init(struct amdgpu_device *adev, uint64_t gtt_size)
{
	struct amdgpu_gtt_mgr *mgr = &adev->mman.gtt_mgr;
	struct ttm_resource_manager *man = &mgr->manager;
	uint64_t start, size;

	man->use_tt = true;
	man->func = &amdgpu_gtt_mgr_func;

	ttm_resource_manager_init(man, &adev->mman.bdev, gtt_size);

	start = AMDGPU_GTT_MAX_TRANSFER_SIZE * AMDGPU_GTT_NUM_TRANSFER_WINDOWS;
	size = (adev->gmc.gart_size >> PAGE_SHIFT) - start;
	drm_mm_init(&mgr->mm, start, size);
	spin_lock_init(&mgr->lock);

	ttm_set_driver_manager(&adev->mman.bdev, TTM_PL_TT, &mgr->manager);
	ttm_resource_manager_set_used(man, true);
	return 0;
}

/**
 * amdgpu_gtt_mgr_fini - free and destroy GTT manager
 *
 * @adev: amdgpu_device pointer
 *
 * Destroy and free the GTT manager, returns -EBUSY if ranges are still
 * allocated inside it.
 */
void amdgpu_gtt_mgr_fini(struct amdgpu_device *adev)
{
	struct amdgpu_gtt_mgr *mgr = &adev->mman.gtt_mgr;
	struct ttm_resource_manager *man = &mgr->manager;
	int ret;

	ttm_resource_manager_set_used(man, false);

	ret = ttm_resource_manager_evict_all(&adev->mman.bdev, man);
	if (ret)
		return;

	spin_lock(&mgr->lock);
	drm_mm_takedown(&mgr->mm);
	spin_unlock(&mgr->lock);

	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(&adev->mman.bdev, TTM_PL_TT, NULL);
}

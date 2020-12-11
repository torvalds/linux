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

#include "amdgpu.h"

static inline struct amdgpu_gtt_mgr *to_gtt_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct amdgpu_gtt_mgr, manager);
}

struct amdgpu_gtt_node {
	struct drm_mm_node node;
	struct ttm_buffer_object *tbo;
};

/**
 * DOC: mem_info_gtt_total
 *
 * The amdgpu driver provides a sysfs API for reporting current total size of
 * the GTT.
 * The file mem_info_gtt_total is used for this, and returns the total size of
 * the GTT block, in bytes
 */
static ssize_t amdgpu_mem_info_gtt_total_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct ttm_resource_manager *man = ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);

	return snprintf(buf, PAGE_SIZE, "%llu\n",
			man->size * PAGE_SIZE);
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
		struct device_attribute *attr, char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct ttm_resource_manager *man = ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);

	return snprintf(buf, PAGE_SIZE, "%llu\n",
			amdgpu_gtt_mgr_usage(man));
}

static DEVICE_ATTR(mem_info_gtt_total, S_IRUGO,
	           amdgpu_mem_info_gtt_total_show, NULL);
static DEVICE_ATTR(mem_info_gtt_used, S_IRUGO,
	           amdgpu_mem_info_gtt_used_show, NULL);

static const struct ttm_resource_manager_func amdgpu_gtt_mgr_func;
/**
 * amdgpu_gtt_mgr_init - init GTT manager and DRM MM
 *
 * @man: TTM memory type manager
 * @p_size: maximum size of GTT
 *
 * Allocate and initialize the GTT manager.
 */
int amdgpu_gtt_mgr_init(struct amdgpu_device *adev, uint64_t gtt_size)
{
	struct amdgpu_gtt_mgr *mgr = &adev->mman.gtt_mgr;
	struct ttm_resource_manager *man = &mgr->manager;
	uint64_t start, size;
	int ret;

	man->use_tt = true;
	man->func = &amdgpu_gtt_mgr_func;

	ttm_resource_manager_init(man, gtt_size >> PAGE_SHIFT);

	start = AMDGPU_GTT_MAX_TRANSFER_SIZE * AMDGPU_GTT_NUM_TRANSFER_WINDOWS;
	size = (adev->gmc.gart_size >> PAGE_SHIFT) - start;
	drm_mm_init(&mgr->mm, start, size);
	spin_lock_init(&mgr->lock);
	atomic64_set(&mgr->available, gtt_size >> PAGE_SHIFT);

	ret = device_create_file(adev->dev, &dev_attr_mem_info_gtt_total);
	if (ret) {
		DRM_ERROR("Failed to create device file mem_info_gtt_total\n");
		return ret;
	}
	ret = device_create_file(adev->dev, &dev_attr_mem_info_gtt_used);
	if (ret) {
		DRM_ERROR("Failed to create device file mem_info_gtt_used\n");
		return ret;
	}

	ttm_set_driver_manager(&adev->mman.bdev, TTM_PL_TT, &mgr->manager);
	ttm_resource_manager_set_used(man, true);
	return 0;
}

/**
 * amdgpu_gtt_mgr_fini - free and destroy GTT manager
 *
 * @man: TTM memory type manager
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

	ret = ttm_resource_manager_force_list_clean(&adev->mman.bdev, man);
	if (ret)
		return;

	spin_lock(&mgr->lock);
	drm_mm_takedown(&mgr->mm);
	spin_unlock(&mgr->lock);

	device_remove_file(adev->dev, &dev_attr_mem_info_gtt_total);
	device_remove_file(adev->dev, &dev_attr_mem_info_gtt_used);

	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(&adev->mman.bdev, TTM_PL_TT, NULL);
}

/**
 * amdgpu_gtt_mgr_has_gart_addr - Check if mem has address space
 *
 * @mem: the mem object to check
 *
 * Check if a mem object has already address space allocated.
 */
bool amdgpu_gtt_mgr_has_gart_addr(struct ttm_resource *mem)
{
	return mem->mm_node != NULL;
}

/**
 * amdgpu_gtt_mgr_new - allocate a new node
 *
 * @man: TTM memory type manager
 * @tbo: TTM BO we need this range for
 * @place: placement flags and restrictions
 * @mem: the resulting mem object
 *
 * Dummy, allocate the node but no space for it yet.
 */
static int amdgpu_gtt_mgr_new(struct ttm_resource_manager *man,
			      struct ttm_buffer_object *tbo,
			      const struct ttm_place *place,
			      struct ttm_resource *mem)
{
	struct amdgpu_gtt_mgr *mgr = to_gtt_mgr(man);
	struct amdgpu_gtt_node *node;
	int r;

	spin_lock(&mgr->lock);
	if ((&tbo->mem == mem || tbo->mem.mem_type != TTM_PL_TT) &&
	    atomic64_read(&mgr->available) < mem->num_pages) {
		spin_unlock(&mgr->lock);
		return -ENOSPC;
	}
	atomic64_sub(mem->num_pages, &mgr->available);
	spin_unlock(&mgr->lock);

	if (!place->lpfn) {
		mem->mm_node = NULL;
		mem->start = AMDGPU_BO_INVALID_OFFSET;
		return 0;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		r = -ENOMEM;
		goto err_out;
	}

	node->tbo = tbo;

	spin_lock(&mgr->lock);
	r = drm_mm_insert_node_in_range(&mgr->mm, &node->node, mem->num_pages,
					mem->page_alignment, 0, place->fpfn,
					place->lpfn, DRM_MM_INSERT_BEST);
	spin_unlock(&mgr->lock);

	if (unlikely(r))
		goto err_free;

	mem->mm_node = node;
	mem->start = node->node.start;

	return 0;

err_free:
	kfree(node);

err_out:
	atomic64_add(mem->num_pages, &mgr->available);

	return r;
}

/**
 * amdgpu_gtt_mgr_del - free ranges
 *
 * @man: TTM memory type manager
 * @mem: TTM memory object
 *
 * Free the allocated GTT again.
 */
static void amdgpu_gtt_mgr_del(struct ttm_resource_manager *man,
			       struct ttm_resource *mem)
{
	struct amdgpu_gtt_mgr *mgr = to_gtt_mgr(man);
	struct amdgpu_gtt_node *node = mem->mm_node;

	if (node) {
		spin_lock(&mgr->lock);
		drm_mm_remove_node(&node->node);
		spin_unlock(&mgr->lock);
		kfree(node);
	}

	atomic64_add(mem->num_pages, &mgr->available);
}

/**
 * amdgpu_gtt_mgr_usage - return usage of GTT domain
 *
 * @man: TTM memory type manager
 *
 * Return how many bytes are used in the GTT domain
 */
uint64_t amdgpu_gtt_mgr_usage(struct ttm_resource_manager *man)
{
	struct amdgpu_gtt_mgr *mgr = to_gtt_mgr(man);
	s64 result = man->size - atomic64_read(&mgr->available);

	return (result > 0 ? result : 0) * PAGE_SIZE;
}

int amdgpu_gtt_mgr_recover(struct ttm_resource_manager *man)
{
	struct amdgpu_gtt_mgr *mgr = to_gtt_mgr(man);
	struct amdgpu_gtt_node *node;
	struct drm_mm_node *mm_node;
	int r = 0;

	spin_lock(&mgr->lock);
	drm_mm_for_each_node(mm_node, &mgr->mm) {
		node = container_of(mm_node, struct amdgpu_gtt_node, node);
		r = amdgpu_ttm_recover_gart(node->tbo);
		if (r)
			break;
	}
	spin_unlock(&mgr->lock);

	return r;
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

	drm_printf(printer, "man size:%llu pages, gtt available:%lld pages, usage:%lluMB\n",
		   man->size, (u64)atomic64_read(&mgr->available),
		   amdgpu_gtt_mgr_usage(man) >> 20);
}

static const struct ttm_resource_manager_func amdgpu_gtt_mgr_func = {
	.alloc = amdgpu_gtt_mgr_new,
	.free = amdgpu_gtt_mgr_del,
	.debug = amdgpu_gtt_mgr_debug
};

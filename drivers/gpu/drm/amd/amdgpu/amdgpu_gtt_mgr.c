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

#include <drm/drmP.h>
#include "amdgpu.h"

struct amdgpu_gtt_mgr {
	struct drm_mm mm;
	spinlock_t lock;
	atomic64_t available;
};

struct amdgpu_gtt_node {
	struct drm_mm_node node;
	struct ttm_buffer_object *tbo;
};

/**
 * amdgpu_gtt_mgr_init - init GTT manager and DRM MM
 *
 * @man: TTM memory type manager
 * @p_size: maximum size of GTT
 *
 * Allocate and initialize the GTT manager.
 */
static int amdgpu_gtt_mgr_init(struct ttm_mem_type_manager *man,
			       unsigned long p_size)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(man->bdev);
	struct amdgpu_gtt_mgr *mgr;
	uint64_t start, size;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return -ENOMEM;

	start = AMDGPU_GTT_MAX_TRANSFER_SIZE * AMDGPU_GTT_NUM_TRANSFER_WINDOWS;
	size = (adev->mc.gart_size >> PAGE_SHIFT) - start;
	drm_mm_init(&mgr->mm, start, size);
	spin_lock_init(&mgr->lock);
	atomic64_set(&mgr->available, p_size);
	man->priv = mgr;
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
static int amdgpu_gtt_mgr_fini(struct ttm_mem_type_manager *man)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;
	spin_lock(&mgr->lock);
	drm_mm_takedown(&mgr->mm);
	spin_unlock(&mgr->lock);
	kfree(mgr);
	man->priv = NULL;
	return 0;
}

/**
 * amdgpu_gtt_mgr_has_gart_addr - Check if mem has address space
 *
 * @mem: the mem object to check
 *
 * Check if a mem object has already address space allocated.
 */
bool amdgpu_gtt_mgr_has_gart_addr(struct ttm_mem_reg *mem)
{
	struct amdgpu_gtt_node *node = mem->mm_node;

	return (node->node.start != AMDGPU_BO_INVALID_OFFSET);
}

/**
 * amdgpu_gtt_mgr_alloc - allocate new ranges
 *
 * @man: TTM memory type manager
 * @tbo: TTM BO we need this range for
 * @place: placement flags and restrictions
 * @mem: the resulting mem object
 *
 * Allocate the address space for a node.
 */
static int amdgpu_gtt_mgr_alloc(struct ttm_mem_type_manager *man,
				struct ttm_buffer_object *tbo,
				const struct ttm_place *place,
				struct ttm_mem_reg *mem)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(man->bdev);
	struct amdgpu_gtt_mgr *mgr = man->priv;
	struct amdgpu_gtt_node *node = mem->mm_node;
	enum drm_mm_insert_mode mode;
	unsigned long fpfn, lpfn;
	int r;

	if (amdgpu_gtt_mgr_has_gart_addr(mem))
		return 0;

	if (place)
		fpfn = place->fpfn;
	else
		fpfn = 0;

	if (place && place->lpfn)
		lpfn = place->lpfn;
	else
		lpfn = adev->gart.num_cpu_pages;

	mode = DRM_MM_INSERT_BEST;
	if (place && place->flags & TTM_PL_FLAG_TOPDOWN)
		mode = DRM_MM_INSERT_HIGH;

	spin_lock(&mgr->lock);
	r = drm_mm_insert_node_in_range(&mgr->mm, &node->node, mem->num_pages,
					mem->page_alignment, 0, fpfn, lpfn,
					mode);
	spin_unlock(&mgr->lock);

	if (!r)
		mem->start = node->node.start;

	return r;
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
static int amdgpu_gtt_mgr_new(struct ttm_mem_type_manager *man,
			      struct ttm_buffer_object *tbo,
			      const struct ttm_place *place,
			      struct ttm_mem_reg *mem)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;
	struct amdgpu_gtt_node *node;
	int r;

	spin_lock(&mgr->lock);
	if ((&tbo->mem == mem || tbo->mem.mem_type != TTM_PL_TT) &&
	    atomic64_read(&mgr->available) < mem->num_pages) {
		spin_unlock(&mgr->lock);
		return 0;
	}
	atomic64_sub(mem->num_pages, &mgr->available);
	spin_unlock(&mgr->lock);

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		r = -ENOMEM;
		goto err_out;
	}

	node->node.start = AMDGPU_BO_INVALID_OFFSET;
	node->node.size = mem->num_pages;
	node->tbo = tbo;
	mem->mm_node = node;

	if (place->fpfn || place->lpfn || place->flags & TTM_PL_FLAG_TOPDOWN) {
		r = amdgpu_gtt_mgr_alloc(man, tbo, place, mem);
		if (unlikely(r)) {
			kfree(node);
			mem->mm_node = NULL;
			r = 0;
			goto err_out;
		}
	} else {
		mem->start = node->node.start;
	}

	return 0;
err_out:
	atomic64_add(mem->num_pages, &mgr->available);

	return r;
}

/**
 * amdgpu_gtt_mgr_del - free ranges
 *
 * @man: TTM memory type manager
 * @tbo: TTM BO we need this range for
 * @place: placement flags and restrictions
 * @mem: TTM memory object
 *
 * Free the allocated GTT again.
 */
static void amdgpu_gtt_mgr_del(struct ttm_mem_type_manager *man,
			       struct ttm_mem_reg *mem)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;
	struct amdgpu_gtt_node *node = mem->mm_node;

	if (!node)
		return;

	spin_lock(&mgr->lock);
	if (node->node.start != AMDGPU_BO_INVALID_OFFSET)
		drm_mm_remove_node(&node->node);
	spin_unlock(&mgr->lock);
	atomic64_add(mem->num_pages, &mgr->available);

	kfree(node);
	mem->mm_node = NULL;
}

/**
 * amdgpu_gtt_mgr_usage - return usage of GTT domain
 *
 * @man: TTM memory type manager
 *
 * Return how many bytes are used in the GTT domain
 */
uint64_t amdgpu_gtt_mgr_usage(struct ttm_mem_type_manager *man)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;
	s64 result = man->size - atomic64_read(&mgr->available);

	return (result > 0 ? result : 0) * PAGE_SIZE;
}

int amdgpu_gtt_mgr_recover(struct ttm_mem_type_manager *man)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;
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
static void amdgpu_gtt_mgr_debug(struct ttm_mem_type_manager *man,
				 struct drm_printer *printer)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;

	spin_lock(&mgr->lock);
	drm_mm_print(&mgr->mm, printer);
	spin_unlock(&mgr->lock);

	drm_printf(printer, "man size:%llu pages, gtt available:%lld pages, usage:%lluMB\n",
		   man->size, (u64)atomic64_read(&mgr->available),
		   amdgpu_gtt_mgr_usage(man) >> 20);
}

const struct ttm_mem_type_manager_func amdgpu_gtt_mgr_func = {
	.init = amdgpu_gtt_mgr_init,
	.takedown = amdgpu_gtt_mgr_fini,
	.get_node = amdgpu_gtt_mgr_new,
	.put_node = amdgpu_gtt_mgr_del,
	.debug = amdgpu_gtt_mgr_debug
};

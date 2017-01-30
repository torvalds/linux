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
	uint64_t available;
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
	struct amdgpu_gtt_mgr *mgr;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return -ENOMEM;

	drm_mm_init(&mgr->mm, 0, p_size);
	spin_lock_init(&mgr->lock);
	mgr->available = p_size;
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
	if (!drm_mm_clean(&mgr->mm)) {
		spin_unlock(&mgr->lock);
		return -EBUSY;
	}

	drm_mm_takedown(&mgr->mm);
	spin_unlock(&mgr->lock);
	kfree(mgr);
	man->priv = NULL;
	return 0;
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
int amdgpu_gtt_mgr_alloc(struct ttm_mem_type_manager *man,
			 struct ttm_buffer_object *tbo,
			 const struct ttm_place *place,
			 struct ttm_mem_reg *mem)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;
	struct drm_mm_node *node = mem->mm_node;
	enum drm_mm_search_flags sflags = DRM_MM_SEARCH_BEST;
	enum drm_mm_allocator_flags aflags = DRM_MM_CREATE_DEFAULT;
	unsigned long fpfn, lpfn;
	int r;

	if (node->start != AMDGPU_BO_INVALID_OFFSET)
		return 0;

	if (place)
		fpfn = place->fpfn;
	else
		fpfn = 0;

	if (place && place->lpfn)
		lpfn = place->lpfn;
	else
		lpfn = man->size;

	if (place && place->flags & TTM_PL_FLAG_TOPDOWN) {
		sflags = DRM_MM_SEARCH_BELOW;
		aflags = DRM_MM_CREATE_TOP;
	}

	spin_lock(&mgr->lock);
	r = drm_mm_insert_node_in_range_generic(&mgr->mm, node, mem->num_pages,
						mem->page_alignment, 0,
						fpfn, lpfn, sflags, aflags);
	spin_unlock(&mgr->lock);

	if (!r) {
		mem->start = node->start;
		if (&tbo->mem == mem)
			tbo->offset = (tbo->mem.start << PAGE_SHIFT) +
			    tbo->bdev->man[tbo->mem.mem_type].gpu_offset;
	}

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
	struct drm_mm_node *node;
	int r;

	spin_lock(&mgr->lock);
	if (mgr->available < mem->num_pages) {
		spin_unlock(&mgr->lock);
		return 0;
	}
	mgr->available -= mem->num_pages;
	spin_unlock(&mgr->lock);

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		r = -ENOMEM;
		goto err_out;
	}

	node->start = AMDGPU_BO_INVALID_OFFSET;
	node->size = mem->num_pages;
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
		mem->start = node->start;
	}

	return 0;
err_out:
	spin_lock(&mgr->lock);
	mgr->available += mem->num_pages;
	spin_unlock(&mgr->lock);

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
	struct drm_mm_node *node = mem->mm_node;

	if (!node)
		return;

	spin_lock(&mgr->lock);
	if (node->start != AMDGPU_BO_INVALID_OFFSET)
		drm_mm_remove_node(node);
	mgr->available += mem->num_pages;
	spin_unlock(&mgr->lock);

	kfree(node);
	mem->mm_node = NULL;
}

/**
 * amdgpu_gtt_mgr_debug - dump VRAM table
 *
 * @man: TTM memory type manager
 * @prefix: text prefix
 *
 * Dump the table content using printk.
 */
static void amdgpu_gtt_mgr_debug(struct ttm_mem_type_manager *man,
				  const char *prefix)
{
	struct amdgpu_gtt_mgr *mgr = man->priv;

	spin_lock(&mgr->lock);
	drm_mm_debug_table(&mgr->mm, prefix);
	spin_unlock(&mgr->lock);
}

const struct ttm_mem_type_manager_func amdgpu_gtt_mgr_func = {
	amdgpu_gtt_mgr_init,
	amdgpu_gtt_mgr_fini,
	amdgpu_gtt_mgr_new,
	amdgpu_gtt_mgr_del,
	amdgpu_gtt_mgr_debug
};

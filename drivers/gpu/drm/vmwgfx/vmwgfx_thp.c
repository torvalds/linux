// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Huge page-table-entry support for IO memory.
 *
 * Copyright (C) 2007-2019 Vmware, Inc. All rights reservedd.
 */
#include "vmwgfx_drv.h"
#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

/**
 * struct vmw_thp_manager - Range manager implementing huge page alignment
 *
 * @mm: The underlying range manager. Protected by @lock.
 * @lock: Manager lock.
 */
struct vmw_thp_manager {
	struct drm_mm mm;
	spinlock_t lock;
};

static int vmw_thp_insert_aligned(struct drm_mm *mm, struct drm_mm_node *node,
				  unsigned long align_pages,
				  const struct ttm_place *place,
				  struct ttm_mem_reg *mem,
				  unsigned long lpfn,
				  enum drm_mm_insert_mode mode)
{
	if (align_pages >= mem->page_alignment &&
	    (!mem->page_alignment || align_pages % mem->page_alignment == 0)) {
		return drm_mm_insert_node_in_range(mm, node,
						   mem->num_pages,
						   align_pages, 0,
						   place->fpfn, lpfn, mode);
	}

	return -ENOSPC;
}

static int vmw_thp_get_node(struct ttm_mem_type_manager *man,
			    struct ttm_buffer_object *bo,
			    const struct ttm_place *place,
			    struct ttm_mem_reg *mem)
{
	struct vmw_thp_manager *rman = (struct vmw_thp_manager *) man->priv;
	struct drm_mm *mm = &rman->mm;
	struct drm_mm_node *node;
	unsigned long align_pages;
	unsigned long lpfn;
	enum drm_mm_insert_mode mode = DRM_MM_INSERT_BEST;
	int ret;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	lpfn = place->lpfn;
	if (!lpfn)
		lpfn = man->size;

	mode = DRM_MM_INSERT_BEST;
	if (place->flags & TTM_PL_FLAG_TOPDOWN)
		mode = DRM_MM_INSERT_HIGH;

	spin_lock(&rman->lock);
	if (IS_ENABLED(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)) {
		align_pages = (HPAGE_PUD_SIZE >> PAGE_SHIFT);
		if (mem->num_pages >= align_pages) {
			ret = vmw_thp_insert_aligned(mm, node, align_pages,
						     place, mem, lpfn, mode);
			if (!ret)
				goto found_unlock;
		}
	}

	align_pages = (HPAGE_PMD_SIZE >> PAGE_SHIFT);
	if (mem->num_pages >= align_pages) {
		ret = vmw_thp_insert_aligned(mm, node, align_pages, place, mem,
					     lpfn, mode);
		if (!ret)
			goto found_unlock;
	}

	ret = drm_mm_insert_node_in_range(mm, node, mem->num_pages,
					  mem->page_alignment, 0,
					  place->fpfn, lpfn, mode);
found_unlock:
	spin_unlock(&rman->lock);

	if (unlikely(ret)) {
		kfree(node);
	} else {
		mem->mm_node = node;
		mem->start = node->start;
	}

	return 0;
}



static void vmw_thp_put_node(struct ttm_mem_type_manager *man,
			     struct ttm_mem_reg *mem)
{
	struct vmw_thp_manager *rman = (struct vmw_thp_manager *) man->priv;

	if (mem->mm_node) {
		spin_lock(&rman->lock);
		drm_mm_remove_node(mem->mm_node);
		spin_unlock(&rman->lock);

		kfree(mem->mm_node);
		mem->mm_node = NULL;
	}
}

static int vmw_thp_init(struct ttm_mem_type_manager *man,
			unsigned long p_size)
{
	struct vmw_thp_manager *rman;

	rman = kzalloc(sizeof(*rman), GFP_KERNEL);
	if (!rman)
		return -ENOMEM;

	drm_mm_init(&rman->mm, 0, p_size);
	spin_lock_init(&rman->lock);
	man->priv = rman;
	return 0;
}

static int vmw_thp_takedown(struct ttm_mem_type_manager *man)
{
	struct vmw_thp_manager *rman = (struct vmw_thp_manager *) man->priv;
	struct drm_mm *mm = &rman->mm;

	spin_lock(&rman->lock);
	if (drm_mm_clean(mm)) {
		drm_mm_takedown(mm);
		spin_unlock(&rman->lock);
		kfree(rman);
		man->priv = NULL;
		return 0;
	}
	spin_unlock(&rman->lock);
	return -EBUSY;
}

static void vmw_thp_debug(struct ttm_mem_type_manager *man,
			  struct drm_printer *printer)
{
	struct vmw_thp_manager *rman = (struct vmw_thp_manager *) man->priv;

	spin_lock(&rman->lock);
	drm_mm_print(&rman->mm, printer);
	spin_unlock(&rman->lock);
}

const struct ttm_mem_type_manager_func vmw_thp_func = {
	.init = vmw_thp_init,
	.takedown = vmw_thp_takedown,
	.get_node = vmw_thp_get_node,
	.put_node = vmw_thp_put_node,
	.debug = vmw_thp_debug
};

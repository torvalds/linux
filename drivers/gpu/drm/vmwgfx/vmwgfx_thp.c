// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Huge page-table-entry support for IO memory.
 *
 * Copyright (C) 2007-2019 Vmware, Inc. All rights reservedd.
 */
#include "vmwgfx_drv.h"
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

/**
 * struct vmw_thp_manager - Range manager implementing huge page alignment
 *
 * @manager: TTM resource manager.
 * @mm: The underlying range manager. Protected by @lock.
 * @lock: Manager lock.
 */
struct vmw_thp_manager {
	struct ttm_resource_manager manager;
	struct drm_mm mm;
	spinlock_t lock;
};

static struct vmw_thp_manager *to_thp_manager(struct ttm_resource_manager *man)
{
	return container_of(man, struct vmw_thp_manager, manager);
}

static const struct ttm_resource_manager_func vmw_thp_func;

static int vmw_thp_insert_aligned(struct ttm_buffer_object *bo,
				  struct drm_mm *mm, struct drm_mm_node *node,
				  unsigned long align_pages,
				  const struct ttm_place *place,
				  struct ttm_resource *mem,
				  unsigned long lpfn,
				  enum drm_mm_insert_mode mode)
{
	if (align_pages >= bo->page_alignment &&
	    (!bo->page_alignment || align_pages % bo->page_alignment == 0)) {
		return drm_mm_insert_node_in_range(mm, node,
						   mem->num_pages,
						   align_pages, 0,
						   place->fpfn, lpfn, mode);
	}

	return -ENOSPC;
}

static int vmw_thp_get_node(struct ttm_resource_manager *man,
			    struct ttm_buffer_object *bo,
			    const struct ttm_place *place,
			    struct ttm_resource *mem)
{
	struct vmw_thp_manager *rman = to_thp_manager(man);
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
			ret = vmw_thp_insert_aligned(bo, mm, node, align_pages,
						     place, mem, lpfn, mode);
			if (!ret)
				goto found_unlock;
		}
	}

	align_pages = (HPAGE_PMD_SIZE >> PAGE_SHIFT);
	if (mem->num_pages >= align_pages) {
		ret = vmw_thp_insert_aligned(bo, mm, node, align_pages, place,
					     mem, lpfn, mode);
		if (!ret)
			goto found_unlock;
	}

	ret = drm_mm_insert_node_in_range(mm, node, mem->num_pages,
					  bo->page_alignment, 0,
					  place->fpfn, lpfn, mode);
found_unlock:
	spin_unlock(&rman->lock);

	if (unlikely(ret)) {
		kfree(node);
	} else {
		mem->mm_node = node;
		mem->start = node->start;
	}

	return ret;
}



static void vmw_thp_put_node(struct ttm_resource_manager *man,
			     struct ttm_resource *mem)
{
	struct vmw_thp_manager *rman = to_thp_manager(man);

	if (mem->mm_node) {
		spin_lock(&rman->lock);
		drm_mm_remove_node(mem->mm_node);
		spin_unlock(&rman->lock);

		kfree(mem->mm_node);
		mem->mm_node = NULL;
	}
}

int vmw_thp_init(struct vmw_private *dev_priv)
{
	struct vmw_thp_manager *rman;

	rman = kzalloc(sizeof(*rman), GFP_KERNEL);
	if (!rman)
		return -ENOMEM;

	ttm_resource_manager_init(&rman->manager,
				  dev_priv->vram_size >> PAGE_SHIFT);

	rman->manager.func = &vmw_thp_func;
	drm_mm_init(&rman->mm, 0, rman->manager.size);
	spin_lock_init(&rman->lock);

	ttm_set_driver_manager(&dev_priv->bdev, TTM_PL_VRAM, &rman->manager);
	ttm_resource_manager_set_used(&rman->manager, true);
	return 0;
}

void vmw_thp_fini(struct vmw_private *dev_priv)
{
	struct ttm_resource_manager *man = ttm_manager_type(&dev_priv->bdev, TTM_PL_VRAM);
	struct vmw_thp_manager *rman = to_thp_manager(man);
	struct drm_mm *mm = &rman->mm;
	int ret;

	ttm_resource_manager_set_used(man, false);

	ret = ttm_resource_manager_evict_all(&dev_priv->bdev, man);
	if (ret)
		return;
	spin_lock(&rman->lock);
	drm_mm_clean(mm);
	drm_mm_takedown(mm);
	spin_unlock(&rman->lock);
	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(&dev_priv->bdev, TTM_PL_VRAM, NULL);
	kfree(rman);
}

static void vmw_thp_debug(struct ttm_resource_manager *man,
			  struct drm_printer *printer)
{
	struct vmw_thp_manager *rman = to_thp_manager(man);

	spin_lock(&rman->lock);
	drm_mm_print(&rman->mm, printer);
	spin_unlock(&rman->lock);
}

static const struct ttm_resource_manager_func vmw_thp_func = {
	.alloc = vmw_thp_get_node,
	.free = vmw_thp_put_node,
	.debug = vmw_thp_debug
};

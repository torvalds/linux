/**************************************************************************
 *
 * Copyright (c) 2007-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#include "ttm/ttm_module.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>

static int ttm_bo_man_get_node(struct ttm_mem_type_manager *man,
			       struct ttm_buffer_object *bo,
			       struct ttm_placement *placement,
			       struct ttm_mem_reg *mem)
{
	struct ttm_bo_global *glob = man->bdev->glob;
	struct drm_mm *mm = man->priv;
	struct drm_mm_node *node = NULL;
	unsigned long lpfn;
	int ret;

	lpfn = placement->lpfn;
	if (!lpfn)
		lpfn = man->size;
	do {
		ret = drm_mm_pre_get(mm);
		if (unlikely(ret))
			return ret;

		spin_lock(&glob->lru_lock);
		node = drm_mm_search_free_in_range(mm,
					mem->num_pages, mem->page_alignment,
					placement->fpfn, lpfn, 1);
		if (unlikely(node == NULL)) {
			spin_unlock(&glob->lru_lock);
			return 0;
		}
		node = drm_mm_get_block_atomic_range(node, mem->num_pages,
							mem->page_alignment,
							placement->fpfn,
							lpfn);
		spin_unlock(&glob->lru_lock);
	} while (node == NULL);

	mem->mm_node = node;
	mem->start = node->start;
	return 0;
}

static void ttm_bo_man_put_node(struct ttm_mem_type_manager *man,
				struct ttm_mem_reg *mem)
{
	struct ttm_bo_global *glob = man->bdev->glob;

	if (mem->mm_node) {
		spin_lock(&glob->lru_lock);
		drm_mm_put_block(mem->mm_node);
		spin_unlock(&glob->lru_lock);
		mem->mm_node = NULL;
	}
}

static int ttm_bo_man_init(struct ttm_mem_type_manager *man,
			   unsigned long p_size)
{
	struct drm_mm *mm;
	int ret;

	mm = kzalloc(sizeof(*mm), GFP_KERNEL);
	if (!mm)
		return -ENOMEM;

	ret = drm_mm_init(mm, 0, p_size);
	if (ret) {
		kfree(mm);
		return ret;
	}

	man->priv = mm;
	return 0;
}

static int ttm_bo_man_takedown(struct ttm_mem_type_manager *man)
{
	struct ttm_bo_global *glob = man->bdev->glob;
	struct drm_mm *mm = man->priv;
	int ret = 0;

	spin_lock(&glob->lru_lock);
	if (drm_mm_clean(mm)) {
		drm_mm_takedown(mm);
		kfree(mm);
		man->priv = NULL;
	} else
		ret = -EBUSY;
	spin_unlock(&glob->lru_lock);
	return ret;
}

static void ttm_bo_man_debug(struct ttm_mem_type_manager *man,
			     const char *prefix)
{
	struct ttm_bo_global *glob = man->bdev->glob;
	struct drm_mm *mm = man->priv;

	spin_lock(&glob->lru_lock);
	drm_mm_debug_table(mm, prefix);
	spin_unlock(&glob->lru_lock);
}

const struct ttm_mem_type_manager_func ttm_bo_manager_func = {
	ttm_bo_man_init,
	ttm_bo_man_takedown,
	ttm_bo_man_get_node,
	ttm_bo_man_put_node,
	ttm_bo_man_debug
};
EXPORT_SYMBOL(ttm_bo_manager_func);

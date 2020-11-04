/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2007-2010 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drm_mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>

/**
 * Currently we use a spinlock for the lock, but a mutex *may* be
 * more appropriate to reduce scheduling latency if the range manager
 * ends up with very fragmented allocation patterns.
 */

struct ttm_range_manager {
	struct ttm_resource_manager manager;
	struct drm_mm mm;
	spinlock_t lock;
};

static inline struct ttm_range_manager *to_range_manager(struct ttm_resource_manager *man)
{
	return container_of(man, struct ttm_range_manager, manager);
}

static int ttm_range_man_alloc(struct ttm_resource_manager *man,
			       struct ttm_buffer_object *bo,
			       const struct ttm_place *place,
			       struct ttm_resource *mem)
{
	struct ttm_range_manager *rman = to_range_manager(man);
	struct drm_mm *mm = &rman->mm;
	struct drm_mm_node *node;
	enum drm_mm_insert_mode mode;
	unsigned long lpfn;
	int ret;

	lpfn = place->lpfn;
	if (!lpfn)
		lpfn = man->size;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	mode = DRM_MM_INSERT_BEST;
	if (place->flags & TTM_PL_FLAG_TOPDOWN)
		mode = DRM_MM_INSERT_HIGH;

	spin_lock(&rman->lock);
	ret = drm_mm_insert_node_in_range(mm, node,
					  mem->num_pages,
					  mem->page_alignment, 0,
					  place->fpfn, lpfn, mode);
	spin_unlock(&rman->lock);

	if (unlikely(ret)) {
		kfree(node);
	} else {
		mem->mm_node = node;
		mem->start = node->start;
	}

	return ret;
}

static void ttm_range_man_free(struct ttm_resource_manager *man,
			       struct ttm_resource *mem)
{
	struct ttm_range_manager *rman = to_range_manager(man);

	if (mem->mm_node) {
		spin_lock(&rman->lock);
		drm_mm_remove_node(mem->mm_node);
		spin_unlock(&rman->lock);

		kfree(mem->mm_node);
		mem->mm_node = NULL;
	}
}

static const struct ttm_resource_manager_func ttm_range_manager_func;

int ttm_range_man_init(struct ttm_bo_device *bdev,
		       unsigned type, bool use_tt,
		       unsigned long p_size)
{
	struct ttm_resource_manager *man;
	struct ttm_range_manager *rman;

	rman = kzalloc(sizeof(*rman), GFP_KERNEL);
	if (!rman)
		return -ENOMEM;

	man = &rman->manager;
	man->use_tt = use_tt;

	man->func = &ttm_range_manager_func;

	ttm_resource_manager_init(man, p_size);

	drm_mm_init(&rman->mm, 0, p_size);
	spin_lock_init(&rman->lock);

	ttm_set_driver_manager(bdev, type, &rman->manager);
	ttm_resource_manager_set_used(man, true);
	return 0;
}
EXPORT_SYMBOL(ttm_range_man_init);

int ttm_range_man_fini(struct ttm_bo_device *bdev,
		       unsigned type)
{
	struct ttm_resource_manager *man = ttm_manager_type(bdev, type);
	struct ttm_range_manager *rman = to_range_manager(man);
	struct drm_mm *mm = &rman->mm;
	int ret;

	ttm_resource_manager_set_used(man, false);

	ret = ttm_resource_manager_evict_all(bdev, man);
	if (ret)
		return ret;

	spin_lock(&rman->lock);
	drm_mm_clean(mm);
	drm_mm_takedown(mm);
	spin_unlock(&rman->lock);

	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(bdev, type, NULL);
	kfree(rman);
	return 0;
}
EXPORT_SYMBOL(ttm_range_man_fini);

static void ttm_range_man_debug(struct ttm_resource_manager *man,
				struct drm_printer *printer)
{
	struct ttm_range_manager *rman = to_range_manager(man);

	spin_lock(&rman->lock);
	drm_mm_print(&rman->mm, printer);
	spin_unlock(&rman->lock);
}

static const struct ttm_resource_manager_func ttm_range_manager_func = {
	.alloc = ttm_range_man_alloc,
	.free = ttm_range_man_free,
	.debug = ttm_range_man_debug
};

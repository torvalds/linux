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

#include "vmwgfx_drv.h"
#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <linux/idr.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>

struct vmwgfx_gmrid_man {
	spinlock_t lock;
	struct ida gmr_ida;
	uint32_t max_gmr_ids;
	uint32_t max_gmr_pages;
	uint32_t used_gmr_pages;
};

static int vmw_gmrid_man_get_node(struct ttm_mem_type_manager *man,
				  struct ttm_buffer_object *bo,
				  const struct ttm_place *place,
				  struct ttm_mem_reg *mem)
{
	struct vmwgfx_gmrid_man *gman =
		(struct vmwgfx_gmrid_man *)man->priv;
	int ret = 0;
	int id;

	mem->mm_node = NULL;

	spin_lock(&gman->lock);

	if (gman->max_gmr_pages > 0) {
		gman->used_gmr_pages += bo->num_pages;
		if (unlikely(gman->used_gmr_pages > gman->max_gmr_pages))
			goto out_err_locked;
	}

	do {
		spin_unlock(&gman->lock);
		if (unlikely(ida_pre_get(&gman->gmr_ida, GFP_KERNEL) == 0)) {
			ret = -ENOMEM;
			goto out_err;
		}
		spin_lock(&gman->lock);

		ret = ida_get_new(&gman->gmr_ida, &id);
		if (unlikely(ret == 0 && id >= gman->max_gmr_ids)) {
			ida_remove(&gman->gmr_ida, id);
			ret = 0;
			goto out_err_locked;
		}
	} while (ret == -EAGAIN);

	if (likely(ret == 0)) {
		mem->mm_node = gman;
		mem->start = id;
		mem->num_pages = bo->num_pages;
	} else
		goto out_err_locked;

	spin_unlock(&gman->lock);
	return 0;

out_err:
	spin_lock(&gman->lock);
out_err_locked:
	gman->used_gmr_pages -= bo->num_pages;
	spin_unlock(&gman->lock);
	return ret;
}

static void vmw_gmrid_man_put_node(struct ttm_mem_type_manager *man,
				   struct ttm_mem_reg *mem)
{
	struct vmwgfx_gmrid_man *gman =
		(struct vmwgfx_gmrid_man *)man->priv;

	if (mem->mm_node) {
		spin_lock(&gman->lock);
		ida_remove(&gman->gmr_ida, mem->start);
		gman->used_gmr_pages -= mem->num_pages;
		spin_unlock(&gman->lock);
		mem->mm_node = NULL;
	}
}

static int vmw_gmrid_man_init(struct ttm_mem_type_manager *man,
			      unsigned long p_size)
{
	struct vmw_private *dev_priv =
		container_of(man->bdev, struct vmw_private, bdev);
	struct vmwgfx_gmrid_man *gman =
		kzalloc(sizeof(*gman), GFP_KERNEL);

	if (unlikely(gman == NULL))
		return -ENOMEM;

	spin_lock_init(&gman->lock);
	gman->used_gmr_pages = 0;
	ida_init(&gman->gmr_ida);

	switch (p_size) {
	case VMW_PL_GMR:
		gman->max_gmr_ids = dev_priv->max_gmr_ids;
		gman->max_gmr_pages = dev_priv->max_gmr_pages;
		break;
	case VMW_PL_MOB:
		gman->max_gmr_ids = VMWGFX_NUM_MOB;
		gman->max_gmr_pages = dev_priv->max_mob_pages;
		break;
	default:
		BUG();
	}
	man->priv = (void *) gman;
	return 0;
}

static int vmw_gmrid_man_takedown(struct ttm_mem_type_manager *man)
{
	struct vmwgfx_gmrid_man *gman =
		(struct vmwgfx_gmrid_man *)man->priv;

	if (gman) {
		ida_destroy(&gman->gmr_ida);
		kfree(gman);
	}
	return 0;
}

static void vmw_gmrid_man_debug(struct ttm_mem_type_manager *man,
				const char *prefix)
{
	pr_info("%s: No debug info available for the GMR id manager\n", prefix);
}

const struct ttm_mem_type_manager_func vmw_gmrid_manager_func = {
	.init = vmw_gmrid_man_init,
	.takedown = vmw_gmrid_man_takedown,
	.get_node = vmw_gmrid_man_get_node,
	.put_node = vmw_gmrid_man_put_node,
	.debug = vmw_gmrid_man_debug
};

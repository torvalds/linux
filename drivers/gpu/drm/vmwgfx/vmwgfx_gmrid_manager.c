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
#include "ttm/ttm_module.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include <linux/idr.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>

struct vmwgfx_gmrid_man {
	spinlock_t lock;
	struct ida gmr_ida;
	uint32_t max_gmr_ids;
};

static int vmw_gmrid_man_get_node(struct ttm_mem_type_manager *man,
				  struct ttm_buffer_object *bo,
				  struct ttm_placement *placement,
				  struct ttm_mem_reg *mem)
{
	struct vmwgfx_gmrid_man *gman =
		(struct vmwgfx_gmrid_man *)man->priv;
	int ret;
	int id;

	mem->mm_node = NULL;

	do {
		if (unlikely(ida_pre_get(&gman->gmr_ida, GFP_KERNEL) == 0))
			return -ENOMEM;

		spin_lock(&gman->lock);
		ret = ida_get_new(&gman->gmr_ida, &id);

		if (unlikely(ret == 0 && id >= gman->max_gmr_ids)) {
			ida_remove(&gman->gmr_ida, id);
			spin_unlock(&gman->lock);
			return 0;
		}

		spin_unlock(&gman->lock);

	} while (ret == -EAGAIN);

	if (likely(ret == 0)) {
		mem->mm_node = gman;
		mem->start = id;
	}

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
		spin_unlock(&gman->lock);
		mem->mm_node = NULL;
	}
}

static int vmw_gmrid_man_init(struct ttm_mem_type_manager *man,
			      unsigned long p_size)
{
	struct vmwgfx_gmrid_man *gman =
		kzalloc(sizeof(*gman), GFP_KERNEL);

	if (unlikely(gman == NULL))
		return -ENOMEM;

	spin_lock_init(&gman->lock);
	ida_init(&gman->gmr_ida);
	gman->max_gmr_ids = p_size;
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
	printk(KERN_INFO "%s: No debug info available for the GMR "
	       "id manager.\n", prefix);
}

const struct ttm_mem_type_manager_func vmw_gmrid_manager_func = {
	vmw_gmrid_man_init,
	vmw_gmrid_man_takedown,
	vmw_gmrid_man_get_node,
	vmw_gmrid_man_put_node,
	vmw_gmrid_man_debug
};

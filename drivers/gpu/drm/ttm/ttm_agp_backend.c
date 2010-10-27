/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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
 *          Keith Packard.
 */

#include "ttm/ttm_module.h"
#include "ttm/ttm_bo_driver.h"
#ifdef TTM_HAS_AGP
#include "ttm/ttm_placement.h"
#include <linux/agp_backend.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <asm/agp.h>

struct ttm_agp_backend {
	struct ttm_backend backend;
	struct agp_memory *mem;
	struct agp_bridge_data *bridge;
};

static int ttm_agp_populate(struct ttm_backend *backend,
			    unsigned long num_pages, struct page **pages,
			    struct page *dummy_read_page)
{
	struct ttm_agp_backend *agp_be =
	    container_of(backend, struct ttm_agp_backend, backend);
	struct page **cur_page, **last_page = pages + num_pages;
	struct agp_memory *mem;

	mem = agp_allocate_memory(agp_be->bridge, num_pages, AGP_USER_MEMORY);
	if (unlikely(mem == NULL))
		return -ENOMEM;

	mem->page_count = 0;
	for (cur_page = pages; cur_page < last_page; ++cur_page) {
		struct page *page = *cur_page;
		if (!page)
			page = dummy_read_page;

		mem->pages[mem->page_count++] = page;
	}
	agp_be->mem = mem;
	return 0;
}

static int ttm_agp_bind(struct ttm_backend *backend, struct ttm_mem_reg *bo_mem)
{
	struct ttm_agp_backend *agp_be =
	    container_of(backend, struct ttm_agp_backend, backend);
	struct drm_mm_node *node = bo_mem->mm_node;
	struct agp_memory *mem = agp_be->mem;
	int cached = (bo_mem->placement & TTM_PL_FLAG_CACHED);
	int ret;

	mem->is_flushed = 1;
	mem->type = (cached) ? AGP_USER_CACHED_MEMORY : AGP_USER_MEMORY;

	ret = agp_bind_memory(mem, node->start);
	if (ret)
		printk(KERN_ERR TTM_PFX "AGP Bind memory failed.\n");

	return ret;
}

static int ttm_agp_unbind(struct ttm_backend *backend)
{
	struct ttm_agp_backend *agp_be =
	    container_of(backend, struct ttm_agp_backend, backend);

	if (agp_be->mem->is_bound)
		return agp_unbind_memory(agp_be->mem);
	else
		return 0;
}

static void ttm_agp_clear(struct ttm_backend *backend)
{
	struct ttm_agp_backend *agp_be =
	    container_of(backend, struct ttm_agp_backend, backend);
	struct agp_memory *mem = agp_be->mem;

	if (mem) {
		ttm_agp_unbind(backend);
		agp_free_memory(mem);
	}
	agp_be->mem = NULL;
}

static void ttm_agp_destroy(struct ttm_backend *backend)
{
	struct ttm_agp_backend *agp_be =
	    container_of(backend, struct ttm_agp_backend, backend);

	if (agp_be->mem)
		ttm_agp_clear(backend);
	kfree(agp_be);
}

static struct ttm_backend_func ttm_agp_func = {
	.populate = ttm_agp_populate,
	.clear = ttm_agp_clear,
	.bind = ttm_agp_bind,
	.unbind = ttm_agp_unbind,
	.destroy = ttm_agp_destroy,
};

struct ttm_backend *ttm_agp_backend_init(struct ttm_bo_device *bdev,
					 struct agp_bridge_data *bridge)
{
	struct ttm_agp_backend *agp_be;

	agp_be = kmalloc(sizeof(*agp_be), GFP_KERNEL);
	if (!agp_be)
		return NULL;

	agp_be->mem = NULL;
	agp_be->bridge = bridge;
	agp_be->backend.func = &ttm_agp_func;
	agp_be->backend.bdev = bdev;
	return &agp_be->backend;
}
EXPORT_SYMBOL(ttm_agp_backend_init);

#endif

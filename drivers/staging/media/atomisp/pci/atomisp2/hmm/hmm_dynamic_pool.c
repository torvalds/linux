/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
/*
 * This file contains functions for dynamic memory pool management
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>

#include "asm/cacheflush.h"

#include "atomisp_internal.h"

#include "hmm/hmm_pool.h"

/*
 * dynamic memory pool ops.
 */
static unsigned int get_pages_from_dynamic_pool(void *pool,
					struct hmm_page_object *page_obj,
					unsigned int size, bool cached)
{
	struct hmm_page *hmm_page;
	unsigned long flags;
	unsigned int i = 0;
	struct hmm_dynamic_pool_info *dypool_info = pool;

	if (!dypool_info)
		return 0;

	spin_lock_irqsave(&dypool_info->list_lock, flags);
	if (dypool_info->initialized) {
		while (!list_empty(&dypool_info->pages_list)) {
			hmm_page = list_entry(dypool_info->pages_list.next,
						struct hmm_page, list);

			list_del(&hmm_page->list);
			dypool_info->pgnr--;
			spin_unlock_irqrestore(&dypool_info->list_lock, flags);

			page_obj[i].page = hmm_page->page;
			page_obj[i++].type = HMM_PAGE_TYPE_DYNAMIC;
#ifdef USE_KMEM_CACHE
			kmem_cache_free(dypool_info->pgptr_cache, hmm_page);
#else
			atomisp_kernel_free(hmm_page);
#endif

			if (i == size)
				return i;

			spin_lock_irqsave(&dypool_info->list_lock, flags);
		}
	}
	spin_unlock_irqrestore(&dypool_info->list_lock, flags);

	return i;
}

static void free_pages_to_dynamic_pool(void *pool,
					struct hmm_page_object *page_obj)
{
	struct hmm_page *hmm_page;
	unsigned long flags;
	int ret;
	struct hmm_dynamic_pool_info *dypool_info = pool;

	if (!dypool_info)
		return;

	spin_lock_irqsave(&dypool_info->list_lock, flags);
	if (!dypool_info->initialized) {
		spin_unlock_irqrestore(&dypool_info->list_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&dypool_info->list_lock, flags);

	if (page_obj->type == HMM_PAGE_TYPE_RESERVED)
		return;

	if (dypool_info->pgnr >= dypool_info->pool_size) {
		/* free page directly back to system */
		ret = set_pages_wb(page_obj->page, 1);
		if (ret)
			dev_err(atomisp_dev,
				"set page to WB err ...ret=%d\n", ret);
		/*
		W/A: set_pages_wb seldom return value = -EFAULT
		indicate that address of page is not in valid
		range(0xffff880000000000~0xffffc7ffffffffff)
		then, _free_pages would panic; Do not know why page
		address be valid, it maybe memory corruption by lowmemory
		*/
		if (!ret) {
			__free_pages(page_obj->page, 0);
			hmm_mem_stat.sys_size--;
		}
		return;
	}
#ifdef USE_KMEM_CACHE
	hmm_page = kmem_cache_zalloc(dypool_info->pgptr_cache,
						GFP_KERNEL);
#else
	hmm_page = atomisp_kernel_malloc(sizeof(struct hmm_page));
#endif
	if (!hmm_page) {
		dev_err(atomisp_dev, "out of memory for hmm_page.\n");

		/* free page directly */
		ret = set_pages_wb(page_obj->page, 1);
		if (ret)
			dev_err(atomisp_dev,
				"set page to WB err ...ret=%d\n", ret);
		if (!ret) {
			__free_pages(page_obj->page, 0);
			hmm_mem_stat.sys_size--;
		}
		return;
	}

	hmm_page->page = page_obj->page;

	/*
	 * add to pages_list of pages_pool
	 */
	spin_lock_irqsave(&dypool_info->list_lock, flags);
	list_add_tail(&hmm_page->list, &dypool_info->pages_list);
	dypool_info->pgnr++;
	spin_unlock_irqrestore(&dypool_info->list_lock, flags);
	hmm_mem_stat.dyc_size++;
}

static int hmm_dynamic_pool_init(void **pool, unsigned int pool_size)
{
	struct hmm_dynamic_pool_info *dypool_info;

	if (pool_size == 0)
		return 0;

	dypool_info = atomisp_kernel_malloc(
					sizeof(struct hmm_dynamic_pool_info));
	if (unlikely(!dypool_info)) {
		dev_err(atomisp_dev, "out of memory for repool_info.\n");
		return -ENOMEM;
	}

#ifdef USE_KMEM_CACHE
	dypool_info->pgptr_cache = kmem_cache_create("pgptr_cache",
						sizeof(struct hmm_page), 0,
						SLAB_HWCACHE_ALIGN, NULL);
	if (!dypool_info->pgptr_cache) {
		atomisp_kernel_free(dypool_info);
		return -ENOMEM;
	}
#endif

	INIT_LIST_HEAD(&dypool_info->pages_list);
	spin_lock_init(&dypool_info->list_lock);
	dypool_info->initialized = true;
	dypool_info->pool_size = pool_size;
	dypool_info->pgnr = 0;

	*pool = dypool_info;

	return 0;
}

static void hmm_dynamic_pool_exit(void **pool)
{
	struct hmm_dynamic_pool_info *dypool_info = *pool;
	struct hmm_page *hmm_page;
	unsigned long flags;
	int ret;

	if (!dypool_info)
		return;

	spin_lock_irqsave(&dypool_info->list_lock, flags);
	if (!dypool_info->initialized) {
		spin_unlock_irqrestore(&dypool_info->list_lock, flags);
		return;
	}
	dypool_info->initialized = false;

	while (!list_empty(&dypool_info->pages_list)) {
		hmm_page = list_entry(dypool_info->pages_list.next,
					struct hmm_page, list);

		list_del(&hmm_page->list);
		spin_unlock_irqrestore(&dypool_info->list_lock, flags);

		/* can cause thread sleep, so cannot be put into spin_lock */
		ret = set_pages_wb(hmm_page->page, 1);
		if (ret)
			dev_err(atomisp_dev,
				"set page to WB err...ret=%d\n", ret);
		if (!ret) {
			__free_pages(hmm_page->page, 0);
			hmm_mem_stat.dyc_size--;
			hmm_mem_stat.sys_size--;
		}
#ifdef USE_KMEM_CACHE
		kmem_cache_free(dypool_info->pgptr_cache, hmm_page);
#else
		atomisp_kernel_free(hmm_page);
#endif
		spin_lock_irqsave(&dypool_info->list_lock, flags);
	}

	spin_unlock_irqrestore(&dypool_info->list_lock, flags);

#ifdef USE_KMEM_CACHE
	kmem_cache_destroy(dypool_info->pgptr_cache);
#endif

	atomisp_kernel_free(dypool_info);

	*pool = NULL;
}

static int hmm_dynamic_pool_inited(void *pool)
{
	struct hmm_dynamic_pool_info *dypool_info = pool;

	if (!dypool_info)
		return 0;

	return dypool_info->initialized;
}

struct hmm_pool_ops dynamic_pops = {
	.pool_init		= hmm_dynamic_pool_init,
	.pool_exit		= hmm_dynamic_pool_exit,
	.pool_alloc_pages	= get_pages_from_dynamic_pool,
	.pool_free_pages	= free_pages_to_dynamic_pool,
	.pool_inited		= hmm_dynamic_pool_inited,
};

/*
 * Copyright (C) 2013-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "../platform/rk/custom_log.h"

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include "mali_osk.h"
#include "mali_memory.h"
#include "mali_memory_os_alloc.h"
#include "mali_kernel_linux.h"

/* Minimum size of allocator page pool */
#define MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB * 256)
#define MALI_OS_MEMORY_POOL_TRIM_JIFFIES (10 * CONFIG_HZ) /* Default to 10s */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
static int mali_mem_os_shrink(int nr_to_scan, gfp_t gfp_mask);
#else
static int mali_mem_os_shrink(struct shrinker *shrinker, int nr_to_scan, gfp_t gfp_mask);
#endif
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
static int mali_mem_os_shrink(struct shrinker *shrinker, struct shrink_control *sc);
#else
static unsigned long mali_mem_os_shrink(struct shrinker *shrinker, struct shrink_control *sc);
static unsigned long mali_mem_os_shrink_count(struct shrinker *shrinker, struct shrink_control *sc);
#endif
#endif
static void mali_mem_os_trim_pool(struct work_struct *work);

struct mali_mem_os_allocator mali_mem_os_allocator = {
	.pool_lock = __SPIN_LOCK_UNLOCKED(pool_lock),
	.pool_pages = LIST_HEAD_INIT(mali_mem_os_allocator.pool_pages),
	.pool_count = 0,

	.allocated_pages = ATOMIC_INIT(0),
	.allocation_limit = 0,

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
	.shrinker.shrink = mali_mem_os_shrink,
#else
	.shrinker.count_objects = mali_mem_os_shrink_count,
	.shrinker.scan_objects = mali_mem_os_shrink,
#endif
	.shrinker.seeks = DEFAULT_SEEKS,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
	.timed_shrinker = __DELAYED_WORK_INITIALIZER(mali_mem_os_allocator.timed_shrinker, mali_mem_os_trim_pool, TIMER_DEFERRABLE),
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	.timed_shrinker = __DEFERRED_WORK_INITIALIZER(mali_mem_os_allocator.timed_shrinker, mali_mem_os_trim_pool),
#else
	.timed_shrinker = __DELAYED_WORK_INITIALIZER(mali_mem_os_allocator.timed_shrinker, mali_mem_os_trim_pool),
#endif
};

u32 mali_mem_os_free(struct list_head *os_pages, u32 pages_count, mali_bool cow_flag)
{
	LIST_HEAD(pages);
	struct mali_page_node *m_page, *m_tmp;
	u32 free_pages_nr = 0;

	if (MALI_TRUE == cow_flag) {
		list_for_each_entry_safe(m_page, m_tmp, os_pages, list) {
			/*only handle OS node here */
			if (m_page->type == MALI_PAGE_NODE_OS) {
				if (1 == _mali_page_node_get_ref_count(m_page)) {
					list_move(&m_page->list, &pages);
					atomic_sub(1, &mali_mem_os_allocator.allocated_pages);
					free_pages_nr ++;
				} else {
					_mali_page_node_unref(m_page);
					m_page->page = NULL;
					list_del(&m_page->list);
					kfree(m_page);
				}
			}
		}
	} else {
		list_cut_position(&pages, os_pages, os_pages->prev);
		atomic_sub(pages_count, &mali_mem_os_allocator.allocated_pages);
		free_pages_nr = pages_count;
	}

	/* Put pages on pool. */
	spin_lock(&mali_mem_os_allocator.pool_lock);
	list_splice(&pages, &mali_mem_os_allocator.pool_pages);
	mali_mem_os_allocator.pool_count += free_pages_nr;
	spin_unlock(&mali_mem_os_allocator.pool_lock);

	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES < mali_mem_os_allocator.pool_count) {
		MALI_DEBUG_PRINT(5, ("OS Mem: Starting pool trim timer %u\n", mali_mem_os_allocator.pool_count));
		queue_delayed_work(mali_mem_os_allocator.wq, &mali_mem_os_allocator.timed_shrinker, MALI_OS_MEMORY_POOL_TRIM_JIFFIES);
	}
	return free_pages_nr;
}

/**
* put page without put it into page pool
*/
_mali_osk_errcode_t mali_mem_os_put_page(struct page *page)
{
	MALI_DEBUG_ASSERT_POINTER(page);
	if (1 == page_count(page)) {
		atomic_sub(1, &mali_mem_os_allocator.allocated_pages);
		dma_unmap_page(&mali_platform_device->dev, page_private(page),
			       _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);
		ClearPagePrivate(page);
	}
	put_page(page);
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_mem_os_resize_pages(mali_mem_os_mem *mem_from, mali_mem_os_mem *mem_to, u32 start_page, u32 page_count)
{
	struct mali_page_node *m_page, *m_tmp;
	u32 i = 0;

	MALI_DEBUG_ASSERT_POINTER(mem_from);
	MALI_DEBUG_ASSERT_POINTER(mem_to);

	if (mem_from->count < start_page + page_count) {
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	list_for_each_entry_safe(m_page, m_tmp, &mem_from->pages, list) {
		if (i >= start_page && i < start_page + page_count) {
			list_move_tail(&m_page->list, &mem_to->pages);
			mem_from->count--;
			mem_to->count++;
		}
		i++;
	}

	return _MALI_OSK_ERR_OK;
}


int mali_mem_os_alloc_pages(mali_mem_os_mem *os_mem, u32 size)
{
	struct page *new_page;
	LIST_HEAD(pages_list);
	size_t page_count = PAGE_ALIGN(size) / _MALI_OSK_MALI_PAGE_SIZE;
	size_t remaining = page_count;
	struct mali_page_node *m_page, *m_tmp;
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(os_mem);

	if (atomic_read(&mali_mem_os_allocator.allocated_pages) * _MALI_OSK_MALI_PAGE_SIZE + size > mali_mem_os_allocator.allocation_limit) {
		MALI_DEBUG_PRINT(2, ("Mali Mem: Unable to allocate %u bytes. Currently allocated: %lu, max limit %lu\n",
				     size,
				     atomic_read(&mali_mem_os_allocator.allocated_pages) * _MALI_OSK_MALI_PAGE_SIZE,
				     mali_mem_os_allocator.allocation_limit));
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&os_mem->pages);
	os_mem->count = page_count;

	/* Grab pages from pool. */
	{
		size_t pool_pages;
		spin_lock(&mali_mem_os_allocator.pool_lock);
		pool_pages = min(remaining, mali_mem_os_allocator.pool_count);
		for (i = pool_pages; i > 0; i--) {
			BUG_ON(list_empty(&mali_mem_os_allocator.pool_pages));
			list_move(mali_mem_os_allocator.pool_pages.next, &pages_list);
		}
		mali_mem_os_allocator.pool_count -= pool_pages;
		remaining -= pool_pages;
		spin_unlock(&mali_mem_os_allocator.pool_lock);
	}

	/* Process pages from pool. */
	i = 0;
	list_for_each_entry_safe(m_page, m_tmp, &pages_list, list) {
		BUG_ON(NULL == m_page);

		list_move_tail(&m_page->list, &os_mem->pages);
	}

	/* Allocate new pages, if needed. */
	for (i = 0; i < remaining; i++) {
		dma_addr_t dma_addr;
		gfp_t flags = __GFP_ZERO | GFP_HIGHUSER;
		int err;

#if defined(CONFIG_ARM) && !defined(CONFIG_ARM_LPAE)
		flags |= GFP_HIGHUSER;
#else
#ifdef CONFIG_ZONE_DMA32
		flags |= GFP_DMA32;
#else
#ifdef CONFIG_ZONE_DMA
#else
		/* arm64 utgard only work on < 4G, but the kernel
		 * didn't provide method to allocte memory < 4G
		 */
		MALI_DEBUG_ASSERT(0);
#endif
#endif
#endif

		new_page = alloc_page(flags);

		if (unlikely(NULL == new_page)) {
			E("err.");
			/* Calculate the number of pages actually allocated, and free them. */
			os_mem->count = (page_count - remaining) + i;
			atomic_add(os_mem->count, &mali_mem_os_allocator.allocated_pages);
			mali_mem_os_free(&os_mem->pages, os_mem->count, MALI_FALSE);
			return -ENOMEM;
		}

		/* Ensure page is flushed from CPU caches. */
		dma_addr = dma_map_page(&mali_platform_device->dev, new_page,
					0, _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);
		dma_unmap_page(&mali_platform_device->dev, dma_addr,
			       _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);
		dma_addr = dma_map_page(&mali_platform_device->dev, new_page,
					0, _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);

		err = dma_mapping_error(&mali_platform_device->dev, dma_addr);
		if (unlikely(err)) {
			MALI_DEBUG_PRINT_ERROR(("OS Mem: Failed to DMA map page %p: %u",
						new_page, err));
			__free_page(new_page);
			os_mem->count = (page_count - remaining) + i;
			atomic_add(os_mem->count, &mali_mem_os_allocator.allocated_pages);
			mali_mem_os_free(&os_mem->pages, os_mem->count, MALI_FALSE);
			return -EFAULT;
		}

		/* Store page phys addr */
		SetPagePrivate(new_page);
		set_page_private(new_page, dma_addr);

		m_page = _mali_page_node_allocate(MALI_PAGE_NODE_OS);
		if (unlikely(NULL == m_page)) {
			MALI_PRINT_ERROR(("OS Mem: Can't allocate mali_page node! \n"));
			dma_unmap_page(&mali_platform_device->dev, page_private(new_page),
				       _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);
			ClearPagePrivate(new_page);
			__free_page(new_page);
			os_mem->count = (page_count - remaining) + i;
			atomic_add(os_mem->count, &mali_mem_os_allocator.allocated_pages);
			mali_mem_os_free(&os_mem->pages, os_mem->count, MALI_FALSE);
			return -EFAULT;
		}
		m_page->page = new_page;

		list_add_tail(&m_page->list, &os_mem->pages);
	}

	atomic_add(page_count, &mali_mem_os_allocator.allocated_pages);

	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES > mali_mem_os_allocator.pool_count) {
		MALI_DEBUG_PRINT(4, ("OS Mem: Stopping pool trim timer, only %u pages on pool\n", mali_mem_os_allocator.pool_count));
		cancel_delayed_work(&mali_mem_os_allocator.timed_shrinker);
	}

	return 0;
}


_mali_osk_errcode_t mali_mem_os_mali_map(mali_mem_os_mem *os_mem, struct mali_session_data *session, u32 vaddr, u32 start_page, u32 mapping_pgae_num, u32 props)
{
	struct mali_page_directory *pagedir = session->page_directory;
	struct mali_page_node *m_page;
	u32 virt;
	u32 prop = props;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_POINTER(os_mem);

	MALI_DEBUG_ASSERT(start_page <= os_mem->count);
	MALI_DEBUG_ASSERT((start_page + mapping_pgae_num) <= os_mem->count);

	if ((start_page + mapping_pgae_num) == os_mem->count) {

		virt = vaddr + MALI_MMU_PAGE_SIZE * (start_page + mapping_pgae_num);

		list_for_each_entry_reverse(m_page, &os_mem->pages, list) {

			virt -= MALI_MMU_PAGE_SIZE;
			if (mapping_pgae_num > 0) {
				dma_addr_t phys = page_private(m_page->page);
#if defined(CONFIG_ARCH_DMA_ADDR_T_64BIT)
				/* Verify that the "physical" address is 32-bit and
				* usable for Mali, when on a system with bus addresses
				* wider than 32-bit. */
				MALI_DEBUG_ASSERT(0 == (phys >> 32));
#endif
				mali_mmu_pagedir_update(pagedir, virt, (mali_dma_addr)phys, MALI_MMU_PAGE_SIZE, prop);
			} else {
				break;
			}
			mapping_pgae_num--;
		}

	} else {
		u32 i = 0;
		virt = vaddr;
		list_for_each_entry(m_page, &os_mem->pages, list) {

			if (i >= start_page) {
				dma_addr_t phys = page_private(m_page->page);

#if defined(CONFIG_ARCH_DMA_ADDR_T_64BIT)
				/* Verify that the "physical" address is 32-bit and
				* usable for Mali, when on a system with bus addresses
				* wider than 32-bit. */
				MALI_DEBUG_ASSERT(0 == (phys >> 32));
#endif
				mali_mmu_pagedir_update(pagedir, virt, (mali_dma_addr)phys, MALI_MMU_PAGE_SIZE, prop);
			}
			i++;
			virt += MALI_MMU_PAGE_SIZE;
		}
	}
	return _MALI_OSK_ERR_OK;
}


void mali_mem_os_mali_unmap(mali_mem_allocation *alloc)
{
	struct mali_session_data *session;
	MALI_DEBUG_ASSERT_POINTER(alloc);
	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);

	mali_session_memory_lock(session);
	mali_mem_mali_map_free(session, alloc->psize, alloc->mali_vma_node.vm_node.start,
			       alloc->flags);
	mali_session_memory_unlock(session);
}

int mali_mem_os_cpu_map(mali_mem_backend *mem_bkend, struct vm_area_struct *vma)
{
	mali_mem_os_mem *os_mem = &mem_bkend->os_mem;
	struct mali_page_node *m_page;
	struct page *page;
	int ret;
	unsigned long addr = vma->vm_start;
	MALI_DEBUG_ASSERT(MALI_MEM_OS == mem_bkend->type);

	list_for_each_entry(m_page, &os_mem->pages, list) {
		/* We should use vm_insert_page, but it does a dcache
		 * flush which makes it way slower than remap_pfn_range or vmf_insert_pfn.
		ret = vm_insert_page(vma, addr, page);
		*/
		page = m_page->page;
		ret = vmf_insert_pfn(vma, addr, page_to_pfn(page));

		if (unlikely(0 != ret)) {
			return -EFAULT;
		}
		addr += _MALI_OSK_MALI_PAGE_SIZE;
	}

	return 0;
}

_mali_osk_errcode_t mali_mem_os_resize_cpu_map_locked(mali_mem_backend *mem_bkend, struct vm_area_struct *vma, unsigned long start_vaddr, u32 mappig_size)
{
	mali_mem_os_mem *os_mem = &mem_bkend->os_mem;
	struct mali_page_node *m_page;
	int ret;
	int offset;
	int mapping_page_num;
	int count ;

	unsigned long vstart = vma->vm_start;
	count = 0;
	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_OS);
	MALI_DEBUG_ASSERT(0 == start_vaddr % _MALI_OSK_MALI_PAGE_SIZE);
	MALI_DEBUG_ASSERT(0 == vstart % _MALI_OSK_MALI_PAGE_SIZE);
	offset = (start_vaddr - vstart) / _MALI_OSK_MALI_PAGE_SIZE;
	MALI_DEBUG_ASSERT(offset <= os_mem->count);
	mapping_page_num = mappig_size / _MALI_OSK_MALI_PAGE_SIZE;
	MALI_DEBUG_ASSERT((offset + mapping_page_num) <= os_mem->count);

	if ((offset + mapping_page_num) == os_mem->count) {

		unsigned long vm_end = start_vaddr + mappig_size;

		list_for_each_entry_reverse(m_page, &os_mem->pages, list) {

			vm_end -= _MALI_OSK_MALI_PAGE_SIZE;
			if (mapping_page_num > 0) {
				ret = vmf_insert_pfn(vma, vm_end, page_to_pfn(m_page->page));

				if (unlikely(0 != ret)) {
					/*will return -EBUSY If the page has already been mapped into table, but it's OK*/
					if (-EBUSY == ret) {
						break;
					} else {
						MALI_DEBUG_PRINT(1, ("OS Mem: mali_mem_os_resize_cpu_map_locked failed, ret = %d, offset is %d,page_count is %d\n",
								     ret,  offset + mapping_page_num, os_mem->count));
					}
					return _MALI_OSK_ERR_FAULT;
				}
			} else {
				break;
			}
			mapping_page_num--;

		}
	} else {

		list_for_each_entry(m_page, &os_mem->pages, list) {
			if (count >= offset) {

				ret = vmf_insert_pfn(vma, vstart, page_to_pfn(m_page->page));

				if (unlikely(0 != ret)) {
					/*will return -EBUSY If the page has already been mapped into table, but it's OK*/
					if (-EBUSY == ret) {
						break;
					} else {
						MALI_DEBUG_PRINT(1, ("OS Mem: mali_mem_os_resize_cpu_map_locked failed, ret = %d, count is %d, offset is %d,page_count is %d\n",
								     ret, count, offset, os_mem->count));
					}
					return _MALI_OSK_ERR_FAULT;
				}
			}
			count++;
			vstart += _MALI_OSK_MALI_PAGE_SIZE;
		}
	}
	return _MALI_OSK_ERR_OK;
}

u32 mali_mem_os_release(mali_mem_backend *mem_bkend)
{

	mali_mem_allocation *alloc;
	struct mali_session_data *session;
	u32 free_pages_nr = 0;
	MALI_DEBUG_ASSERT_POINTER(mem_bkend);
	MALI_DEBUG_ASSERT(MALI_MEM_OS == mem_bkend->type);

	alloc = mem_bkend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);

	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);

	/* Unmap the memory from the mali virtual address space. */
	mali_mem_os_mali_unmap(alloc);
	mutex_lock(&mem_bkend->mutex);
	/* Free pages */
	if (MALI_MEM_BACKEND_FLAG_COWED & mem_bkend->flags) {
		/* Lock to avoid the free race condition for the cow shared memory page node. */
		_mali_osk_mutex_wait(session->cow_lock);
		free_pages_nr = mali_mem_os_free(&mem_bkend->os_mem.pages, mem_bkend->os_mem.count, MALI_TRUE);
		_mali_osk_mutex_signal(session->cow_lock);
	} else {
		free_pages_nr = mali_mem_os_free(&mem_bkend->os_mem.pages, mem_bkend->os_mem.count, MALI_FALSE);
	}
	mutex_unlock(&mem_bkend->mutex);

	MALI_DEBUG_PRINT(4, ("OS Mem free : allocated size = 0x%x, free size = 0x%x\n", mem_bkend->os_mem.count * _MALI_OSK_MALI_PAGE_SIZE,
			     free_pages_nr * _MALI_OSK_MALI_PAGE_SIZE));

	mem_bkend->os_mem.count = 0;
	return free_pages_nr;
}


#define MALI_MEM_OS_PAGE_TABLE_PAGE_POOL_SIZE 128
static struct {
	struct {
		mali_dma_addr phys;
		mali_io_address mapping;
	} page[MALI_MEM_OS_PAGE_TABLE_PAGE_POOL_SIZE];
	size_t count;
	spinlock_t lock;
} mali_mem_page_table_page_pool = {
	.count = 0,
	.lock = __SPIN_LOCK_UNLOCKED(pool_lock),
};

_mali_osk_errcode_t mali_mem_os_get_table_page(mali_dma_addr *phys, mali_io_address *mapping)
{
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_NOMEM;
	dma_addr_t tmp_phys;

	spin_lock(&mali_mem_page_table_page_pool.lock);
	if (0 < mali_mem_page_table_page_pool.count) {
		u32 i = --mali_mem_page_table_page_pool.count;
		*phys = mali_mem_page_table_page_pool.page[i].phys;
		*mapping = mali_mem_page_table_page_pool.page[i].mapping;

		ret = _MALI_OSK_ERR_OK;
	}
	spin_unlock(&mali_mem_page_table_page_pool.lock);

	if (_MALI_OSK_ERR_OK != ret) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		*mapping = dma_alloc_attrs(&mali_platform_device->dev,
					   _MALI_OSK_MALI_PAGE_SIZE, &tmp_phys,
					   GFP_KERNEL, DMA_ATTR_WRITE_COMBINE);
#else
		*mapping = dma_alloc_writecombine(&mali_platform_device->dev,
						  _MALI_OSK_MALI_PAGE_SIZE, &tmp_phys, GFP_KERNEL);
#endif
		if (NULL != *mapping) {
			ret = _MALI_OSK_ERR_OK;

#if defined(CONFIG_ARCH_DMA_ADDR_T_64BIT)
			/* Verify that the "physical" address is 32-bit and
			 * usable for Mali, when on a system with bus addresses
			 * wider than 32-bit. */
			MALI_DEBUG_ASSERT(0 == (tmp_phys >> 32));
#endif

			*phys = (mali_dma_addr)tmp_phys;
		}
	}

	return ret;
}

void mali_mem_os_release_table_page(mali_dma_addr phys, void *virt)
{
	spin_lock(&mali_mem_page_table_page_pool.lock);
	if (MALI_MEM_OS_PAGE_TABLE_PAGE_POOL_SIZE > mali_mem_page_table_page_pool.count) {
		u32 i = mali_mem_page_table_page_pool.count;
		mali_mem_page_table_page_pool.page[i].phys = phys;
		mali_mem_page_table_page_pool.page[i].mapping = virt;

		++mali_mem_page_table_page_pool.count;

		spin_unlock(&mali_mem_page_table_page_pool.lock);
	} else {
		spin_unlock(&mali_mem_page_table_page_pool.lock);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		dma_free_attrs(&mali_platform_device->dev,
			       _MALI_OSK_MALI_PAGE_SIZE, virt, phys,
			       DMA_ATTR_WRITE_COMBINE);
#else
		dma_free_writecombine(&mali_platform_device->dev,
				      _MALI_OSK_MALI_PAGE_SIZE, virt, phys);
#endif
	}
}

void mali_mem_os_free_page_node(struct mali_page_node *m_page)
{
	struct page *page = m_page->page;
	MALI_DEBUG_ASSERT(m_page->type == MALI_PAGE_NODE_OS);

	if (1  == page_count(page)) {
		dma_unmap_page(&mali_platform_device->dev, page_private(page),
			       _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);
		ClearPagePrivate(page);
	}
	__free_page(page);
	m_page->page = NULL;
	list_del(&m_page->list);
	kfree(m_page);
}

/* The maximum number of page table pool pages to free in one go. */
#define MALI_MEM_OS_CHUNK_TO_FREE 64UL

/* Free a certain number of pages from the page table page pool.
 * The pool lock must be held when calling the function, and the lock will be
 * released before returning.
 */
static void mali_mem_os_page_table_pool_free(size_t nr_to_free)
{
	mali_dma_addr phys_arr[MALI_MEM_OS_CHUNK_TO_FREE];
	void *virt_arr[MALI_MEM_OS_CHUNK_TO_FREE];
	u32 i;

	MALI_DEBUG_ASSERT(nr_to_free <= MALI_MEM_OS_CHUNK_TO_FREE);

	/* Remove nr_to_free pages from the pool and store them locally on stack. */
	for (i = 0; i < nr_to_free; i++) {
		u32 pool_index = mali_mem_page_table_page_pool.count - i - 1;

		phys_arr[i] = mali_mem_page_table_page_pool.page[pool_index].phys;
		virt_arr[i] = mali_mem_page_table_page_pool.page[pool_index].mapping;
	}

	mali_mem_page_table_page_pool.count -= nr_to_free;

	spin_unlock(&mali_mem_page_table_page_pool.lock);

	/* After releasing the spinlock: free the pages we removed from the pool. */
	for (i = 0; i < nr_to_free; i++) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		dma_free_attrs(&mali_platform_device->dev, _MALI_OSK_MALI_PAGE_SIZE,
			       virt_arr[i], (dma_addr_t)phys_arr[i],
			       DMA_ATTR_WRITE_COMBINE);
#else
		dma_free_writecombine(&mali_platform_device->dev,
				      _MALI_OSK_MALI_PAGE_SIZE,
				      virt_arr[i], (dma_addr_t)phys_arr[i]);
#endif
	}
}

static void mali_mem_os_trim_page_table_page_pool(void)
{
	size_t nr_to_free = 0;
	size_t nr_to_keep;

	/* Keep 2 page table pages for each 1024 pages in the page cache. */
	nr_to_keep = mali_mem_os_allocator.pool_count / 512;
	/* And a minimum of eight pages, to accomodate new sessions. */
	nr_to_keep += 8;

	if (0 == spin_trylock(&mali_mem_page_table_page_pool.lock)) return;

	if (nr_to_keep < mali_mem_page_table_page_pool.count) {
		nr_to_free = mali_mem_page_table_page_pool.count - nr_to_keep;
		nr_to_free = min((size_t)MALI_MEM_OS_CHUNK_TO_FREE, nr_to_free);
	}

	/* Pool lock will be released by the callee. */
	mali_mem_os_page_table_pool_free(nr_to_free);
}

static unsigned long mali_mem_os_shrink_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	return mali_mem_os_allocator.pool_count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
static int mali_mem_os_shrink(int nr_to_scan, gfp_t gfp_mask)
#else
static int mali_mem_os_shrink(struct shrinker *shrinker, int nr_to_scan, gfp_t gfp_mask)
#endif /* Linux < 2.6.35 */
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
static int mali_mem_os_shrink(struct shrinker *shrinker, struct shrink_control *sc)
#else
static unsigned long mali_mem_os_shrink(struct shrinker *shrinker, struct shrink_control *sc)
#endif /* Linux < 3.12.0 */
#endif /* Linux < 3.0.0 */
{
	struct mali_page_node *m_page, *m_tmp;
	unsigned long flags;
	struct list_head *le, pages;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
	int nr = nr_to_scan;
#else
	int nr = sc->nr_to_scan;
#endif

	if (0 == nr) {
		return mali_mem_os_shrink_count(shrinker, sc);
	}

	if (0 == spin_trylock_irqsave(&mali_mem_os_allocator.pool_lock, flags)) {
		/* Not able to lock. */
		return -1;
	}

	if (0 == mali_mem_os_allocator.pool_count) {
		/* No pages availble */
		spin_unlock_irqrestore(&mali_mem_os_allocator.pool_lock, flags);
		return 0;
	}

	/* Release from general page pool */
	nr = min((size_t)nr, mali_mem_os_allocator.pool_count);
	mali_mem_os_allocator.pool_count -= nr;
	list_for_each(le, &mali_mem_os_allocator.pool_pages) {
		--nr;
		if (0 == nr) break;
	}
	list_cut_position(&pages, &mali_mem_os_allocator.pool_pages, le);
	spin_unlock_irqrestore(&mali_mem_os_allocator.pool_lock, flags);

	list_for_each_entry_safe(m_page, m_tmp, &pages, list) {
		mali_mem_os_free_page_node(m_page);
	}

	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES > mali_mem_os_allocator.pool_count) {
		/* Pools are empty, stop timer */
		MALI_DEBUG_PRINT(5, ("Stopping timer, only %u pages on pool\n", mali_mem_os_allocator.pool_count));
		cancel_delayed_work(&mali_mem_os_allocator.timed_shrinker);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
	return mali_mem_os_shrink_count(shrinker, sc);
#else
	return nr;
#endif
}

static void mali_mem_os_trim_pool(struct work_struct *data)
{
	struct mali_page_node *m_page, *m_tmp;
	struct list_head *le;
	LIST_HEAD(pages);
	size_t nr_to_free;

	MALI_IGNORE(data);

	MALI_DEBUG_PRINT(3, ("OS Mem: Trimming pool %u\n", mali_mem_os_allocator.pool_count));

	/* Release from general page pool */
	spin_lock(&mali_mem_os_allocator.pool_lock);
	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES < mali_mem_os_allocator.pool_count) {
		size_t count = mali_mem_os_allocator.pool_count - MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES;
		const size_t min_to_free = min(64, MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES);

		/* Free half the pages on the pool above the static limit. Or 64 pages, 256KB. */
		nr_to_free = max(count / 2, min_to_free);

		mali_mem_os_allocator.pool_count -= nr_to_free;
		list_for_each(le, &mali_mem_os_allocator.pool_pages) {
			--nr_to_free;
			if (0 == nr_to_free) break;
		}
		list_cut_position(&pages, &mali_mem_os_allocator.pool_pages, le);
	}
	spin_unlock(&mali_mem_os_allocator.pool_lock);

	list_for_each_entry_safe(m_page, m_tmp, &pages, list) {
		mali_mem_os_free_page_node(m_page);
	}

	/* Release some pages from page table page pool */
	mali_mem_os_trim_page_table_page_pool();

	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES < mali_mem_os_allocator.pool_count) {
		MALI_DEBUG_PRINT(4, ("OS Mem: Starting pool trim timer %u\n", mali_mem_os_allocator.pool_count));
		queue_delayed_work(mali_mem_os_allocator.wq, &mali_mem_os_allocator.timed_shrinker, MALI_OS_MEMORY_POOL_TRIM_JIFFIES);
	}
}

_mali_osk_errcode_t mali_mem_os_init(void)
{
	mali_mem_os_allocator.wq = alloc_workqueue("mali-mem", WQ_UNBOUND, 1);
	if (NULL == mali_mem_os_allocator.wq) {
		return _MALI_OSK_ERR_NOMEM;
	}

	register_shrinker(&mali_mem_os_allocator.shrinker);

	return _MALI_OSK_ERR_OK;
}

void mali_mem_os_term(void)
{
	struct mali_page_node *m_page, *m_tmp;
	unregister_shrinker(&mali_mem_os_allocator.shrinker);
	cancel_delayed_work_sync(&mali_mem_os_allocator.timed_shrinker);

	if (NULL != mali_mem_os_allocator.wq) {
		destroy_workqueue(mali_mem_os_allocator.wq);
		mali_mem_os_allocator.wq = NULL;
	}

	spin_lock(&mali_mem_os_allocator.pool_lock);
	list_for_each_entry_safe(m_page, m_tmp, &mali_mem_os_allocator.pool_pages, list) {
		mali_mem_os_free_page_node(m_page);

		--mali_mem_os_allocator.pool_count;
	}
	BUG_ON(mali_mem_os_allocator.pool_count);
	spin_unlock(&mali_mem_os_allocator.pool_lock);

	/* Release from page table page pool */
	do {
		u32 nr_to_free;

		spin_lock(&mali_mem_page_table_page_pool.lock);

		nr_to_free = min((size_t)MALI_MEM_OS_CHUNK_TO_FREE, mali_mem_page_table_page_pool.count);

		/* Pool lock will be released by the callee. */
		mali_mem_os_page_table_pool_free(nr_to_free);
	} while (0 != mali_mem_page_table_page_pool.count);
}

_mali_osk_errcode_t mali_memory_core_resource_os_memory(u32 size)
{
	mali_mem_os_allocator.allocation_limit = size;

	MALI_SUCCESS;
}

u32 mali_mem_os_stat(void)
{
	return atomic_read(&mali_mem_os_allocator.allocated_pages) * _MALI_OSK_MALI_PAGE_SIZE;
}

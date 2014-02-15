/*
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

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

static unsigned long mali_mem_os_shrink_count(struct shrinker *shrinker, struct shrink_control *sc);
static unsigned long mali_mem_os_shrink_scan(struct shrinker *shrinker, struct shrink_control *sc);
static void mali_mem_os_trim_pool(struct work_struct *work);

static struct mali_mem_os_allocator {
	spinlock_t pool_lock;
	struct list_head pool_pages;
	size_t pool_count;

	atomic_t allocated_pages;
	size_t allocation_limit;

	struct shrinker shrinker;
	struct delayed_work timed_shrinker;
	struct workqueue_struct *wq;
} mali_mem_os_allocator = {
	.pool_lock = __SPIN_LOCK_UNLOCKED(pool_lock),
	.pool_pages = LIST_HEAD_INIT(mali_mem_os_allocator.pool_pages),
	.pool_count = 0,

	.allocated_pages = ATOMIC_INIT(0),
	.allocation_limit = 0,

	.shrinker.count_objects = mali_mem_os_shrink_count,
	.shrinker.scan_objects = mali_mem_os_shrink_scan,
	.shrinker.seeks = DEFAULT_SEEKS,
	.timed_shrinker = __DELAYED_WORK_INITIALIZER(mali_mem_os_allocator.timed_shrinker, mali_mem_os_trim_pool, TIMER_DEFERRABLE),
};

static void mali_mem_os_free(mali_mem_allocation *descriptor)
{
	LIST_HEAD(pages);

	MALI_DEBUG_ASSERT(MALI_MEM_OS == descriptor->type);

	atomic_sub(descriptor->os_mem.count, &mali_mem_os_allocator.allocated_pages);

	/* Put pages on pool. */
	list_cut_position(&pages, &descriptor->os_mem.pages, descriptor->os_mem.pages.prev);

	spin_lock(&mali_mem_os_allocator.pool_lock);

	list_splice(&pages, &mali_mem_os_allocator.pool_pages);
	mali_mem_os_allocator.pool_count += descriptor->os_mem.count;

	spin_unlock(&mali_mem_os_allocator.pool_lock);

	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES < mali_mem_os_allocator.pool_count) {
		MALI_DEBUG_PRINT(5, ("OS Mem: Starting pool trim timer %u\n", mali_mem_os_allocator.pool_count));
		queue_delayed_work(mali_mem_os_allocator.wq, &mali_mem_os_allocator.timed_shrinker, MALI_OS_MEMORY_POOL_TRIM_JIFFIES);
	}
}

static int mali_mem_os_alloc_pages(mali_mem_allocation *descriptor, u32 size)
{
	struct page *new_page, *tmp;
	LIST_HEAD(pages);
	size_t page_count = PAGE_ALIGN(size) / _MALI_OSK_MALI_PAGE_SIZE;
	size_t remaining = page_count;
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT(MALI_MEM_OS == descriptor->type);

	INIT_LIST_HEAD(&descriptor->os_mem.pages);
	descriptor->os_mem.count = page_count;

	/* Grab pages from pool. */
	{
		size_t pool_pages;
		spin_lock(&mali_mem_os_allocator.pool_lock);
		pool_pages = min(remaining, mali_mem_os_allocator.pool_count);
		for (i = pool_pages; i > 0; i--) {
			BUG_ON(list_empty(&mali_mem_os_allocator.pool_pages));
			list_move(mali_mem_os_allocator.pool_pages.next, &pages);
		}
		mali_mem_os_allocator.pool_count -= pool_pages;
		remaining -= pool_pages;
		spin_unlock(&mali_mem_os_allocator.pool_lock);
	}

	/* Process pages from pool. */
	i = 0;
	list_for_each_entry_safe(new_page, tmp, &pages, lru) {
		BUG_ON(NULL == new_page);

		list_move_tail(&new_page->lru, &descriptor->os_mem.pages);
	}

	/* Allocate new pages, if needed. */
	for (i = 0; i < remaining; i++) {
		dma_addr_t dma_addr;

		new_page = alloc_page(GFP_HIGHUSER | __GFP_ZERO | __GFP_REPEAT | __GFP_NOWARN | __GFP_COLD);

		if (unlikely(NULL == new_page)) {
			/* Calculate the number of pages actually allocated, and free them. */
			descriptor->os_mem.count = (page_count - remaining) + i;
			atomic_add(descriptor->os_mem.count, &mali_mem_os_allocator.allocated_pages);
			mali_mem_os_free(descriptor);
			return -ENOMEM;
		}

		/* Ensure page is flushed from CPU caches. */
		dma_addr = dma_map_page(&mali_platform_device->dev, new_page,
		                        0, _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);

		/* Store page phys addr */
		SetPagePrivate(new_page);
		set_page_private(new_page, dma_addr);

		list_add_tail(&new_page->lru, &descriptor->os_mem.pages);
	}

	atomic_add(page_count, &mali_mem_os_allocator.allocated_pages);

	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES > mali_mem_os_allocator.pool_count) {
		MALI_DEBUG_PRINT(4, ("OS Mem: Stopping pool trim timer, only %u pages on pool\n", mali_mem_os_allocator.pool_count));
		cancel_delayed_work(&mali_mem_os_allocator.timed_shrinker);
	}

	return 0;
}

static int mali_mem_os_mali_map(mali_mem_allocation *descriptor, struct mali_session_data *session)
{
	struct mali_page_directory *pagedir = session->page_directory;
	struct page *page;
	_mali_osk_errcode_t err;
	u32 virt = descriptor->mali_mapping.addr;
	u32 prop = descriptor->mali_mapping.properties;

	MALI_DEBUG_ASSERT(MALI_MEM_OS == descriptor->type);

	err = mali_mem_mali_map_prepare(descriptor);
	if (_MALI_OSK_ERR_OK != err) {
		return -ENOMEM;
	}

	list_for_each_entry(page, &descriptor->os_mem.pages, lru) {
		u32 phys = page_private(page);
		mali_mmu_pagedir_update(pagedir, virt, phys, MALI_MMU_PAGE_SIZE, prop);
		virt += MALI_MMU_PAGE_SIZE;
	}

	return 0;
}

static void mali_mem_os_mali_unmap(struct mali_session_data *session, mali_mem_allocation *descriptor)
{
	mali_mem_mali_map_free(descriptor);
}

static int mali_mem_os_cpu_map(mali_mem_allocation *descriptor, struct vm_area_struct *vma)
{
	struct page *page;
	int ret;
	unsigned long addr = vma->vm_start;

	list_for_each_entry(page, &descriptor->os_mem.pages, lru) {
		/* We should use vm_insert_page, but it does a dcache
		 * flush which makes it way slower than remap_pfn_range or vm_insert_pfn.
		ret = vm_insert_page(vma, addr, page);
		*/
		ret = vm_insert_pfn(vma, addr, page_to_pfn(page));

		if (unlikely(0 != ret)) {
			return -EFAULT;
		}
		addr += _MALI_OSK_MALI_PAGE_SIZE;
	}

	return 0;
}

mali_mem_allocation *mali_mem_os_alloc(u32 mali_addr, u32 size, struct vm_area_struct *vma, struct mali_session_data *session)
{
	mali_mem_allocation *descriptor;
	int err;

	if (atomic_read(&mali_mem_os_allocator.allocated_pages) * _MALI_OSK_MALI_PAGE_SIZE + size > mali_mem_os_allocator.allocation_limit) {
		MALI_DEBUG_PRINT(2, ("Mali Mem: Unable to allocate %u bytes. Currently allocated: %lu, max limit %lu\n",
		                     size,
		                     atomic_read(&mali_mem_os_allocator.allocated_pages) * _MALI_OSK_MALI_PAGE_SIZE,
		                     mali_mem_os_allocator.allocation_limit));
		return NULL;
	}

	descriptor = mali_mem_descriptor_create(session, MALI_MEM_OS);
	if (NULL == descriptor) return NULL;

	descriptor->mali_mapping.addr = mali_addr;
	descriptor->size = size;
	descriptor->cpu_mapping.addr = (void __user*)vma->vm_start;
	descriptor->cpu_mapping.ref = 1;

	if (VM_SHARED == (VM_SHARED & vma->vm_flags)) {
		descriptor->mali_mapping.properties = MALI_MMU_FLAGS_DEFAULT;
	} else {
		/* Cached Mali memory mapping */
		descriptor->mali_mapping.properties = MALI_MMU_FLAGS_FORCE_GP_READ_ALLOCATE;
		vma->vm_flags |= VM_SHARED;
	}

	err = mali_mem_os_alloc_pages(descriptor, size); /* Allocate pages */
	if (0 != err) goto alloc_failed;

	/* Take session memory lock */
	_mali_osk_mutex_wait(session->memory_lock);

	err = mali_mem_os_mali_map(descriptor, session); /* Map on Mali */
	if (0 != err) goto mali_map_failed;

	_mali_osk_mutex_signal(session->memory_lock);

	err = mali_mem_os_cpu_map(descriptor, vma); /* Map on CPU */
	if (0 != err) goto cpu_map_failed;

	return descriptor;

cpu_map_failed:
	mali_mem_os_mali_unmap(session, descriptor);
mali_map_failed:
	_mali_osk_mutex_signal(session->memory_lock);
	mali_mem_os_free(descriptor);
alloc_failed:
	mali_mem_descriptor_destroy(descriptor);
	MALI_DEBUG_PRINT(2, ("OS allocator: Failed to allocate memory (%d)\n", err));
	return NULL;
}

void mali_mem_os_release(mali_mem_allocation *descriptor)
{
	struct mali_session_data *session = descriptor->session;

	/* Unmap the memory from the mali virtual address space. */
	mali_mem_os_mali_unmap(session, descriptor);

	/* Free pages */
	mali_mem_os_free(descriptor);
}


#define MALI_MEM_OS_PAGE_TABLE_PAGE_POOL_SIZE 128
static struct {
	struct {
		u32 phys;
		mali_io_address mapping;
	} page[MALI_MEM_OS_PAGE_TABLE_PAGE_POOL_SIZE];
	u32 count;
	spinlock_t lock;
} mali_mem_page_table_page_pool = {
	.count = 0,
	.lock = __SPIN_LOCK_UNLOCKED(pool_lock),
};

_mali_osk_errcode_t mali_mem_os_get_table_page(u32 *phys, mali_io_address *mapping)
{
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_NOMEM;

	spin_lock(&mali_mem_page_table_page_pool.lock);
	if (0 < mali_mem_page_table_page_pool.count) {
		u32 i = --mali_mem_page_table_page_pool.count;
		*phys = mali_mem_page_table_page_pool.page[i].phys;
		*mapping = mali_mem_page_table_page_pool.page[i].mapping;

		ret = _MALI_OSK_ERR_OK;
	}
	spin_unlock(&mali_mem_page_table_page_pool.lock);

	if (_MALI_OSK_ERR_OK != ret) {
		*mapping = dma_alloc_writecombine(&mali_platform_device->dev, _MALI_OSK_MALI_PAGE_SIZE, phys, GFP_KERNEL);
		if (NULL != *mapping) {
			ret = _MALI_OSK_ERR_OK;
		}
	}

	return ret;
}

void mali_mem_os_release_table_page(u32 phys, void *virt)
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

		dma_free_writecombine(&mali_platform_device->dev, _MALI_OSK_MALI_PAGE_SIZE, virt, phys);
	}
}

static void mali_mem_os_free_page(struct page *page)
{
	BUG_ON(page_count(page) != 1);

	dma_unmap_page(&mali_platform_device->dev, page_private(page),
	               _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);

	ClearPagePrivate(page);

	__free_page(page);
}

/* The maximum number of page table pool pages to free in one go. */
#define MALI_MEM_OS_CHUNK_TO_FREE 64UL

/* Free a certain number of pages from the page table page pool.
 * The pool lock must be held when calling the function, and the lock will be
 * released before returning.
 */
static void mali_mem_os_page_table_pool_free(size_t nr_to_free)
{
	u32 phys_arr[MALI_MEM_OS_CHUNK_TO_FREE];
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
		dma_free_writecombine(&mali_platform_device->dev, _MALI_OSK_MALI_PAGE_SIZE, virt_arr[i], phys_arr[i]);
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
	return mali_mem_os_allocator.pool_count + mali_mem_page_table_page_pool.count;
}

static unsigned long mali_mem_os_shrink_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct page *page, *tmp;
	unsigned long flags;
	struct list_head *le, pages;
	int nr = sc->nr_to_scan;
	unsigned long freed = 0;

	if (0 == mali_mem_os_allocator.pool_count) {
		/* No pages availble */
		return SHRINK_STOP;
	}

	if (0 == spin_trylock_irqsave(&mali_mem_os_allocator.pool_lock, flags)) {
		/* Not able to lock. */
		return -1;
	}

	/* Release from general page pool */
	nr = min((size_t)nr, mali_mem_os_allocator.pool_count);
	mali_mem_os_allocator.pool_count -= nr;
	list_for_each(le, &mali_mem_os_allocator.pool_pages) {
		--nr;
		freed++;
		if (0 == nr) break;
	}
	list_cut_position(&pages, &mali_mem_os_allocator.pool_pages, le);
	spin_unlock_irqrestore(&mali_mem_os_allocator.pool_lock, flags);

	list_for_each_entry_safe(page, tmp, &pages, lru) {
		mali_mem_os_free_page(page);
	}

	/* Release some pages from page table page pool */
	mali_mem_os_trim_page_table_page_pool();

	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES > mali_mem_os_allocator.pool_count) {
		/* Pools are empty, stop timer */
		MALI_DEBUG_PRINT(5, ("Stopping timer, only %u pages on pool\n", mali_mem_os_allocator.pool_count));
		cancel_delayed_work(&mali_mem_os_allocator.timed_shrinker);
	}

	return freed;
}

static void mali_mem_os_trim_pool(struct work_struct *data)
{
	struct page *page, *tmp;
	struct list_head *le;
	LIST_HEAD(pages);
	size_t nr_to_free;

	MALI_IGNORE(data);

	MALI_DEBUG_PRINT(3, ("OS Mem: Trimming pool %u\n", mali_mem_os_allocator.pool_count));

	/* Release from general page pool */
	spin_lock(&mali_mem_os_allocator.pool_lock);
	if (MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES < mali_mem_os_allocator.pool_count) {
		size_t count = mali_mem_os_allocator.pool_count - MALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_PAGES;
		/* Free half the pages on the pool above the static limit. Or 64 pages, 256KB. */
		nr_to_free = max(count / 2, (size_t)64);

		mali_mem_os_allocator.pool_count -= nr_to_free;
		list_for_each(le, &mali_mem_os_allocator.pool_pages) {
			--nr_to_free;
			if (0 == nr_to_free) break;
		}
		list_cut_position(&pages, &mali_mem_os_allocator.pool_pages, le);
	}
	spin_unlock(&mali_mem_os_allocator.pool_lock);

	list_for_each_entry_safe(page, tmp, &pages, lru) {
		mali_mem_os_free_page(page);
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
	struct page *page, *tmp;

	unregister_shrinker(&mali_mem_os_allocator.shrinker);
	cancel_delayed_work_sync(&mali_mem_os_allocator.timed_shrinker);
	destroy_workqueue(mali_mem_os_allocator.wq);

	spin_lock(&mali_mem_os_allocator.pool_lock);
	list_for_each_entry_safe(page, tmp, &mali_mem_os_allocator.pool_pages, lru) {
		mali_mem_os_free_page(page);

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

/*
 * Copyright (C) 2013-2017 ARM Limited. All rights reserved.
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
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/idr.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/shmem_fs.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_memory.h"
#include "mali_memory_manager.h"
#include "mali_memory_virtual.h"
#include "mali_memory_cow.h"
#include "mali_ukk.h"
#include "mali_kernel_utilization.h"
#include "mali_memory_swap_alloc.h"


static struct _mali_osk_bitmap idx_mgr;
static struct file *global_swap_file;
static struct address_space *global_swap_space;
static _mali_osk_wq_work_t *mali_mem_swap_out_workq = NULL;
static u32 mem_backend_swapped_pool_size;
#ifdef MALI_MEM_SWAP_TRACKING
static u32 mem_backend_swapped_unlock_size;
#endif
/* Lock order: mem_backend_swapped_pool_lock  > each memory backend's mutex lock.
 * This lock used to protect mem_backend_swapped_pool_size and mem_backend_swapped_pool. */
static struct mutex mem_backend_swapped_pool_lock;
static struct list_head mem_backend_swapped_pool;

extern struct mali_mem_os_allocator mali_mem_os_allocator;

#define MALI_SWAP_LOW_MEM_DEFAULT_VALUE (60*1024*1024)
#define MALI_SWAP_INVALIDATE_MALI_ADDRESS (0)               /* Used to mark the given memory cookie is invalidate. */
#define MALI_SWAP_GLOBAL_SWAP_FILE_SIZE (0xFFFFFFFF)
#define MALI_SWAP_GLOBAL_SWAP_FILE_INDEX \
	((MALI_SWAP_GLOBAL_SWAP_FILE_SIZE) >> PAGE_SHIFT)
#define MALI_SWAP_GLOBAL_SWAP_FILE_INDEX_RESERVE (1 << 15) /* Reserved for CoW nonlinear swap backend memory, the space size is 128MB. */

unsigned int mali_mem_swap_out_threshold_value = MALI_SWAP_LOW_MEM_DEFAULT_VALUE;

/**
 * We have two situations to do shrinking things, one is we met low GPU utilization which shows GPU needn't touch too
 * swappable backends in short time, and the other one is we add new swappable backends, the total pool size exceed
 * the threshold value of the swapped pool size.
 */
typedef enum {
	MALI_MEM_SWAP_SHRINK_WITH_LOW_UTILIZATION = 100,
	MALI_MEM_SWAP_SHRINK_FOR_ADDING_NEW_BACKENDS = 257,
} _mali_mem_swap_pool_shrink_type_t;

static void mali_mem_swap_swapped_bkend_pool_check_for_low_utilization(void *arg);

_mali_osk_errcode_t mali_mem_swap_init(void)
{
	gfp_t flags = __GFP_NORETRY | __GFP_NOWARN;

	if (_MALI_OSK_ERR_OK != _mali_osk_bitmap_init(&idx_mgr, MALI_SWAP_GLOBAL_SWAP_FILE_INDEX, MALI_SWAP_GLOBAL_SWAP_FILE_INDEX_RESERVE)) {
		return _MALI_OSK_ERR_NOMEM;
	}

	global_swap_file = shmem_file_setup("mali_swap", MALI_SWAP_GLOBAL_SWAP_FILE_SIZE, VM_NORESERVE);
	if (IS_ERR(global_swap_file)) {
		_mali_osk_bitmap_term(&idx_mgr);
		return _MALI_OSK_ERR_NOMEM;
	}

	global_swap_space = global_swap_file->f_path.dentry->d_inode->i_mapping;

	mali_mem_swap_out_workq = _mali_osk_wq_create_work(mali_mem_swap_swapped_bkend_pool_check_for_low_utilization, NULL);
	if (NULL == mali_mem_swap_out_workq) {
		_mali_osk_bitmap_term(&idx_mgr);
		fput(global_swap_file);
		return _MALI_OSK_ERR_NOMEM;
	}

#if defined(CONFIG_ARM) && !defined(CONFIG_ARM_LPAE)
	flags |= GFP_HIGHUSER;
#else
#ifdef CONFIG_ZONE_DMA32
	flags |= GFP_DMA32;
#else
#ifdef CONFIG_ZONE_DMA
	flags |= GFP_DMA;
#else
	/* arm64 utgard only work on < 4G, but the kernel
	 * didn't provide method to allocte memory < 4G
	 */
	MALI_DEBUG_ASSERT(0);
#endif
#endif
#endif

	/* When we use shmem_read_mapping_page to allocate/swap-in, it will
	 * use these flags to allocate new page if need.*/
	mapping_set_gfp_mask(global_swap_space, flags);

	mem_backend_swapped_pool_size = 0;
#ifdef MALI_MEM_SWAP_TRACKING
	mem_backend_swapped_unlock_size = 0;
#endif
	mutex_init(&mem_backend_swapped_pool_lock);
	INIT_LIST_HEAD(&mem_backend_swapped_pool);

	MALI_DEBUG_PRINT(2, ("Mali SWAP: Swap out threshold vaule is %uM\n", mali_mem_swap_out_threshold_value >> 20));

	return _MALI_OSK_ERR_OK;
}

void mali_mem_swap_term(void)
{
	_mali_osk_bitmap_term(&idx_mgr);

	fput(global_swap_file);

	_mali_osk_wq_delete_work(mali_mem_swap_out_workq);

	MALI_DEBUG_ASSERT(list_empty(&mem_backend_swapped_pool));
	MALI_DEBUG_ASSERT(0 == mem_backend_swapped_pool_size);

	return;
}

struct file *mali_mem_swap_get_global_swap_file(void)
{
	return  global_swap_file;
}

/* Judge if swappable backend in swapped pool. */
static mali_bool mali_memory_swap_backend_in_swapped_pool(mali_mem_backend *mem_bkend)
{
	MALI_DEBUG_ASSERT_POINTER(mem_bkend);

	return !list_empty(&mem_bkend->list);
}

void mali_memory_swap_list_backend_delete(mali_mem_backend *mem_bkend)
{
	MALI_DEBUG_ASSERT_POINTER(mem_bkend);

	mutex_lock(&mem_backend_swapped_pool_lock);
	mutex_lock(&mem_bkend->mutex);

	if (MALI_FALSE == mali_memory_swap_backend_in_swapped_pool(mem_bkend)) {
		mutex_unlock(&mem_bkend->mutex);
		mutex_unlock(&mem_backend_swapped_pool_lock);
		return;
	}

	MALI_DEBUG_ASSERT(!list_empty(&mem_bkend->list));

	list_del_init(&mem_bkend->list);

	mutex_unlock(&mem_bkend->mutex);

	mem_backend_swapped_pool_size -= mem_bkend->size;

	mutex_unlock(&mem_backend_swapped_pool_lock);
}

static void mali_mem_swap_out_page_node(mali_page_node *page_node)
{
	MALI_DEBUG_ASSERT(page_node);

	dma_unmap_page(&mali_platform_device->dev, page_node->swap_it->dma_addr,
		       _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);
	set_page_dirty(page_node->swap_it->page);
	put_page(page_node->swap_it->page);
}

void mali_mem_swap_unlock_single_mem_backend(mali_mem_backend *mem_bkend)
{
	mali_page_node *m_page;

	MALI_DEBUG_ASSERT(1 == mutex_is_locked(&mem_bkend->mutex));

	if (MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN == (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN)) {
		return;
	}

	mem_bkend->flags |= MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN;

	list_for_each_entry(m_page, &mem_bkend->swap_mem.pages, list) {
		mali_mem_swap_out_page_node(m_page);
	}

	return;
}

static void mali_mem_swap_unlock_partial_locked_mem_backend(mali_mem_backend *mem_bkend, mali_page_node *page_node)
{
	mali_page_node *m_page;

	MALI_DEBUG_ASSERT(1 == mutex_is_locked(&mem_bkend->mutex));

	list_for_each_entry(m_page, &mem_bkend->swap_mem.pages, list) {
		if (m_page == page_node) {
			break;
		}
		mali_mem_swap_out_page_node(m_page);
	}
}

static void mali_mem_swap_swapped_bkend_pool_shrink(_mali_mem_swap_pool_shrink_type_t shrink_type)
{
	mali_mem_backend *bkend, *tmp_bkend;
	long system_free_size;
	u32 last_gpu_utilization, gpu_utilization_threshold_value, temp_swap_out_threshold_value;

	MALI_DEBUG_ASSERT(1 == mutex_is_locked(&mem_backend_swapped_pool_lock));

	if (MALI_MEM_SWAP_SHRINK_WITH_LOW_UTILIZATION == shrink_type) {
		/**
		 * When we met that system memory is very low and Mali locked swappable memory size is less than
		 * threshold value, and at the same time, GPU load is very low and don't need high performance,
		 * at this condition, we can unlock more swap memory backend from swapped backends pool.
		 */
		gpu_utilization_threshold_value = MALI_MEM_SWAP_SHRINK_WITH_LOW_UTILIZATION;
		temp_swap_out_threshold_value = (mali_mem_swap_out_threshold_value >> 2);
	} else {
		/* When we add swappable memory backends to swapped pool, we need to think that we couldn't
		* hold too much swappable backends in Mali driver, and also we need considering performance.
		* So there is a balance for swapping out memory backend, we should follow the following conditions:
		* 1. Total memory size in global mem backend swapped pool is more than the defined threshold value.
		* 2. System level free memory size is less than the defined threshold value.
		* 3. Please note that GPU utilization problem isn't considered in this condition.
		*/
		gpu_utilization_threshold_value = MALI_MEM_SWAP_SHRINK_FOR_ADDING_NEW_BACKENDS;
		temp_swap_out_threshold_value = mali_mem_swap_out_threshold_value;
	}

	/* Get system free pages number. */
	system_free_size = global_zone_page_state(NR_FREE_PAGES) * PAGE_SIZE;
	last_gpu_utilization = _mali_ukk_utilization_gp_pp();

	if ((last_gpu_utilization < gpu_utilization_threshold_value)
	    && (system_free_size < mali_mem_swap_out_threshold_value)
	    && (mem_backend_swapped_pool_size > temp_swap_out_threshold_value)) {
		list_for_each_entry_safe(bkend, tmp_bkend, &mem_backend_swapped_pool, list) {
			if (mem_backend_swapped_pool_size <= temp_swap_out_threshold_value) {
				break;
			}

			mutex_lock(&bkend->mutex);

			/* check if backend is in use. */
			if (0 < bkend->using_count) {
				mutex_unlock(&bkend->mutex);
				continue;
			}

			mali_mem_swap_unlock_single_mem_backend(bkend);
			list_del_init(&bkend->list);
			mem_backend_swapped_pool_size -= bkend->size;
#ifdef MALI_MEM_SWAP_TRACKING
			mem_backend_swapped_unlock_size += bkend->size;
#endif
			mutex_unlock(&bkend->mutex);
		}
	}

	return;
}

static void mali_mem_swap_swapped_bkend_pool_check_for_low_utilization(void *arg)
{
	MALI_IGNORE(arg);

	mutex_lock(&mem_backend_swapped_pool_lock);

	mali_mem_swap_swapped_bkend_pool_shrink(MALI_MEM_SWAP_SHRINK_WITH_LOW_UTILIZATION);

	mutex_unlock(&mem_backend_swapped_pool_lock);
}

/**
 * After PP job finished, we add all of swappable memory backend used by this PP
 * job to the tail of the global swapped pool, and if the total size of swappable memory is more than threshold
 * value, we also need to shrink the swapped pool start from the head of the list.
 */
void mali_memory_swap_list_backend_add(mali_mem_backend *mem_bkend)
{
	mutex_lock(&mem_backend_swapped_pool_lock);
	mutex_lock(&mem_bkend->mutex);

	if (mali_memory_swap_backend_in_swapped_pool(mem_bkend)) {
		MALI_DEBUG_ASSERT(!list_empty(&mem_bkend->list));

		list_del_init(&mem_bkend->list);
		list_add_tail(&mem_bkend->list, &mem_backend_swapped_pool);
		mutex_unlock(&mem_bkend->mutex);
		mutex_unlock(&mem_backend_swapped_pool_lock);
		return;
	}

	list_add_tail(&mem_bkend->list, &mem_backend_swapped_pool);

	mutex_unlock(&mem_bkend->mutex);
	mem_backend_swapped_pool_size += mem_bkend->size;

	mali_mem_swap_swapped_bkend_pool_shrink(MALI_MEM_SWAP_SHRINK_FOR_ADDING_NEW_BACKENDS);

	mutex_unlock(&mem_backend_swapped_pool_lock);
	return;
}


u32 mali_mem_swap_idx_alloc(void)
{
	return _mali_osk_bitmap_alloc(&idx_mgr);
}

void mali_mem_swap_idx_free(u32 idx)
{
	_mali_osk_bitmap_free(&idx_mgr, idx);
}

static u32 mali_mem_swap_idx_range_alloc(u32 count)
{
	u32 index;

	index = _mali_osk_bitmap_alloc_range(&idx_mgr, count);

	return index;
}

static void mali_mem_swap_idx_range_free(u32 idx, int num)
{
	_mali_osk_bitmap_free_range(&idx_mgr, idx, num);
}

struct mali_swap_item *mali_mem_swap_alloc_swap_item(void)
{
	mali_swap_item *swap_item;

	swap_item = kzalloc(sizeof(mali_swap_item), GFP_KERNEL);

	if (NULL == swap_item) {
		return NULL;
	}

	atomic_set(&swap_item->ref_count, 1);
	swap_item->page = NULL;
	atomic_add(1, &mali_mem_os_allocator.allocated_pages);

	return swap_item;
}

void mali_mem_swap_free_swap_item(mali_swap_item *swap_item)
{
	struct inode *file_node;
	long long start, end;

	/* If this swap item is shared, we just reduce the reference counter. */
	if (0 == atomic_dec_return(&swap_item->ref_count)) {
		file_node = global_swap_file->f_path.dentry->d_inode;
		start = swap_item->idx;
		start = start << 12;
		end = start + PAGE_SIZE;

		shmem_truncate_range(file_node, start, (end - 1));

		mali_mem_swap_idx_free(swap_item->idx);

		atomic_sub(1, &mali_mem_os_allocator.allocated_pages);

		kfree(swap_item);
	}
}

/* Used to allocate new swap item for new memory allocation and cow page for write. */
struct mali_page_node *_mali_mem_swap_page_node_allocate(void)
{
	struct mali_page_node *m_page;

	m_page = _mali_page_node_allocate(MALI_PAGE_NODE_SWAP);

	if (NULL == m_page) {
		return NULL;
	}

	m_page->swap_it = mali_mem_swap_alloc_swap_item();

	if (NULL == m_page->swap_it) {
		kfree(m_page);
		return NULL;
	}

	return m_page;
}

_mali_osk_errcode_t _mali_mem_swap_put_page_node(struct mali_page_node *m_page)
{

	mali_mem_swap_free_swap_item(m_page->swap_it);

	return _MALI_OSK_ERR_OK;
}

void _mali_mem_swap_page_node_free(struct mali_page_node *m_page)
{
	_mali_mem_swap_put_page_node(m_page);

	kfree(m_page);

	return;
}

u32 mali_mem_swap_free(mali_mem_swap *swap_mem)
{
	struct mali_page_node *m_page, *m_tmp;
	u32 free_pages_nr = 0;

	MALI_DEBUG_ASSERT_POINTER(swap_mem);

	list_for_each_entry_safe(m_page, m_tmp, &swap_mem->pages, list) {
		MALI_DEBUG_ASSERT(m_page->type == MALI_PAGE_NODE_SWAP);

		/* free the page node and release the swap item, if the ref count is 1,
		 * then need also free the swap item. */
		list_del(&m_page->list);
		if (1 == _mali_page_node_get_ref_count(m_page)) {
			free_pages_nr++;
		}

		_mali_mem_swap_page_node_free(m_page);
	}

	return free_pages_nr;
}

static u32 mali_mem_swap_cow_free(mali_mem_cow *cow_mem)
{
	struct mali_page_node *m_page, *m_tmp;
	u32 free_pages_nr = 0;

	MALI_DEBUG_ASSERT_POINTER(cow_mem);

	list_for_each_entry_safe(m_page, m_tmp, &cow_mem->pages, list) {
		MALI_DEBUG_ASSERT(m_page->type == MALI_PAGE_NODE_SWAP);

		/* free the page node and release the swap item, if the ref count is 1,
		 * then need also free the swap item. */
		list_del(&m_page->list);
		if (1 == _mali_page_node_get_ref_count(m_page)) {
			free_pages_nr++;
		}

		_mali_mem_swap_page_node_free(m_page);
	}

	return free_pages_nr;
}

u32 mali_mem_swap_release(mali_mem_backend *mem_bkend, mali_bool is_mali_mapped)
{
	mali_mem_allocation *alloc;
	u32 free_pages_nr = 0;

	MALI_DEBUG_ASSERT_POINTER(mem_bkend);
	alloc = mem_bkend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);

	if (is_mali_mapped) {
		mali_mem_swap_mali_unmap(alloc);
	}

	mali_memory_swap_list_backend_delete(mem_bkend);

	mutex_lock(&mem_bkend->mutex);
	/* To make sure the given memory backend was unlocked from Mali side,
	 * and then free this memory block. */
	mali_mem_swap_unlock_single_mem_backend(mem_bkend);
	mutex_unlock(&mem_bkend->mutex);

	if (MALI_MEM_SWAP == mem_bkend->type) {
		free_pages_nr = mali_mem_swap_free(&mem_bkend->swap_mem);
	} else {
		free_pages_nr = mali_mem_swap_cow_free(&mem_bkend->cow_mem);
	}

	return free_pages_nr;
}

mali_bool mali_mem_swap_in_page_node(struct mali_page_node *page_node)
{
	MALI_DEBUG_ASSERT(NULL != page_node);

	page_node->swap_it->page = shmem_read_mapping_page(global_swap_space, page_node->swap_it->idx);

	if (IS_ERR(page_node->swap_it->page)) {
		MALI_DEBUG_PRINT_ERROR(("SWAP Mem: failed to swap in page with index: %d.\n", page_node->swap_it->idx));
		return MALI_FALSE;
	}

	/* Ensure page is flushed from CPU caches. */
	page_node->swap_it->dma_addr = dma_map_page(&mali_platform_device->dev, page_node->swap_it->page,
				       0, _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);

	return MALI_TRUE;
}

int mali_mem_swap_alloc_pages(mali_mem_swap *swap_mem, u32 size, u32 *bkend_idx)
{
	size_t page_count = PAGE_ALIGN(size) / PAGE_SIZE;
	struct mali_page_node *m_page;
	long system_free_size;
	u32 i, index;
	mali_bool ret;

	MALI_DEBUG_ASSERT(NULL != swap_mem);
	MALI_DEBUG_ASSERT(NULL != bkend_idx);
	MALI_DEBUG_ASSERT(page_count <= MALI_SWAP_GLOBAL_SWAP_FILE_INDEX_RESERVE);

	if (atomic_read(&mali_mem_os_allocator.allocated_pages) * _MALI_OSK_MALI_PAGE_SIZE + size > mali_mem_os_allocator.allocation_limit) {
		MALI_DEBUG_PRINT(2, ("Mali Mem: Unable to allocate %u bytes. Currently allocated: %lu, max limit %lu\n",
				     size,
				     atomic_read(&mali_mem_os_allocator.allocated_pages) * _MALI_OSK_MALI_PAGE_SIZE,
				     mali_mem_os_allocator.allocation_limit));
		return _MALI_OSK_ERR_NOMEM;
	}

	INIT_LIST_HEAD(&swap_mem->pages);
	swap_mem->count = page_count;
	index = mali_mem_swap_idx_range_alloc(page_count);

	if (_MALI_OSK_BITMAP_INVALIDATE_INDEX == index) {
		MALI_PRINT_ERROR(("Mali Swap: Failed to allocate continuous index for swappable Mali memory."));
		return _MALI_OSK_ERR_FAULT;
	}

	for (i = 0; i < page_count; i++) {
		m_page = _mali_mem_swap_page_node_allocate();

		if (NULL == m_page) {
			MALI_DEBUG_PRINT_ERROR(("SWAP Mem: Failed to allocate mali page node."));
			swap_mem->count = i;

			mali_mem_swap_free(swap_mem);
			mali_mem_swap_idx_range_free(index + i, page_count - i);
			return _MALI_OSK_ERR_FAULT;
		}

		m_page->swap_it->idx = index + i;

		ret = mali_mem_swap_in_page_node(m_page);

		if (MALI_FALSE == ret) {
			MALI_DEBUG_PRINT_ERROR(("SWAP Mem: Allocate new page from SHMEM file failed."));
			_mali_mem_swap_page_node_free(m_page);
			mali_mem_swap_idx_range_free(index + i + 1, page_count - i - 1);

			swap_mem->count = i;
			mali_mem_swap_free(swap_mem);
			return _MALI_OSK_ERR_NOMEM;
		}

		list_add_tail(&m_page->list, &swap_mem->pages);
	}

	system_free_size = global_zone_page_state(NR_FREE_PAGES) * PAGE_SIZE;

	if ((system_free_size < mali_mem_swap_out_threshold_value)
	    && (mem_backend_swapped_pool_size > (mali_mem_swap_out_threshold_value >> 2))
	    && mali_utilization_enabled()) {
		_mali_osk_wq_schedule_work(mali_mem_swap_out_workq);
	}

	*bkend_idx = index;
	return 0;
}

void mali_mem_swap_mali_unmap(mali_mem_allocation *alloc)
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


/* Insert these pages from shmem to mali page table*/
_mali_osk_errcode_t mali_mem_swap_mali_map(mali_mem_swap *swap_mem, struct mali_session_data *session, u32 vaddr, u32 props)
{
	struct mali_page_directory *pagedir = session->page_directory;
	struct mali_page_node *m_page;
	dma_addr_t phys;
	u32 virt = vaddr;
	u32 prop = props;

	list_for_each_entry(m_page, &swap_mem->pages, list) {
		MALI_DEBUG_ASSERT(NULL != m_page->swap_it->page);
		phys = m_page->swap_it->dma_addr;

		mali_mmu_pagedir_update(pagedir, virt, phys, MALI_MMU_PAGE_SIZE, prop);
		virt += MALI_MMU_PAGE_SIZE;
	}

	return _MALI_OSK_ERR_OK;
}

int mali_mem_swap_in_pages(struct mali_pp_job *job)
{
	u32 num_memory_cookies;
	struct mali_session_data *session;
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_allocation *mali_alloc = NULL;
	mali_mem_backend *mem_bkend = NULL;
	struct mali_page_node *m_page;
	mali_bool swap_in_success = MALI_TRUE;
	int i;

	MALI_DEBUG_ASSERT_POINTER(job);

	num_memory_cookies = mali_pp_job_num_memory_cookies(job);
	session = mali_pp_job_get_session(job);

	MALI_DEBUG_ASSERT_POINTER(session);

	for (i = 0; i < num_memory_cookies; i++) {

		u32 mali_addr  = mali_pp_job_get_memory_cookie(job, i);

		mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_addr, 0);
		if (NULL == mali_vma_node) {
			job->memory_cookies[i] = MALI_SWAP_INVALIDATE_MALI_ADDRESS;
			swap_in_success = MALI_FALSE;
			MALI_PRINT_ERROR(("SWAP Mem: failed to find mali_vma_node through Mali address: 0x%08x.\n", mali_addr));
			continue;
		}

		mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
		MALI_DEBUG_ASSERT(NULL != mali_alloc);

		if (MALI_MEM_SWAP != mali_alloc->type &&
		    MALI_MEM_COW != mali_alloc->type) {
			continue;
		}

		/* Get backend memory & Map on GPU */
		mutex_lock(&mali_idr_mutex);
		mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle);
		mutex_unlock(&mali_idr_mutex);
		MALI_DEBUG_ASSERT(NULL != mem_bkend);

		/* We neednot hold backend's lock here, race safe.*/
		if ((MALI_MEM_COW == mem_bkend->type) &&
		    (!(mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED))) {
			continue;
		}

		mutex_lock(&mem_bkend->mutex);

		/* When swap_in_success is MALI_FALSE, it means this job has memory backend that could not be swapped in,
		 * and it will be aborted in mali scheduler, so here, we just mark those memory cookies which
		 * should not be swapped out when delete job to invalide */
		if (MALI_FALSE == swap_in_success) {
			job->memory_cookies[i] = MALI_SWAP_INVALIDATE_MALI_ADDRESS;
			mutex_unlock(&mem_bkend->mutex);
			continue;
		}

		/* Before swap in, checking if this memory backend has been swapped in by the latest flushed jobs. */
		++mem_bkend->using_count;

		if (1 < mem_bkend->using_count) {
			MALI_DEBUG_ASSERT(MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN != (MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN & mem_bkend->flags));
			mutex_unlock(&mem_bkend->mutex);
			continue;
		}

		if (MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN != (MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN & mem_bkend->flags)) {
			mutex_unlock(&mem_bkend->mutex);
			continue;
		}


		list_for_each_entry(m_page, &mem_bkend->swap_mem.pages, list) {
			if (MALI_FALSE == mali_mem_swap_in_page_node(m_page)) {
				/* Don't have enough memory to swap in page, so release pages have already been swapped
				 * in and then mark this pp job to be fail. */
				mali_mem_swap_unlock_partial_locked_mem_backend(mem_bkend, m_page);
				swap_in_success = MALI_FALSE;
				break;
			}
		}

		if (swap_in_success) {
#ifdef MALI_MEM_SWAP_TRACKING
			mem_backend_swapped_unlock_size -= mem_bkend->size;
#endif
			_mali_osk_mutex_wait(session->memory_lock);
			mali_mem_swap_mali_map(&mem_bkend->swap_mem, session, mali_alloc->mali_mapping.addr, mali_alloc->mali_mapping.properties);
			_mali_osk_mutex_signal(session->memory_lock);

			/* Remove the unlock flag from mem backend flags, mark this backend has been swapped in. */
			mem_bkend->flags &= ~(MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN);
			mutex_unlock(&mem_bkend->mutex);
		} else {
			--mem_bkend->using_count;
			/* Marking that this backend is not swapped in, need not to be processed anymore. */
			job->memory_cookies[i] = MALI_SWAP_INVALIDATE_MALI_ADDRESS;
			mutex_unlock(&mem_bkend->mutex);
		}
	}

	job->swap_status = swap_in_success ? MALI_SWAP_IN_SUCC : MALI_SWAP_IN_FAIL;

	return _MALI_OSK_ERR_OK;
}

int mali_mem_swap_out_pages(struct mali_pp_job *job)
{
	u32 num_memory_cookies;
	struct mali_session_data *session;
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_allocation *mali_alloc = NULL;
	mali_mem_backend *mem_bkend = NULL;
	int i;

	MALI_DEBUG_ASSERT_POINTER(job);

	num_memory_cookies = mali_pp_job_num_memory_cookies(job);
	session = mali_pp_job_get_session(job);

	MALI_DEBUG_ASSERT_POINTER(session);


	for (i = 0; i < num_memory_cookies; i++) {
		u32 mali_addr  = mali_pp_job_get_memory_cookie(job, i);

		if (MALI_SWAP_INVALIDATE_MALI_ADDRESS == mali_addr) {
			continue;
		}

		mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_addr, 0);

		if (NULL == mali_vma_node) {
			MALI_PRINT_ERROR(("SWAP Mem: failed to find mali_vma_node through Mali address: 0x%08x.\n", mali_addr));
			continue;
		}

		mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
		MALI_DEBUG_ASSERT(NULL != mali_alloc);

		if (MALI_MEM_SWAP != mali_alloc->type &&
		    MALI_MEM_COW != mali_alloc->type) {
			continue;
		}

		mutex_lock(&mali_idr_mutex);
		mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle);
		mutex_unlock(&mali_idr_mutex);
		MALI_DEBUG_ASSERT(NULL != mem_bkend);

		/* We neednot hold backend's lock here, race safe.*/
		if ((MALI_MEM_COW == mem_bkend->type) &&
		    (!(mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED))) {
			continue;
		}

		mutex_lock(&mem_bkend->mutex);

		MALI_DEBUG_ASSERT(0 < mem_bkend->using_count);

		/* Reducing the using_count of mem backend means less pp job are using this memory backend,
		 * if this count get to zero, it means no pp job is using it now, could put it to swap out list. */
		--mem_bkend->using_count;

		if (0 < mem_bkend->using_count) {
			mutex_unlock(&mem_bkend->mutex);
			continue;
		}
		mutex_unlock(&mem_bkend->mutex);

		mali_memory_swap_list_backend_add(mem_bkend);
	}

	return _MALI_OSK_ERR_OK;
}

int mali_mem_swap_allocate_page_on_demand(mali_mem_backend *mem_bkend, u32 offset, struct page **pagep)
{
	struct mali_page_node *m_page, *found_node = NULL;
	struct page *found_page;
	mali_mem_swap *swap = NULL;
	mali_mem_cow *cow = NULL;
	dma_addr_t dma_addr;
	u32 i = 0;

	if (MALI_MEM_SWAP == mem_bkend->type) {
		swap = &mem_bkend->swap_mem;
		list_for_each_entry(m_page, &swap->pages, list) {
			if (i == offset) {
				found_node = m_page;
				break;
			}
			i++;
		}
	} else {
		MALI_DEBUG_ASSERT(MALI_MEM_COW == mem_bkend->type);
		MALI_DEBUG_ASSERT(MALI_MEM_BACKEND_FLAG_SWAP_COWED == (MALI_MEM_BACKEND_FLAG_SWAP_COWED & mem_bkend->flags));

		cow = &mem_bkend->cow_mem;
		list_for_each_entry(m_page, &cow->pages, list) {
			if (i == offset) {
				found_node = m_page;
				break;
			}
			i++;
		}
	}

	if (NULL == found_node) {
		return _MALI_OSK_ERR_FAULT;
	}

	found_page = shmem_read_mapping_page(global_swap_space, found_node->swap_it->idx);

	if (!IS_ERR(found_page)) {
		lock_page(found_page);
		dma_addr = dma_map_page(&mali_platform_device->dev, found_page,
					0, _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);
		dma_unmap_page(&mali_platform_device->dev, dma_addr,
			       _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);

		*pagep = found_page;
	} else {
		return _MALI_OSK_ERR_NOMEM;
	}

	return _MALI_OSK_ERR_OK;
}

int mali_mem_swap_cow_page_on_demand(mali_mem_backend *mem_bkend, u32 offset, struct page **pagep)
{
	struct mali_page_node *m_page, *found_node = NULL, *new_node = NULL;
	mali_mem_cow *cow = NULL;
	u32 i = 0;

	MALI_DEBUG_ASSERT(MALI_MEM_COW == mem_bkend->type);
	MALI_DEBUG_ASSERT(MALI_MEM_BACKEND_FLAG_SWAP_COWED == (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED));
	MALI_DEBUG_ASSERT(MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN == (MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN & mem_bkend->flags));
	MALI_DEBUG_ASSERT(!mali_memory_swap_backend_in_swapped_pool(mem_bkend));

	cow = &mem_bkend->cow_mem;
	list_for_each_entry(m_page, &cow->pages, list) {
		if (i == offset) {
			found_node = m_page;
			break;
		}
		i++;
	}

	if (NULL == found_node) {
		return _MALI_OSK_ERR_FAULT;
	}

	new_node = _mali_mem_swap_page_node_allocate();

	if (NULL == new_node) {
		return _MALI_OSK_ERR_FAULT;
	}

	new_node->swap_it->idx = mali_mem_swap_idx_alloc();

	if (_MALI_OSK_BITMAP_INVALIDATE_INDEX == new_node->swap_it->idx) {
		MALI_DEBUG_PRINT(1, ("Failed to allocate swap index in swap CoW on demand.\n"));
		kfree(new_node->swap_it);
		kfree(new_node);
		return _MALI_OSK_ERR_FAULT;
	}

	if (MALI_FALSE == mali_mem_swap_in_page_node(new_node)) {
		_mali_mem_swap_page_node_free(new_node);
		return _MALI_OSK_ERR_FAULT;
	}

	/* swap in found node for copy in kernel. */
	if (MALI_FALSE == mali_mem_swap_in_page_node(found_node)) {
		mali_mem_swap_out_page_node(new_node);
		_mali_mem_swap_page_node_free(new_node);
		return _MALI_OSK_ERR_FAULT;
	}

	_mali_mem_cow_copy_page(found_node, new_node);

	list_replace(&found_node->list, &new_node->list);

	if (1 != _mali_page_node_get_ref_count(found_node)) {
		atomic_add(1, &mem_bkend->mali_allocation->session->mali_mem_allocated_pages);
		if (atomic_read(&mem_bkend->mali_allocation->session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE > mem_bkend->mali_allocation->session->max_mali_mem_allocated_size) {
			mem_bkend->mali_allocation->session->max_mali_mem_allocated_size = atomic_read(&mem_bkend->mali_allocation->session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE;
		}
		mem_bkend->cow_mem.change_pages_nr++;
	}

	mali_mem_swap_out_page_node(found_node);
	_mali_mem_swap_page_node_free(found_node);

	/* When swap in the new page node, we have called dma_map_page for this page.\n */
	dma_unmap_page(&mali_platform_device->dev, new_node->swap_it->dma_addr,
		       _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);

	lock_page(new_node->swap_it->page);

	*pagep = new_node->swap_it->page;

	return _MALI_OSK_ERR_OK;
}

#ifdef MALI_MEM_SWAP_TRACKING
void mali_mem_swap_tracking(u32 *swap_pool_size, u32 *unlock_size)
{
	*swap_pool_size = mem_backend_swapped_pool_size;
	*unlock_size =  mem_backend_swapped_unlock_size;
}
#endif

/*
 * Copyright (C) 2013-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <asm/cacheflush.h>
#include <linux/sched.h>
#ifdef CONFIG_ARM
#include <asm/outercache.h>
#endif
#include <asm/dma-mapping.h>

#include "mali_memory.h"
#include "mali_kernel_common.h"
#include "mali_uk_types.h"
#include "mali_osk.h"
#include "mali_kernel_linux.h"
#include "mali_memory_cow.h"
#include "mali_memory_block_alloc.h"
#include "mali_memory_swap_alloc.h"

/**
* allocate pages for COW backend and flush cache
*/
static struct page *mali_mem_cow_alloc_page(void)

{
	mali_mem_os_mem os_mem;
	struct mali_page_node *node;
	struct page *new_page;

	int ret = 0;
	/* allocate pages from os mem */
	ret = mali_mem_os_alloc_pages(&os_mem, _MALI_OSK_MALI_PAGE_SIZE);

	if (ret) {
		return NULL;
	}

	MALI_DEBUG_ASSERT(1 == os_mem.count);

	node = _MALI_OSK_CONTAINER_OF(os_mem.pages.next, struct mali_page_node, list);
	new_page = node->page;
	node->page = NULL;
	list_del(&node->list);
	kfree(node);

	return new_page;
}


static struct list_head *_mali_memory_cow_get_node_list(mali_mem_backend *target_bk,
		u32 target_offset,
		u32 target_size)
{
	MALI_DEBUG_ASSERT(MALI_MEM_OS == target_bk->type || MALI_MEM_COW == target_bk->type ||
			  MALI_MEM_BLOCK == target_bk->type || MALI_MEM_SWAP == target_bk->type);

	if (MALI_MEM_OS == target_bk->type) {
		MALI_DEBUG_ASSERT(&target_bk->os_mem);
		MALI_DEBUG_ASSERT(((target_size + target_offset) / _MALI_OSK_MALI_PAGE_SIZE) <= target_bk->os_mem.count);
		return &target_bk->os_mem.pages;
	} else if (MALI_MEM_COW == target_bk->type) {
		MALI_DEBUG_ASSERT(&target_bk->cow_mem);
		MALI_DEBUG_ASSERT(((target_size + target_offset) / _MALI_OSK_MALI_PAGE_SIZE) <= target_bk->cow_mem.count);
		return  &target_bk->cow_mem.pages;
	} else if (MALI_MEM_BLOCK == target_bk->type) {
		MALI_DEBUG_ASSERT(&target_bk->block_mem);
		MALI_DEBUG_ASSERT(((target_size + target_offset) / _MALI_OSK_MALI_PAGE_SIZE) <= target_bk->block_mem.count);
		return  &target_bk->block_mem.pfns;
	} else if (MALI_MEM_SWAP == target_bk->type) {
		MALI_DEBUG_ASSERT(&target_bk->swap_mem);
		MALI_DEBUG_ASSERT(((target_size + target_offset) / _MALI_OSK_MALI_PAGE_SIZE) <= target_bk->swap_mem.count);
		return  &target_bk->swap_mem.pages;
	}

	return NULL;
}

/**
* Do COW for os memory - support do COW for memory from bank memory
* The range_start/size can be zero, which means it will call cow_modify_range
* latter.
* This function allocate new pages for COW backend from os mem for a modified range
* It will keep the page which not in the modified range and Add ref to it
*
* @target_bk - target allocation's backend(the allocation need to do COW)
* @target_offset - the offset in target allocation to do COW(for support COW  a memory allocated from memory_bank, 4K align)
* @target_size - size of target allocation to do COW (for support memory bank)
* @backend -COW backend
* @range_start - offset of modified range (4K align)
* @range_size - size of modified range
*/
_mali_osk_errcode_t mali_memory_cow_os_memory(mali_mem_backend *target_bk,
		u32 target_offset,
		u32 target_size,
		mali_mem_backend *backend,
		u32 range_start,
		u32 range_size)
{
	mali_mem_cow *cow = &backend->cow_mem;
	struct mali_page_node *m_page, *m_tmp, *page_node;
	int target_page = 0;
	struct page *new_page;
	struct list_head *pages = NULL;

	pages = _mali_memory_cow_get_node_list(target_bk, target_offset, target_size);

	if (NULL == pages) {
		MALI_DEBUG_PRINT_ERROR(("No memory page  need to cow ! \n"));
		return _MALI_OSK_ERR_FAULT;
	}

	MALI_DEBUG_ASSERT(0 == cow->count);

	INIT_LIST_HEAD(&cow->pages);
	mutex_lock(&target_bk->mutex);
	list_for_each_entry_safe(m_page, m_tmp, pages, list) {
		/* add page from (target_offset,target_offset+size) to cow backend */
		if ((target_page >= target_offset / _MALI_OSK_MALI_PAGE_SIZE) &&
		    (target_page < ((target_size + target_offset) / _MALI_OSK_MALI_PAGE_SIZE))) {

			/* allocate a new page node, alway use OS memory for COW */
			page_node = _mali_page_node_allocate(MALI_PAGE_NODE_OS);

			if (NULL == page_node) {
				mutex_unlock(&target_bk->mutex);
				goto error;
			}

			INIT_LIST_HEAD(&page_node->list);

			/* check if in the modified range*/
			if ((cow->count >= range_start / _MALI_OSK_MALI_PAGE_SIZE) &&
			    (cow->count < (range_start + range_size) / _MALI_OSK_MALI_PAGE_SIZE)) {
				/* need to allocate a new page */
				/* To simplify the case, All COW memory is allocated from os memory ?*/
				new_page = mali_mem_cow_alloc_page();

				if (NULL == new_page) {
					kfree(page_node);
					mutex_unlock(&target_bk->mutex);
					goto error;
				}

				_mali_page_node_add_page(page_node, new_page);
			} else {
				/*Add Block memory case*/
				if (m_page->type != MALI_PAGE_NODE_BLOCK) {
					_mali_page_node_add_page(page_node, m_page->page);
				} else {
					page_node->type = MALI_PAGE_NODE_BLOCK;
					_mali_page_node_add_block_item(page_node, m_page->blk_it);
				}

				/* add ref to this page */
				_mali_page_node_ref(m_page);
			}

			/* add it to COW backend page list */
			list_add_tail(&page_node->list, &cow->pages);
			cow->count++;
		}
		target_page++;
	}
	mutex_unlock(&target_bk->mutex);
	return _MALI_OSK_ERR_OK;
error:
	mali_mem_cow_release(backend, MALI_FALSE);
	return _MALI_OSK_ERR_FAULT;
}

_mali_osk_errcode_t mali_memory_cow_swap_memory(mali_mem_backend *target_bk,
		u32 target_offset,
		u32 target_size,
		mali_mem_backend *backend,
		u32 range_start,
		u32 range_size)
{
	mali_mem_cow *cow = &backend->cow_mem;
	struct mali_page_node *m_page, *m_tmp, *page_node;
	int target_page = 0;
	struct mali_swap_item *swap_item;
	struct list_head *pages = NULL;

	pages = _mali_memory_cow_get_node_list(target_bk, target_offset, target_size);
	if (NULL == pages) {
		MALI_DEBUG_PRINT_ERROR(("No swap memory page need to cow ! \n"));
		return _MALI_OSK_ERR_FAULT;
	}

	MALI_DEBUG_ASSERT(0 == cow->count);

	INIT_LIST_HEAD(&cow->pages);
	mutex_lock(&target_bk->mutex);

	backend->flags |= MALI_MEM_BACKEND_FLAG_UNSWAPPED_IN;

	list_for_each_entry_safe(m_page, m_tmp, pages, list) {
		/* add page from (target_offset,target_offset+size) to cow backend */
		if ((target_page >= target_offset / _MALI_OSK_MALI_PAGE_SIZE) &&
		    (target_page < ((target_size + target_offset) / _MALI_OSK_MALI_PAGE_SIZE))) {

			/* allocate a new page node, use swap memory for COW memory swap cowed flag. */
			page_node = _mali_page_node_allocate(MALI_PAGE_NODE_SWAP);

			if (NULL == page_node) {
				mutex_unlock(&target_bk->mutex);
				goto error;
			}

			/* check if in the modified range*/
			if ((cow->count >= range_start / _MALI_OSK_MALI_PAGE_SIZE) &&
			    (cow->count < (range_start + range_size) / _MALI_OSK_MALI_PAGE_SIZE)) {
				/* need to allocate a new page */
				/* To simplify the case, All COW memory is allocated from os memory ?*/
				swap_item = mali_mem_swap_alloc_swap_item();

				if (NULL == swap_item) {
					kfree(page_node);
					mutex_unlock(&target_bk->mutex);
					goto error;
				}

				swap_item->idx = mali_mem_swap_idx_alloc();

				if (_MALI_OSK_BITMAP_INVALIDATE_INDEX == swap_item->idx) {
					MALI_DEBUG_PRINT(1, ("Failed to allocate swap index in swap CoW.\n"));
					kfree(page_node);
					kfree(swap_item);
					mutex_unlock(&target_bk->mutex);
					goto error;
				}

				_mali_page_node_add_swap_item(page_node, swap_item);
			} else {
				_mali_page_node_add_swap_item(page_node, m_page->swap_it);

				/* add ref to this page */
				_mali_page_node_ref(m_page);
			}

			list_add_tail(&page_node->list, &cow->pages);
			cow->count++;
		}
		target_page++;
	}
	mutex_unlock(&target_bk->mutex);

	return _MALI_OSK_ERR_OK;
error:
	mali_mem_swap_release(backend, MALI_FALSE);
	return _MALI_OSK_ERR_FAULT;

}


_mali_osk_errcode_t _mali_mem_put_page_node(mali_page_node *node)
{
	if (node->type == MALI_PAGE_NODE_OS) {
		return mali_mem_os_put_page(node->page);
	} else if (node->type == MALI_PAGE_NODE_BLOCK) {
		return mali_mem_block_unref_node(node);
	} else if (node->type == MALI_PAGE_NODE_SWAP) {
		return _mali_mem_swap_put_page_node(node);
	} else
		MALI_DEBUG_ASSERT(0);
	return _MALI_OSK_ERR_FAULT;
}


/**
* Modify a range of a exist COW backend
* @backend -COW backend
* @range_start - offset of modified range (4K align)
* @range_size - size of modified range(in byte)
*/
_mali_osk_errcode_t mali_memory_cow_modify_range(mali_mem_backend *backend,
		u32 range_start,
		u32 range_size)
{
	mali_mem_allocation *alloc = NULL;
	struct mali_session_data *session;
	mali_mem_cow *cow = &backend->cow_mem;
	struct mali_page_node *m_page, *m_tmp;
	LIST_HEAD(pages);
	struct page *new_page;
	u32 count = 0;
	s32 change_pages_nr = 0;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_OK;

	if (range_start % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	if (range_size % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	alloc = backend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);

	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);

	MALI_DEBUG_ASSERT(MALI_MEM_COW == backend->type);
	MALI_DEBUG_ASSERT(((range_start + range_size) / _MALI_OSK_MALI_PAGE_SIZE) <= cow->count);

	mutex_lock(&backend->mutex);

	/* free pages*/
	list_for_each_entry_safe(m_page, m_tmp, &cow->pages, list) {

		/* check if in the modified range*/
		if ((count >= range_start / _MALI_OSK_MALI_PAGE_SIZE) &&
		    (count < (range_start + range_size) / _MALI_OSK_MALI_PAGE_SIZE)) {
			if (MALI_PAGE_NODE_SWAP != m_page->type) {
				new_page = mali_mem_cow_alloc_page();

				if (NULL == new_page) {
					goto error;
				}
				if (1 != _mali_page_node_get_ref_count(m_page))
					change_pages_nr++;
				/* unref old page*/
				_mali_osk_mutex_wait(session->cow_lock);
				if (_mali_mem_put_page_node(m_page)) {
					__free_page(new_page);
					_mali_osk_mutex_signal(session->cow_lock);
					goto error;
				}
				_mali_osk_mutex_signal(session->cow_lock);
				/* add new page*/
				/* always use OS for COW*/
				m_page->type = MALI_PAGE_NODE_OS;
				_mali_page_node_add_page(m_page, new_page);
			} else {
				struct mali_swap_item *swap_item;

				swap_item = mali_mem_swap_alloc_swap_item();

				if (NULL == swap_item) {
					goto error;
				}

				swap_item->idx = mali_mem_swap_idx_alloc();

				if (_MALI_OSK_BITMAP_INVALIDATE_INDEX == swap_item->idx) {
					MALI_DEBUG_PRINT(1, ("Failed to allocate swap index in swap CoW modify range.\n"));
					kfree(swap_item);
					goto error;
				}

				if (1 != _mali_page_node_get_ref_count(m_page)) {
					change_pages_nr++;
				}

				if (_mali_mem_put_page_node(m_page)) {
					mali_mem_swap_free_swap_item(swap_item);
					goto error;
				}

				_mali_page_node_add_swap_item(m_page, swap_item);
			}
		}
		count++;
	}
	cow->change_pages_nr  = change_pages_nr;

	MALI_DEBUG_ASSERT(MALI_MEM_COW == alloc->type);

	/* ZAP cpu mapping(modified range), and do cpu mapping here if need */
	if (NULL != alloc->cpu_mapping.vma) {
		MALI_DEBUG_ASSERT(0 != alloc->backend_handle);
		MALI_DEBUG_ASSERT(NULL != alloc->cpu_mapping.vma);
		MALI_DEBUG_ASSERT(alloc->cpu_mapping.vma->vm_end - alloc->cpu_mapping.vma->vm_start >= range_size);

		if (MALI_MEM_BACKEND_FLAG_SWAP_COWED != (backend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED)) {
			zap_vma_ptes(alloc->cpu_mapping.vma, alloc->cpu_mapping.vma->vm_start + range_start, range_size);

			ret = mali_mem_cow_cpu_map_pages_locked(backend, alloc->cpu_mapping.vma, alloc->cpu_mapping.vma->vm_start  + range_start, range_size / _MALI_OSK_MALI_PAGE_SIZE);

			if (unlikely(ret != _MALI_OSK_ERR_OK)) {
				MALI_DEBUG_PRINT(2, ("mali_memory_cow_modify_range: cpu mapping failed !\n"));
				ret =  _MALI_OSK_ERR_FAULT;
			}
		} else {
			/* used to trigger page fault for swappable cowed memory. */
			alloc->cpu_mapping.vma->vm_flags |= VM_PFNMAP;
			alloc->cpu_mapping.vma->vm_flags |= VM_MIXEDMAP;

			zap_vma_ptes(alloc->cpu_mapping.vma, alloc->cpu_mapping.vma->vm_start + range_start, range_size);
			/* delete this flag to let swappble is ummapped regard to stauct page not page frame. */
			alloc->cpu_mapping.vma->vm_flags &= ~VM_PFNMAP;
			alloc->cpu_mapping.vma->vm_flags &= ~VM_MIXEDMAP;
		}
	}

error:
	mutex_unlock(&backend->mutex);
	return ret;

}


/**
* Allocate pages for COW backend
* @alloc  -allocation for COW allocation
* @target_bk - target allocation's backend(the allocation need to do COW)
* @target_offset - the offset in target allocation to do COW(for support COW  a memory allocated from memory_bank, 4K align)
* @target_size - size of target allocation to do COW (for support memory bank)(in byte)
* @backend -COW backend
* @range_start - offset of modified range (4K align)
* @range_size - size of modified range(in byte)
*/
_mali_osk_errcode_t mali_memory_do_cow(mali_mem_backend *target_bk,
				       u32 target_offset,
				       u32 target_size,
				       mali_mem_backend *backend,
				       u32 range_start,
				       u32 range_size)
{
	struct mali_session_data *session = backend->mali_allocation->session;

	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_INVALID_ARGS);

	/* size & offset must be a multiple of the system page size */
	if (target_size % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	if (range_size % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	if (target_offset % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	if (range_start % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	/* check backend type */
	MALI_DEBUG_ASSERT(MALI_MEM_COW == backend->type);

	switch (target_bk->type) {
	case MALI_MEM_OS:
	case MALI_MEM_BLOCK:
		return mali_memory_cow_os_memory(target_bk, target_offset, target_size, backend, range_start, range_size);
		break;
	case MALI_MEM_COW:
		if (backend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED) {
			return mali_memory_cow_swap_memory(target_bk, target_offset, target_size, backend, range_start, range_size);
		} else {
			return mali_memory_cow_os_memory(target_bk, target_offset, target_size, backend, range_start, range_size);
		}
		break;
	case MALI_MEM_SWAP:
		return mali_memory_cow_swap_memory(target_bk, target_offset, target_size, backend, range_start, range_size);
		break;
	case MALI_MEM_EXTERNAL:
		/*NOT support yet*/
		MALI_DEBUG_PRINT_ERROR(("External physical memory not supported ! \n"));
		return _MALI_OSK_ERR_UNSUPPORTED;
		break;
	case MALI_MEM_DMA_BUF:
		/*NOT support yet*/
		MALI_DEBUG_PRINT_ERROR(("DMA buffer not supported ! \n"));
		return _MALI_OSK_ERR_UNSUPPORTED;
		break;
	case MALI_MEM_UMP:
		/*NOT support yet*/
		MALI_DEBUG_PRINT_ERROR(("UMP buffer not supported ! \n"));
		return _MALI_OSK_ERR_UNSUPPORTED;
		break;
	default:
		/*Not support yet*/
		MALI_DEBUG_PRINT_ERROR(("Invalid memory type not supported ! \n"));
		return _MALI_OSK_ERR_UNSUPPORTED;
		break;
	}
	return _MALI_OSK_ERR_OK;
}


/**
* Map COW backend memory to mali
* Support OS/BLOCK for mali_page_node
*/
int mali_mem_cow_mali_map(mali_mem_backend *mem_bkend, u32 range_start, u32 range_size)
{
	mali_mem_allocation *cow_alloc;
	struct mali_page_node *m_page;
	struct mali_session_data *session;
	struct mali_page_directory *pagedir;
	u32 virt, start;

	cow_alloc = mem_bkend->mali_allocation;
	virt = cow_alloc->mali_vma_node.vm_node.start;
	start = virt;

	MALI_DEBUG_ASSERT_POINTER(mem_bkend);
	MALI_DEBUG_ASSERT(MALI_MEM_COW == mem_bkend->type);
	MALI_DEBUG_ASSERT_POINTER(cow_alloc);

	session = cow_alloc->session;
	pagedir = session->page_directory;
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_INVALID_ARGS);
	list_for_each_entry(m_page, &mem_bkend->cow_mem.pages, list) {
		if ((virt - start >= range_start) && (virt - start < range_start + range_size)) {
			dma_addr_t phys = _mali_page_node_get_dma_addr(m_page);
#if defined(CONFIG_ARCH_DMA_ADDR_T_64BIT)
			MALI_DEBUG_ASSERT(0 == (phys >> 32));
#endif
			mali_mmu_pagedir_update(pagedir, virt, (mali_dma_addr)phys,
						MALI_MMU_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);
		}
		virt += MALI_MMU_PAGE_SIZE;
	}
	return 0;
}

/**
* Map COW backend to cpu
* support OS/BLOCK memory
*/
int mali_mem_cow_cpu_map(mali_mem_backend *mem_bkend, struct vm_area_struct *vma)
{
	mali_mem_cow *cow = &mem_bkend->cow_mem;
	struct mali_page_node *m_page;
	int ret;
	unsigned long addr = vma->vm_start;
	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_COW);

	list_for_each_entry(m_page, &cow->pages, list) {
		/* We should use vm_insert_page, but it does a dcache
		 * flush which makes it way slower than remap_pfn_range or vm_insert_pfn.
		ret = vm_insert_page(vma, addr, page);
		*/
		ret = vm_insert_pfn(vma, addr, _mali_page_node_get_pfn(m_page));

		if (unlikely(0 != ret)) {
			return ret;
		}
		addr += _MALI_OSK_MALI_PAGE_SIZE;
	}

	return 0;
}

/**
* Map some pages(COW backend) to CPU vma@vaddr
*@ mem_bkend - COW backend
*@ vma
*@ vaddr -start CPU vaddr mapped to
*@ num - max number of pages to map to CPU vaddr
*/
_mali_osk_errcode_t mali_mem_cow_cpu_map_pages_locked(mali_mem_backend *mem_bkend,
		struct vm_area_struct *vma,
		unsigned long vaddr,
		int num)
{
	mali_mem_cow *cow = &mem_bkend->cow_mem;
	struct mali_page_node *m_page;
	int ret;
	int offset;
	int count ;
	unsigned long vstart = vma->vm_start;
	count = 0;
	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_COW);
	MALI_DEBUG_ASSERT(0 == vaddr % _MALI_OSK_MALI_PAGE_SIZE);
	MALI_DEBUG_ASSERT(0 == vstart % _MALI_OSK_MALI_PAGE_SIZE);
	offset = (vaddr - vstart) / _MALI_OSK_MALI_PAGE_SIZE;

	list_for_each_entry(m_page, &cow->pages, list) {
		if ((count >= offset) && (count < offset + num)) {
			ret = vm_insert_pfn(vma, vaddr, _mali_page_node_get_pfn(m_page));

			if (unlikely(0 != ret)) {
				if (count == offset) {
					return _MALI_OSK_ERR_FAULT;
				} else {
					/* ret is EBUSY when page isn't in modify range, but now it's OK*/
					return _MALI_OSK_ERR_OK;
				}
			}
			vaddr += _MALI_OSK_MALI_PAGE_SIZE;
		}
		count++;
	}
	return _MALI_OSK_ERR_OK;
}

/**
* Release COW backend memory
* free it directly(put_page--unref page), not put into pool
*/
u32 mali_mem_cow_release(mali_mem_backend *mem_bkend, mali_bool is_mali_mapped)
{
	mali_mem_allocation *alloc;
	struct mali_session_data *session;
	u32 free_pages_nr = 0;
	MALI_DEBUG_ASSERT_POINTER(mem_bkend);
	MALI_DEBUG_ASSERT(MALI_MEM_COW == mem_bkend->type);
	alloc = mem_bkend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);

	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);

	if (MALI_MEM_BACKEND_FLAG_SWAP_COWED != (MALI_MEM_BACKEND_FLAG_SWAP_COWED & mem_bkend->flags)) {
		/* Unmap the memory from the mali virtual address space. */
		if (MALI_TRUE == is_mali_mapped)
			mali_mem_os_mali_unmap(alloc);
		/* free cow backend list*/
		_mali_osk_mutex_wait(session->cow_lock);
		free_pages_nr = mali_mem_os_free(&mem_bkend->cow_mem.pages, mem_bkend->cow_mem.count, MALI_TRUE);
		_mali_osk_mutex_signal(session->cow_lock);

		free_pages_nr += mali_mem_block_free_list(&mem_bkend->cow_mem.pages);

		MALI_DEBUG_ASSERT(list_empty(&mem_bkend->cow_mem.pages));
	} else {
		free_pages_nr = mali_mem_swap_release(mem_bkend, is_mali_mapped);
	}


	MALI_DEBUG_PRINT(4, ("COW Mem free : allocated size = 0x%x, free size = 0x%x\n", mem_bkend->cow_mem.count * _MALI_OSK_MALI_PAGE_SIZE,
			     free_pages_nr * _MALI_OSK_MALI_PAGE_SIZE));

	mem_bkend->cow_mem.count = 0;
	return free_pages_nr;
}


/* Dst node could os node or swap node. */
void _mali_mem_cow_copy_page(mali_page_node *src_node, mali_page_node *dst_node)
{
	void *dst, *src;
	struct page *dst_page;
	dma_addr_t dma_addr;

	MALI_DEBUG_ASSERT(src_node != NULL);
	MALI_DEBUG_ASSERT(dst_node != NULL);
	MALI_DEBUG_ASSERT(dst_node->type == MALI_PAGE_NODE_OS
			  || dst_node->type == MALI_PAGE_NODE_SWAP);

	if (dst_node->type == MALI_PAGE_NODE_OS) {
		dst_page = dst_node->page;
	} else {
		dst_page = dst_node->swap_it->page;
	}

	dma_unmap_page(&mali_platform_device->dev, _mali_page_node_get_dma_addr(dst_node),
		       _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);

	/* map it , and copy the content*/
	dst = kmap_atomic(dst_page);

	if (src_node->type == MALI_PAGE_NODE_OS ||
	    src_node->type == MALI_PAGE_NODE_SWAP) {
		struct page *src_page;

		if (src_node->type == MALI_PAGE_NODE_OS) {
			src_page = src_node->page;
		} else {
			src_page = src_node->swap_it->page;
		}

		/* Clear and invaliate cache */
		/* In ARM architecture, speculative read may pull stale data into L1 cache
		 * for kernel linear mapping page table. DMA_BIDIRECTIONAL could
		 * invalidate the L1 cache so that following read get the latest data
		*/
		dma_unmap_page(&mali_platform_device->dev, _mali_page_node_get_dma_addr(src_node),
			       _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);

		src = kmap_atomic(src_page);
		memcpy(dst, src , _MALI_OSK_MALI_PAGE_SIZE);
		kunmap_atomic(src);
		dma_addr = dma_map_page(&mali_platform_device->dev, src_page,
					0, _MALI_OSK_MALI_PAGE_SIZE, DMA_BIDIRECTIONAL);

		if (src_node->type == MALI_PAGE_NODE_SWAP) {
			src_node->swap_it->dma_addr = dma_addr;
		}
	} else if (src_node->type == MALI_PAGE_NODE_BLOCK) {
		/*
		* use ioremap to map src for BLOCK memory
		*/
		src = ioremap_nocache(_mali_page_node_get_dma_addr(src_node), _MALI_OSK_MALI_PAGE_SIZE);
		memcpy(dst, src , _MALI_OSK_MALI_PAGE_SIZE);
		iounmap(src);
	}
	kunmap_atomic(dst);
	dma_addr = dma_map_page(&mali_platform_device->dev, dst_page,
				0, _MALI_OSK_MALI_PAGE_SIZE, DMA_TO_DEVICE);

	if (dst_node->type == MALI_PAGE_NODE_SWAP) {
		dst_node->swap_it->dma_addr = dma_addr;
	}
}


/*
* allocate page on demand when CPU access it,
* THis used in page fault handler
*/
_mali_osk_errcode_t mali_mem_cow_allocate_on_demand(mali_mem_backend *mem_bkend, u32 offset_page)
{
	struct page *new_page = NULL;
	struct mali_page_node *new_node = NULL;
	int i = 0;
	struct mali_page_node *m_page, *found_node = NULL;
	struct  mali_session_data *session = NULL;
	mali_mem_cow *cow = &mem_bkend->cow_mem;
	MALI_DEBUG_ASSERT(MALI_MEM_COW == mem_bkend->type);
	MALI_DEBUG_ASSERT(offset_page < mem_bkend->size / _MALI_OSK_MALI_PAGE_SIZE);
	MALI_DEBUG_PRINT(4, ("mali_mem_cow_allocate_on_demand !, offset_page =0x%x\n", offset_page));

	/* allocate new page here */
	new_page = mali_mem_cow_alloc_page();
	if (!new_page)
		return _MALI_OSK_ERR_NOMEM;

	new_node = _mali_page_node_allocate(MALI_PAGE_NODE_OS);
	if (!new_node) {
		__free_page(new_page);
		return _MALI_OSK_ERR_NOMEM;
	}

	/* find the page in backend*/
	list_for_each_entry(m_page, &cow->pages, list) {
		if (i == offset_page) {
			found_node = m_page;
			break;
		}
		i++;
	}
	MALI_DEBUG_ASSERT(found_node);
	if (NULL == found_node) {
		__free_page(new_page);
		kfree(new_node);
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	_mali_page_node_add_page(new_node, new_page);

	/* Copy the src page's content to new page */
	_mali_mem_cow_copy_page(found_node, new_node);

	MALI_DEBUG_ASSERT_POINTER(mem_bkend->mali_allocation);
	session = mem_bkend->mali_allocation->session;
	MALI_DEBUG_ASSERT_POINTER(session);
	if (1 != _mali_page_node_get_ref_count(found_node)) {
		atomic_add(1, &session->mali_mem_allocated_pages);
		if (atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE > session->max_mali_mem_allocated_size) {
			session->max_mali_mem_allocated_size = atomic_read(&session->mali_mem_allocated_pages) * MALI_MMU_PAGE_SIZE;
		}
		mem_bkend->cow_mem.change_pages_nr++;
	}

	_mali_osk_mutex_wait(session->cow_lock);
	if (_mali_mem_put_page_node(found_node)) {
		__free_page(new_page);
		kfree(new_node);
		_mali_osk_mutex_signal(session->cow_lock);
		return _MALI_OSK_ERR_NOMEM;
	}
	_mali_osk_mutex_signal(session->cow_lock);

	list_replace(&found_node->list, &new_node->list);

	kfree(found_node);

	/* map to GPU side*/
	_mali_osk_mutex_wait(session->memory_lock);
	mali_mem_cow_mali_map(mem_bkend, offset_page * _MALI_OSK_MALI_PAGE_SIZE, _MALI_OSK_MALI_PAGE_SIZE);
	_mali_osk_mutex_signal(session->memory_lock);
	return _MALI_OSK_ERR_OK;
}

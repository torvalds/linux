/*
 * Copyright (C) 2010-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_memory.h"
#include "mali_memory_block_alloc.h"
#include "mali_osk.h"
#include <linux/mutex.h>


static mali_block_allocator *mali_mem_block_gobal_allocator = NULL;

unsigned long _mali_blk_item_get_phy_addr(mali_block_item *item)
{
	return (item->phy_addr & ~(MALI_BLOCK_REF_MASK));
}


unsigned long _mali_blk_item_get_pfn(mali_block_item *item)
{
	return (item->phy_addr / MALI_BLOCK_SIZE);
}


u32 mali_mem_block_get_ref_count(mali_page_node *node)
{
	MALI_DEBUG_ASSERT(node->type == MALI_PAGE_NODE_BLOCK);
	return (node->blk_it->phy_addr & MALI_BLOCK_REF_MASK);
}


/* Increase the refence count
* It not atomic, so it need to get sp_lock before call this function
*/

u32 mali_mem_block_add_ref(mali_page_node *node)
{
	MALI_DEBUG_ASSERT(node->type == MALI_PAGE_NODE_BLOCK);
	MALI_DEBUG_ASSERT(mali_mem_block_get_ref_count(node) < MALI_BLOCK_MAX_REF_COUNT);
	return (node->blk_it->phy_addr++ & MALI_BLOCK_REF_MASK);
}

/* Decase the refence count
* It not atomic, so it need to get sp_lock before call this function
*/
u32 mali_mem_block_dec_ref(mali_page_node *node)
{
	MALI_DEBUG_ASSERT(node->type == MALI_PAGE_NODE_BLOCK);
	MALI_DEBUG_ASSERT(mali_mem_block_get_ref_count(node) > 0);
	return (node->blk_it->phy_addr-- & MALI_BLOCK_REF_MASK);
}


static mali_block_allocator *mali_mem_block_allocator_create(u32 base_address, u32 size)
{
	mali_block_allocator *info;
	u32 usable_size;
	u32 num_blocks;
	mali_page_node *m_node;
	mali_block_item *mali_blk_items = NULL;
	int i = 0;

	usable_size = size & ~(MALI_BLOCK_SIZE - 1);
	MALI_DEBUG_PRINT(3, ("Mali block allocator create for region starting at 0x%08X length 0x%08X\n", base_address, size));
	MALI_DEBUG_PRINT(4, ("%d usable bytes\n", usable_size));
	num_blocks = usable_size / MALI_BLOCK_SIZE;
	MALI_DEBUG_PRINT(4, ("which becomes %d blocks\n", num_blocks));

	if (usable_size == 0) {
		MALI_DEBUG_PRINT(1, ("Memory block of size %d is unusable\n", size));
		return NULL;
	}

	info = _mali_osk_calloc(1, sizeof(mali_block_allocator));
	if (NULL != info) {
		INIT_LIST_HEAD(&info->free);
		spin_lock_init(&info->sp_lock);
		info->total_num = num_blocks;
		mali_blk_items = _mali_osk_calloc(1, sizeof(mali_block_item) * num_blocks);

		if (mali_blk_items) {
			info->items = mali_blk_items;
			/* add blocks(4k size) to free list*/
			for (i = 0 ; i < num_blocks ; i++) {
				/* add block information*/
				mali_blk_items[i].phy_addr = base_address + (i * MALI_BLOCK_SIZE);
				/* add  to free list */
				m_node = _mali_page_node_allocate(MALI_PAGE_NODE_BLOCK);
				if (m_node == NULL)
					goto fail;
				_mali_page_node_add_block_item(m_node, &(mali_blk_items[i]));
				list_add_tail(&m_node->list, &info->free);
				atomic_add(1, &info->free_num);
			}
			return info;
		}
	}
fail:
	mali_mem_block_allocator_destroy();
	return NULL;
}

void mali_mem_block_allocator_destroy(void)
{
	struct mali_page_node *m_page, *m_tmp;
	mali_block_allocator *info = mali_mem_block_gobal_allocator;
	MALI_DEBUG_ASSERT_POINTER(info);
	MALI_DEBUG_PRINT(4, ("Memory block destroy !\n"));

	if (NULL == info)
		return;

	list_for_each_entry_safe(m_page, m_tmp , &info->free, list) {
		MALI_DEBUG_ASSERT(m_page->type == MALI_PAGE_NODE_BLOCK);
		list_del(&m_page->list);
		kfree(m_page);
	}

	_mali_osk_free(info->items);
	_mali_osk_free(info);
}

u32 mali_mem_block_release(mali_mem_backend *mem_bkend)
{
	mali_mem_allocation *alloc = mem_bkend->mali_allocation;
	u32 free_pages_nr = 0;
	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_BLOCK);

	/* Unmap the memory from the mali virtual address space. */
	mali_mem_block_mali_unmap(alloc);
	mutex_lock(&mem_bkend->mutex);
	free_pages_nr = mali_mem_block_free(&mem_bkend->block_mem);
	mutex_unlock(&mem_bkend->mutex);
	return free_pages_nr;
}


int mali_mem_block_alloc(mali_mem_block_mem *block_mem, u32 size)
{
	struct mali_page_node *m_page, *m_tmp;
	size_t page_count = PAGE_ALIGN(size) / _MALI_OSK_MALI_PAGE_SIZE;
	mali_block_allocator *info = mali_mem_block_gobal_allocator;
	MALI_DEBUG_ASSERT_POINTER(info);

	MALI_DEBUG_PRINT(4, ("BLOCK Mem: Allocate size = 0x%x\n", size));
	/*do some init */
	INIT_LIST_HEAD(&block_mem->pfns);

	spin_lock(&info->sp_lock);
	/*check if have enough space*/
	if (atomic_read(&info->free_num) > page_count) {
		list_for_each_entry_safe(m_page, m_tmp , &info->free, list) {
			if (page_count > 0) {
				MALI_DEBUG_ASSERT(m_page->type == MALI_PAGE_NODE_BLOCK);
				MALI_DEBUG_ASSERT(mali_mem_block_get_ref_count(m_page) == 0);
				list_move(&m_page->list, &block_mem->pfns);
				block_mem->count++;
				atomic_dec(&info->free_num);
				_mali_page_node_ref(m_page);
			} else {
				break;
			}
			page_count--;
		}
	} else {
		/* can't allocate from BLOCK memory*/
		spin_unlock(&info->sp_lock);
		return -1;
	}

	spin_unlock(&info->sp_lock);
	return 0;
}

u32 mali_mem_block_free(mali_mem_block_mem *block_mem)
{
	u32 free_pages_nr = 0;

	free_pages_nr = mali_mem_block_free_list(&block_mem->pfns);
	MALI_DEBUG_PRINT(4, ("BLOCK Mem free : allocated size = 0x%x, free size = 0x%x\n", block_mem->count * _MALI_OSK_MALI_PAGE_SIZE,
			     free_pages_nr * _MALI_OSK_MALI_PAGE_SIZE));
	block_mem->count = 0;
	MALI_DEBUG_ASSERT(list_empty(&block_mem->pfns));

	return free_pages_nr;
}


u32 mali_mem_block_free_list(struct list_head *list)
{
	struct mali_page_node *m_page, *m_tmp;
	mali_block_allocator *info = mali_mem_block_gobal_allocator;
	u32 free_pages_nr = 0;

	if (info) {
		spin_lock(&info->sp_lock);
		list_for_each_entry_safe(m_page, m_tmp , list, list) {
			if (1 == _mali_page_node_get_ref_count(m_page)) {
				free_pages_nr++;
			}
			mali_mem_block_free_node(m_page);
		}
		spin_unlock(&info->sp_lock);
	}
	return free_pages_nr;
}

/* free the node,*/
void mali_mem_block_free_node(struct mali_page_node *node)
{
	mali_block_allocator *info = mali_mem_block_gobal_allocator;

	/* only handle BLOCK node */
	if (node->type == MALI_PAGE_NODE_BLOCK && info) {
		/*Need to make this atomic?*/
		if (1 == _mali_page_node_get_ref_count(node)) {
			/*Move to free list*/
			_mali_page_node_unref(node);
			list_move_tail(&node->list, &info->free);
			atomic_add(1, &info->free_num);
		} else {
			_mali_page_node_unref(node);
			list_del(&node->list);
			kfree(node);
		}
	}
}

/* unref the node, but not free it */
_mali_osk_errcode_t mali_mem_block_unref_node(struct mali_page_node *node)
{
	mali_block_allocator *info = mali_mem_block_gobal_allocator;
	mali_page_node *new_node;

	/* only handle BLOCK node */
	if (node->type == MALI_PAGE_NODE_BLOCK && info) {
		/*Need to make this atomic?*/
		if (1 == _mali_page_node_get_ref_count(node)) {
			/* allocate a  new node, Add to free list, keep the old node*/
			_mali_page_node_unref(node);
			new_node = _mali_page_node_allocate(MALI_PAGE_NODE_BLOCK);
			if (new_node) {
				memcpy(new_node, node, sizeof(mali_page_node));
				list_add(&new_node->list, &info->free);
				atomic_add(1, &info->free_num);
			} else
				return _MALI_OSK_ERR_FAULT;

		} else {
			_mali_page_node_unref(node);
		}
	}
	return _MALI_OSK_ERR_OK;
}


int mali_mem_block_mali_map(mali_mem_block_mem *block_mem, struct mali_session_data *session, u32 vaddr, u32 props)
{
	struct mali_page_directory *pagedir = session->page_directory;
	struct mali_page_node *m_page;
	dma_addr_t phys;
	u32 virt = vaddr;
	u32 prop = props;

	list_for_each_entry(m_page, &block_mem->pfns, list) {
		MALI_DEBUG_ASSERT(m_page->type == MALI_PAGE_NODE_BLOCK);
		phys = _mali_page_node_get_dma_addr(m_page);
#if defined(CONFIG_ARCH_DMA_ADDR_T_64BIT)
		/* Verify that the "physical" address is 32-bit and
		 * usable for Mali, when on a system with bus addresses
		 * wider than 32-bit. */
		MALI_DEBUG_ASSERT(0 == (phys >> 32));
#endif
		mali_mmu_pagedir_update(pagedir, virt, (mali_dma_addr)phys, MALI_MMU_PAGE_SIZE, prop);
		virt += MALI_MMU_PAGE_SIZE;
	}

	return 0;
}

void mali_mem_block_mali_unmap(mali_mem_allocation *alloc)
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


int mali_mem_block_cpu_map(mali_mem_backend *mem_bkend, struct vm_area_struct *vma)
{
	int ret;
	mali_mem_block_mem *block_mem = &mem_bkend->block_mem;
	unsigned long addr = vma->vm_start;
	struct mali_page_node *m_page;
	MALI_DEBUG_ASSERT(mem_bkend->type == MALI_MEM_BLOCK);

	list_for_each_entry(m_page, &block_mem->pfns, list) {
		MALI_DEBUG_ASSERT(m_page->type == MALI_PAGE_NODE_BLOCK);
		ret = vm_insert_pfn(vma, addr, _mali_page_node_get_pfn(m_page));

		if (unlikely(0 != ret)) {
			return -EFAULT;
		}
		addr += _MALI_OSK_MALI_PAGE_SIZE;

	}

	return 0;
}


_mali_osk_errcode_t mali_memory_core_resource_dedicated_memory(u32 start, u32 size)
{
	mali_block_allocator *allocator;

	/* Do the low level linux operation first */

	/* Request ownership of the memory */
	if (_MALI_OSK_ERR_OK != _mali_osk_mem_reqregion(start, size, "Dedicated Mali GPU memory")) {
		MALI_DEBUG_PRINT(1, ("Failed to request memory region for frame buffer (0x%08X - 0x%08X)\n", start, start + size - 1));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create generic block allocator object to handle it */
	allocator = mali_mem_block_allocator_create(start, size);

	if (NULL == allocator) {
		MALI_DEBUG_PRINT(1, ("Memory bank registration failed\n"));
		_mali_osk_mem_unreqregion(start, size);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	mali_mem_block_gobal_allocator = (mali_block_allocator *)allocator;

	return _MALI_OSK_ERR_OK;
}

mali_bool mali_memory_have_dedicated_memory(void)
{
	return mali_mem_block_gobal_allocator ? MALI_TRUE : MALI_FALSE;
}

u32 mali_mem_block_allocator_stat(void)
{
	mali_block_allocator *allocator = mali_mem_block_gobal_allocator;
	MALI_DEBUG_ASSERT_POINTER(allocator);

	return (allocator->total_num - atomic_read(&allocator->free_num)) * _MALI_OSK_MALI_PAGE_SIZE;
}

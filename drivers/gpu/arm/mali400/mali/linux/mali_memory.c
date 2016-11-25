/*
 * Copyright (C) 2013-2016 ARM Limited. All rights reserved.
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
#include <linux/platform_device.h>
#include <linux/idr.h>

#include "mali_osk.h"
#include "mali_executor.h"

#include "mali_memory.h"
#include "mali_memory_os_alloc.h"
#include "mali_memory_block_alloc.h"
#include "mali_memory_util.h"
#include "mali_memory_virtual.h"
#include "mali_memory_manager.h"
#include "mali_memory_cow.h"
#include "mali_memory_swap_alloc.h"
#include "mali_memory_defer_bind.h"
#if defined(CONFIG_DMA_SHARED_BUFFER)
#include "mali_memory_secure.h"
#endif

extern unsigned int mali_dedicated_mem_size;
extern unsigned int mali_shared_mem_size;

#define MALI_VM_NUM_FAULT_PREFETCH (0x8)

static void mali_mem_vma_open(struct vm_area_struct *vma)
{
	mali_mem_allocation *alloc = (mali_mem_allocation *)vma->vm_private_data;
	MALI_DEBUG_PRINT(4, ("Open called on vma %p\n", vma));

	/* If need to share the allocation, add ref_count here */
	mali_allocation_ref(alloc);
	return;
}
static void mali_mem_vma_close(struct vm_area_struct *vma)
{
	/* If need to share the allocation, unref ref_count here */
	mali_mem_allocation *alloc = (mali_mem_allocation *)vma->vm_private_data;

	mali_allocation_unref(&alloc);
	vma->vm_private_data = NULL;
}

static int mali_mem_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	mali_mem_allocation *alloc = (mali_mem_allocation *)vma->vm_private_data;
	mali_mem_backend *mem_bkend = NULL;
	int ret;
	int prefetch_num = MALI_VM_NUM_FAULT_PREFETCH;

	unsigned long address = (unsigned long)vmf->virtual_address;
	MALI_DEBUG_ASSERT(alloc->backend_handle);
	MALI_DEBUG_ASSERT((unsigned long)alloc->cpu_mapping.addr <= address);

	/* Get backend memory & Map on CPU */
	mutex_lock(&mali_idr_mutex);
	if (!(mem_bkend = idr_find(&mali_backend_idr, alloc->backend_handle))) {
		MALI_DEBUG_PRINT(1, ("Can't find memory backend in mmap!\n"));
		mutex_unlock(&mali_idr_mutex);
		return VM_FAULT_SIGBUS;
	}
	mutex_unlock(&mali_idr_mutex);
	MALI_DEBUG_ASSERT(mem_bkend->type == alloc->type);

	if ((mem_bkend->type == MALI_MEM_COW && (MALI_MEM_BACKEND_FLAG_SWAP_COWED !=
			(mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED))) &&
	    (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_COW_CPU_NO_WRITE)) {
		/*check if use page fault to do COW*/
		MALI_DEBUG_PRINT(4, ("mali_vma_fault: do cow allocate on demand!, address=0x%x\n", address));
		mutex_lock(&mem_bkend->mutex);
		ret = mali_mem_cow_allocate_on_demand(mem_bkend,
						      (address - vma->vm_start) / PAGE_SIZE);
		mutex_unlock(&mem_bkend->mutex);

		if (ret != _MALI_OSK_ERR_OK) {
			return VM_FAULT_OOM;
		}
		prefetch_num = 1;

		/* handle COW modified range cpu mapping
		 we zap the mapping in cow_modify_range, it will trigger page fault
		 when CPU access it, so here we map it to CPU*/
		mutex_lock(&mem_bkend->mutex);
		ret = mali_mem_cow_cpu_map_pages_locked(mem_bkend, vma, address, prefetch_num);
		mutex_unlock(&mem_bkend->mutex);

		if (unlikely(ret != _MALI_OSK_ERR_OK)) {
			return VM_FAULT_SIGBUS;
		}
	} else if ((mem_bkend->type == MALI_MEM_SWAP) ||
		   (mem_bkend->type == MALI_MEM_COW && (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED))) {
		u32 offset_in_bkend = (address - vma->vm_start) / PAGE_SIZE;
		int ret = _MALI_OSK_ERR_OK;

		mutex_lock(&mem_bkend->mutex);
		if (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_COW_CPU_NO_WRITE) {
			ret = mali_mem_swap_cow_page_on_demand(mem_bkend, offset_in_bkend, &vmf->page);
		} else {
			ret = mali_mem_swap_allocate_page_on_demand(mem_bkend, offset_in_bkend, &vmf->page);
		}
		mutex_unlock(&mem_bkend->mutex);

		if (ret != _MALI_OSK_ERR_OK) {
			MALI_DEBUG_PRINT(2, ("Mali swap memory page fault process failed, address=0x%x\n", address));
			return VM_FAULT_OOM;
		} else {
			return VM_FAULT_LOCKED;
		}
	} else {
		MALI_PRINT_ERROR(("Mali vma fault! It never happen, indicating some logic errors in caller.\n"));
		/*NOT support yet or OOM*/
		return VM_FAULT_OOM;
	}
	return VM_FAULT_NOPAGE;
}

static struct vm_operations_struct mali_kernel_vm_ops = {
	.open = mali_mem_vma_open,
	.close = mali_mem_vma_close,
	.fault = mali_mem_vma_fault,
};


/** @ map mali allocation to CPU address
*
* Supported backend types:
* --MALI_MEM_OS
* -- need to add COW?
 *Not supported backend types:
* -_MALI_MEMORY_BIND_BACKEND_UMP
* -_MALI_MEMORY_BIND_BACKEND_DMA_BUF
* -_MALI_MEMORY_BIND_BACKEND_EXTERNAL_MEMORY
*
*/
int mali_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct mali_session_data *session;
	mali_mem_allocation *mali_alloc = NULL;
	u32 mali_addr = vma->vm_pgoff << PAGE_SHIFT;
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_backend *mem_bkend = NULL;
	int ret = -EFAULT;

	session = (struct mali_session_data *)filp->private_data;
	if (NULL == session) {
		MALI_PRINT_ERROR(("mmap called without any session data available\n"));
		return -EFAULT;
	}

	MALI_DEBUG_PRINT(4, ("MMap() handler: start=0x%08X, phys=0x%08X, size=0x%08X vma->flags 0x%08x\n",
			     (unsigned int)vma->vm_start, (unsigned int)(vma->vm_pgoff << PAGE_SHIFT),
			     (unsigned int)(vma->vm_end - vma->vm_start), vma->vm_flags));

	/* Operations used on any memory system */
	/* do not need to anything in vm open/close now */

	/* find mali allocation structure by vaddress*/
	mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_addr, 0);
	if (likely(mali_vma_node)) {
		mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
		MALI_DEBUG_ASSERT(mali_addr == mali_vma_node->vm_node.start);
		if (unlikely(mali_addr != mali_vma_node->vm_node.start)) {
			/* only allow to use start address for mmap */
			MALI_DEBUG_PRINT(1, ("mali_addr != mali_vma_node->vm_node.start\n"));
			return -EFAULT;
		}
	} else {
		MALI_DEBUG_ASSERT(NULL == mali_vma_node);
		return -EFAULT;
	}

	mali_alloc->cpu_mapping.addr = (void __user *)vma->vm_start;

	if (mali_alloc->flags & _MALI_MEMORY_ALLOCATE_DEFER_BIND) {
		MALI_DEBUG_PRINT(1, ("ERROR : trying to access varying memory by CPU!\n"));
		return -EFAULT;
	}

	/* Get backend memory & Map on CPU */
	mutex_lock(&mali_idr_mutex);
	if (!(mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle))) {
		MALI_DEBUG_PRINT(1, ("Can't find memory backend in mmap!\n"));
		mutex_unlock(&mali_idr_mutex);
		return -EFAULT;
	}
	mutex_unlock(&mali_idr_mutex);

	if (!(MALI_MEM_SWAP == mali_alloc->type ||
	      (MALI_MEM_COW == mali_alloc->type && (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED)))) {
		/* Set some bits which indicate that, the memory is IO memory, meaning
		 * that no paging is to be performed and the memory should not be
		 * included in crash dumps. And that the memory is reserved, meaning
		 * that it's present and can never be paged out (see also previous
		 * entry)
		 */
		vma->vm_flags |= VM_IO;
		vma->vm_flags |= VM_DONTCOPY;
		vma->vm_flags |= VM_PFNMAP;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
		vma->vm_flags |= VM_RESERVED;
#else
		vma->vm_flags |= VM_DONTDUMP;
		vma->vm_flags |= VM_DONTEXPAND;
#endif
	} else if (MALI_MEM_SWAP == mali_alloc->type) {
		vma->vm_pgoff = mem_bkend->start_idx;
	}

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &mali_kernel_vm_ops;

	mali_alloc->cpu_mapping.addr = (void __user *)vma->vm_start;

	/* If it's a copy-on-write mapping, map to read only */
	if (!(vma->vm_flags & VM_WRITE)) {
		MALI_DEBUG_PRINT(4, ("mmap allocation with read only !\n"));
		/* add VM_WRITE for do_page_fault will check this when a write fault */
		vma->vm_flags |= VM_WRITE | VM_READ;
		vma->vm_page_prot = PAGE_READONLY;
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		mem_bkend->flags |= MALI_MEM_BACKEND_FLAG_COW_CPU_NO_WRITE;
		goto out;
	}

	if (mem_bkend->type == MALI_MEM_OS) {
		ret = mali_mem_os_cpu_map(mem_bkend, vma);
	} else if (mem_bkend->type == MALI_MEM_COW &&
		   (MALI_MEM_BACKEND_FLAG_SWAP_COWED != (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED))) {
		ret = mali_mem_cow_cpu_map(mem_bkend, vma);
	} else if (mem_bkend->type == MALI_MEM_BLOCK) {
		ret = mali_mem_block_cpu_map(mem_bkend, vma);
	} else if ((mem_bkend->type == MALI_MEM_SWAP) || (mem_bkend->type == MALI_MEM_COW &&
			(MALI_MEM_BACKEND_FLAG_SWAP_COWED == (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED)))) {
		/*For swappable memory, CPU page table will be created by page fault handler. */
		ret = 0;
	} else if (mem_bkend->type == MALI_MEM_SECURE) {
#if defined(CONFIG_DMA_SHARED_BUFFER)
		ret = mali_mem_secure_cpu_map(mem_bkend, vma);
#else
		MALI_DEBUG_PRINT(1, ("DMA not supported for mali secure memory\n"));
		return -EFAULT;
#endif
	} else {
		/* Not support yet*/
		MALI_DEBUG_PRINT_ERROR(("Invalid type of backend memory! \n"));
		return -EFAULT;
	}

	if (ret != 0) {
		MALI_DEBUG_PRINT(1, ("ret != 0\n"));
		return -EFAULT;
	}
out:
	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == mali_alloc->magic);

	vma->vm_private_data = (void *)mali_alloc;
	mali_alloc->cpu_mapping.vma = vma;

	mali_allocation_ref(mali_alloc);

	return 0;
}

_mali_osk_errcode_t mali_mem_mali_map_prepare(mali_mem_allocation *descriptor)
{
	u32 size = descriptor->psize;
	struct mali_session_data *session = descriptor->session;

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	/* Map dma-buf into this session's page tables */

	if (descriptor->flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		size += MALI_MMU_PAGE_SIZE;
	}

	return mali_mmu_pagedir_map(session->page_directory, descriptor->mali_vma_node.vm_node.start, size);
}

_mali_osk_errcode_t mali_mem_mali_map_resize(mali_mem_allocation *descriptor, u32 new_size)
{
	u32 old_size = descriptor->psize;
	struct mali_session_data *session = descriptor->session;

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	if (descriptor->flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		new_size  += MALI_MMU_PAGE_SIZE;
	}

	if (new_size > old_size) {
		MALI_DEBUG_ASSERT(new_size <= descriptor->mali_vma_node.vm_node.size);
		return mali_mmu_pagedir_map(session->page_directory, descriptor->mali_vma_node.vm_node.start + old_size, new_size - old_size);
	}
	return _MALI_OSK_ERR_OK;
}

void mali_mem_mali_map_free(struct mali_session_data *session, u32 size, mali_address_t vaddr, u32 flags)
{
	if (flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		size += MALI_MMU_PAGE_SIZE;
	}

	/* Umap and flush L2 */
	mali_mmu_pagedir_unmap(session->page_directory, vaddr, size);
	mali_executor_zap_all_active(session);
}

u32 _mali_ukk_report_memory_usage(void)
{
	u32 sum = 0;

	if (MALI_TRUE == mali_memory_have_dedicated_memory()) {
		sum += mali_mem_block_allocator_stat();
	}

	sum += mali_mem_os_stat();

	return sum;
}

u32 _mali_ukk_report_total_memory_size(void)
{
	return mali_dedicated_mem_size + mali_shared_mem_size;
}


/**
 * Per-session memory descriptor mapping table sizes
 */
#define MALI_MEM_DESCRIPTORS_INIT 64
#define MALI_MEM_DESCRIPTORS_MAX 65536

_mali_osk_errcode_t mali_memory_session_begin(struct mali_session_data *session_data)
{
	MALI_DEBUG_PRINT(5, ("Memory session begin\n"));

	session_data->memory_lock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED,
				    _MALI_OSK_LOCK_ORDER_MEM_SESSION);

	if (NULL == session_data->memory_lock) {
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	session_data->cow_lock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_UNORDERED, 0);
	if (NULL == session_data->cow_lock) {
		_mali_osk_mutex_term(session_data->memory_lock);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	mali_memory_manager_init(&session_data->allocation_mgr);

	MALI_DEBUG_PRINT(5, ("MMU session begin: success\n"));
	MALI_SUCCESS;
}

void mali_memory_session_end(struct mali_session_data *session)
{
	MALI_DEBUG_PRINT(3, ("MMU session end\n"));

	if (NULL == session) {
		MALI_DEBUG_PRINT(1, ("No session data found during session end\n"));
		return;
	}
	/* free allocation */
	mali_free_session_allocations(session);
	/* do some check in unint*/
	mali_memory_manager_uninit(&session->allocation_mgr);

	/* Free the lock */
	_mali_osk_mutex_term(session->memory_lock);
	_mali_osk_mutex_term(session->cow_lock);
	return;
}

_mali_osk_errcode_t mali_memory_initialize(void)
{
	_mali_osk_errcode_t err;

	idr_init(&mali_backend_idr);
	mutex_init(&mali_idr_mutex);

	err = mali_mem_swap_init();
	if (err != _MALI_OSK_ERR_OK) {
		return err;
	}
	err = mali_mem_os_init();
	if (_MALI_OSK_ERR_OK == err) {
		err = mali_mem_defer_bind_manager_init();
	}

	return err;
}

void mali_memory_terminate(void)
{
	mali_mem_swap_term();
	mali_mem_defer_bind_manager_destory();
	mali_mem_os_term();
	if (mali_memory_have_dedicated_memory()) {
		mali_mem_block_allocator_destroy();
	}
}


struct mali_page_node *_mali_page_node_allocate(mali_page_node_type type)
{
	mali_page_node *page_node = NULL;

	page_node = kzalloc(sizeof(mali_page_node), GFP_KERNEL);
	MALI_DEBUG_ASSERT(NULL != page_node);

	if (page_node) {
		page_node->type = type;
		INIT_LIST_HEAD(&page_node->list);
	}

	return page_node;
}

void _mali_page_node_ref(struct mali_page_node *node)
{
	if (node->type == MALI_PAGE_NODE_OS) {
		/* add ref to this page */
		get_page(node->page);
	} else if (node->type == MALI_PAGE_NODE_BLOCK) {
		mali_mem_block_add_ref(node);
	} else if (node->type == MALI_PAGE_NODE_SWAP) {
		atomic_inc(&node->swap_it->ref_count);
	} else {
		MALI_DEBUG_PRINT_ERROR(("Invalid type of mali page node! \n"));
	}
}

void _mali_page_node_unref(struct mali_page_node *node)
{
	if (node->type == MALI_PAGE_NODE_OS) {
		/* unref to this page */
		put_page(node->page);
	} else if (node->type == MALI_PAGE_NODE_BLOCK) {
		mali_mem_block_dec_ref(node);
	} else {
		MALI_DEBUG_PRINT_ERROR(("Invalid type of mali page node! \n"));
	}
}


void _mali_page_node_add_page(struct mali_page_node *node, struct page *page)
{
	MALI_DEBUG_ASSERT(MALI_PAGE_NODE_OS == node->type);
	node->page = page;
}


void _mali_page_node_add_swap_item(struct mali_page_node *node, struct mali_swap_item *item)
{
	MALI_DEBUG_ASSERT(MALI_PAGE_NODE_SWAP == node->type);
	node->swap_it = item;
}

void _mali_page_node_add_block_item(struct mali_page_node *node, mali_block_item *item)
{
	MALI_DEBUG_ASSERT(MALI_PAGE_NODE_BLOCK == node->type);
	node->blk_it = item;
}


int _mali_page_node_get_ref_count(struct mali_page_node *node)
{
	if (node->type == MALI_PAGE_NODE_OS) {
		/* get ref count of this page */
		return page_count(node->page);
	} else if (node->type == MALI_PAGE_NODE_BLOCK) {
		return mali_mem_block_get_ref_count(node);
	} else if (node->type == MALI_PAGE_NODE_SWAP) {
		return atomic_read(&node->swap_it->ref_count);
	} else {
		MALI_DEBUG_PRINT_ERROR(("Invalid type of mali page node! \n"));
	}
	return -1;
}


dma_addr_t _mali_page_node_get_dma_addr(struct mali_page_node *node)
{
	if (node->type == MALI_PAGE_NODE_OS) {
		return page_private(node->page);
	} else if (node->type == MALI_PAGE_NODE_BLOCK) {
		return _mali_blk_item_get_phy_addr(node->blk_it);
	} else if (node->type == MALI_PAGE_NODE_SWAP) {
		return node->swap_it->dma_addr;
	} else {
		MALI_DEBUG_PRINT_ERROR(("Invalid type of mali page node! \n"));
	}
	return 0;
}


unsigned long _mali_page_node_get_pfn(struct mali_page_node *node)
{
	if (node->type == MALI_PAGE_NODE_OS) {
		return page_to_pfn(node->page);
	} else if (node->type == MALI_PAGE_NODE_BLOCK) {
		/* get phy addr for BLOCK page*/
		return _mali_blk_item_get_pfn(node->blk_it);
	} else if (node->type == MALI_PAGE_NODE_SWAP) {
		return page_to_pfn(node->swap_it->page);
	} else {
		MALI_DEBUG_PRINT_ERROR(("Invalid type of mali page node! \n"));
	}
	return 0;
}



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
#include <linux/platform_device.h>

#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_kernel_linux.h"
#include "mali_scheduler.h"

#include "mali_memory.h"
#include "mali_memory_os_alloc.h"
#if defined(CONFIG_DMA_SHARED_BUFFER)
#include "mali_memory_dma_buf.h"
#include "mali_memory_secure.h"
#endif
#if defined(CONFIG_MALI400_UMP)
#include "mali_memory_ump.h"
#endif
#include "mali_memory_external.h"
#include "mali_memory_manager.h"
#include "mali_memory_virtual.h"
#include "mali_memory_cow.h"
#include "mali_memory_block_alloc.h"
#include "mali_memory_swap_alloc.h"



/**
*function @_mali_free_allocation_mem - free a memory allocation
*/
static u32 _mali_free_allocation_mem(mali_mem_allocation *mali_alloc)
{
	mali_mem_backend *mem_bkend = NULL;
	u32 free_pages_nr = 0;

	struct mali_session_data *session = mali_alloc->session;
	MALI_DEBUG_PRINT(4, (" _mali_free_allocation_mem, psize =0x%x! \n", mali_alloc->psize));
	if (0 == mali_alloc->psize)
		goto out;

	/* Get backend memory & Map on CPU */
	mutex_lock(&mali_idr_mutex);
	mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle);
	mutex_unlock(&mali_idr_mutex);
	MALI_DEBUG_ASSERT(NULL != mem_bkend);

	switch (mem_bkend->type) {
	case MALI_MEM_OS:
		free_pages_nr = mali_mem_os_release(mem_bkend);
		atomic_sub(free_pages_nr, &session->mali_mem_allocated_pages);
		break;
	case MALI_MEM_UMP:
#if defined(CONFIG_MALI400_UMP)
		mali_mem_unbind_ump_buf(mem_bkend);
		atomic_sub(mem_bkend->size / MALI_MMU_PAGE_SIZE, &session->mali_mem_array[mem_bkend->type]);
#else
		MALI_DEBUG_PRINT(1, ("UMP not supported\n"));
#endif
		break;
	case MALI_MEM_DMA_BUF:
#if defined(CONFIG_DMA_SHARED_BUFFER)
		mali_mem_unbind_dma_buf(mem_bkend);
		atomic_sub(mem_bkend->size / MALI_MMU_PAGE_SIZE, &session->mali_mem_array[mem_bkend->type]);
#else
		MALI_DEBUG_PRINT(1, ("DMA not supported\n"));
#endif
		break;
	case MALI_MEM_EXTERNAL:
		mali_mem_unbind_ext_buf(mem_bkend);
		atomic_sub(mem_bkend->size / MALI_MMU_PAGE_SIZE, &session->mali_mem_array[mem_bkend->type]);
		break;

	case MALI_MEM_BLOCK:
		free_pages_nr = mali_mem_block_release(mem_bkend);
		atomic_sub(free_pages_nr, &session->mali_mem_allocated_pages);
		break;

	case MALI_MEM_COW:
		if (mem_bkend->flags & MALI_MEM_BACKEND_FLAG_SWAP_COWED) {
			free_pages_nr = mali_mem_swap_release(mem_bkend, MALI_TRUE);
		} else {
			free_pages_nr = mali_mem_cow_release(mem_bkend, MALI_TRUE);
		}
		atomic_sub(free_pages_nr, &session->mali_mem_allocated_pages);
		break;
	case MALI_MEM_SWAP:
		free_pages_nr = mali_mem_swap_release(mem_bkend, MALI_TRUE);
		atomic_sub(free_pages_nr, &session->mali_mem_allocated_pages);
		atomic_sub(free_pages_nr, &session->mali_mem_array[mem_bkend->type]);
		break;
	case MALI_MEM_SECURE:
#if defined(CONFIG_DMA_SHARED_BUFFER)
		free_pages_nr = mali_mem_secure_release(mem_bkend);
		atomic_sub(free_pages_nr, &session->mali_mem_allocated_pages);
#else
		MALI_DEBUG_PRINT(1, ("DMA not supported for mali secure memory\n"));
#endif
		break;
	default:
		MALI_DEBUG_PRINT(1, ("mem type %d is not in the mali_mem_type enum.\n", mem_bkend->type));
		break;
	}

	/*Remove backend memory idex */
	mutex_lock(&mali_idr_mutex);
	idr_remove(&mali_backend_idr, mali_alloc->backend_handle);
	mutex_unlock(&mali_idr_mutex);
	kfree(mem_bkend);
out:
	/* remove memory allocation  */
	mali_vma_offset_remove(&session->allocation_mgr, &mali_alloc->mali_vma_node);
	mali_mem_allocation_struct_destory(mali_alloc);
	return free_pages_nr;
}

/**
*  ref_count for allocation
*/
u32 mali_allocation_unref(struct mali_mem_allocation **alloc)
{
	u32 free_pages_nr = 0;
	mali_mem_allocation *mali_alloc = *alloc;
	*alloc = NULL;
	if (0 == _mali_osk_atomic_dec_return(&mali_alloc->mem_alloc_refcount)) {
		free_pages_nr = _mali_free_allocation_mem(mali_alloc);
	}
	return free_pages_nr;
}

void mali_allocation_ref(struct mali_mem_allocation *alloc)
{
	_mali_osk_atomic_inc(&alloc->mem_alloc_refcount);
}

void mali_free_session_allocations(struct mali_session_data *session)
{
	struct mali_mem_allocation *entry, *next;

	MALI_DEBUG_PRINT(4, (" mali_free_session_allocations! \n"));

	list_for_each_entry_safe(entry, next, &session->allocation_mgr.head, list) {
		mali_allocation_unref(&entry);
	}
}

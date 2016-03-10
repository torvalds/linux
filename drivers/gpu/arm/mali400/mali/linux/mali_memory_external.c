/*
 * Copyright (C) 2013-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"
#include "mali_memory.h"
#include "mali_mem_validation.h"
#include "mali_uk_types.h"

void mali_mem_unbind_ext_buf(mali_mem_backend *mem_backend)
{
	mali_mem_allocation *alloc;
	struct mali_session_data *session;
	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	alloc = mem_backend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);
	MALI_DEBUG_ASSERT(MALI_MEM_EXTERNAL == mem_backend->type);

	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);
	mali_session_memory_lock(session);
	mali_mem_mali_map_free(session, alloc->psize, alloc->mali_vma_node.vm_node.start,
			       alloc->flags);
	mali_session_memory_unlock(session);
}

_mali_osk_errcode_t mali_mem_bind_ext_buf(mali_mem_allocation *alloc,
		mali_mem_backend *mem_backend,
		u32 phys_addr,
		u32 flag)
{
	struct mali_session_data *session;
	_mali_osk_errcode_t err;
	u32 virt, phys, size;
	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	MALI_DEBUG_ASSERT_POINTER(alloc);
	size = alloc->psize;
	session = (struct mali_session_data *)(uintptr_t)alloc->session;
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_INVALID_ARGS);

	/* check arguments */
	/* NULL might be a valid Mali address */
	if (!size) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	/* size must be a multiple of the system page size */
	if (size % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	/* Validate the mali physical range */
	if (_MALI_OSK_ERR_OK != mali_mem_validation_check(phys_addr, size)) {
		return _MALI_OSK_ERR_FAULT;
	}

	if (flag & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		alloc->flags |= MALI_MEM_FLAG_MALI_GUARD_PAGE;
	}

	mali_session_memory_lock(session);

	virt = alloc->mali_vma_node.vm_node.start;
	phys = phys_addr;

	err = mali_mem_mali_map_prepare(alloc);
	if (_MALI_OSK_ERR_OK != err) {
		mali_session_memory_unlock(session);
		return _MALI_OSK_ERR_NOMEM;
	}

	mali_mmu_pagedir_update(session->page_directory, virt, phys, size, MALI_MMU_FLAGS_DEFAULT);

	if (alloc->flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		mali_mmu_pagedir_update(session->page_directory, virt + size, phys, _MALI_OSK_MALI_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);
	}
	MALI_DEBUG_PRINT(3,
			 ("Requested to map physical memory 0x%x-0x%x into virtual memory 0x%x\n",
			  phys_addr, (phys_addr + size - 1),
			  virt));
	mali_session_memory_unlock(session);

	MALI_SUCCESS;
}


/*
 * Copyright (C) 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_kernel_linux.h"
#include "mali_memory.h"
#include "ump_kernel_interface.h"

static int mali_mem_ump_map(mali_mem_backend *mem_backend)
{
	ump_dd_handle ump_mem;
	mali_mem_allocation *alloc;
	struct mali_session_data *session;
	u32 nr_blocks;
	u32 i;
	ump_dd_physical_block *ump_blocks;
	struct mali_page_directory *pagedir;
	u32 offset = 0;
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	MALI_DEBUG_ASSERT(MALI_MEM_UMP == mem_backend->type);

	alloc = mem_backend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);

	session = alloc->session;
	MALI_DEBUG_ASSERT_POINTER(session);

	ump_mem = mem_backend->ump_mem.handle;
	MALI_DEBUG_ASSERT(UMP_DD_HANDLE_INVALID != ump_mem);

	nr_blocks = ump_dd_phys_block_count_get(ump_mem);
	if (nr_blocks == 0) {
		MALI_DEBUG_PRINT(1, ("No block count\n"));
		return -EINVAL;
	}

	ump_blocks = _mali_osk_malloc(sizeof(*ump_blocks) * nr_blocks);
	if (NULL == ump_blocks) {
		return -ENOMEM;
	}

	if (UMP_DD_INVALID == ump_dd_phys_blocks_get(ump_mem, ump_blocks, nr_blocks)) {
		_mali_osk_free(ump_blocks);
		return -EFAULT;
	}

	pagedir = session->page_directory;

	mali_session_memory_lock(session);

	err = mali_mem_mali_map_prepare(alloc);
	if (_MALI_OSK_ERR_OK != err) {
		MALI_DEBUG_PRINT(1, ("Mapping of UMP memory failed\n"));

		_mali_osk_free(ump_blocks);
		mali_session_memory_unlock(session);
		return -ENOMEM;
	}

	for (i = 0; i < nr_blocks; ++i) {
		u32 virt = alloc->mali_vma_node.vm_node.start + offset;

		MALI_DEBUG_PRINT(7, ("Mapping in 0x%08x size %d\n", ump_blocks[i].addr , ump_blocks[i].size));

		mali_mmu_pagedir_update(pagedir, virt, ump_blocks[i].addr,
					ump_blocks[i].size, MALI_MMU_FLAGS_DEFAULT);

		offset += ump_blocks[i].size;
	}

	if (alloc->flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		u32 virt = alloc->mali_vma_node.vm_node.start + offset;

		/* Map in an extra virtual guard page at the end of the VMA */
		MALI_DEBUG_PRINT(6, ("Mapping in extra guard page\n"));

		mali_mmu_pagedir_update(pagedir, virt, ump_blocks[0].addr, _MALI_OSK_MALI_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);

		offset += _MALI_OSK_MALI_PAGE_SIZE;
	}
	mali_session_memory_unlock(session);
	_mali_osk_free(ump_blocks);
	return 0;
}

static void mali_mem_ump_unmap(mali_mem_allocation *alloc)
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

int mali_mem_bind_ump_buf(mali_mem_allocation *alloc, mali_mem_backend *mem_backend, u32  secure_id, u32 flags)
{
	ump_dd_handle ump_mem;
	int ret;
	MALI_DEBUG_ASSERT_POINTER(alloc);
	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	MALI_DEBUG_ASSERT(MALI_MEM_UMP == mem_backend->type);

	MALI_DEBUG_PRINT(3,
			 ("Requested to map ump memory with secure id %d into virtual memory 0x%08X, size 0x%08X\n",
			  secure_id, alloc->mali_vma_node.vm_node.start, alloc->mali_vma_node.vm_node.size));

	ump_mem = ump_dd_handle_create_from_secure_id(secure_id);
	if (UMP_DD_HANDLE_INVALID == ump_mem) MALI_ERROR(_MALI_OSK_ERR_FAULT);
	alloc->flags |= MALI_MEM_FLAG_DONT_CPU_MAP;
	if (flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		alloc->flags |= MALI_MEM_FLAG_MALI_GUARD_PAGE;
	}

	mem_backend->ump_mem.handle = ump_mem;

	ret = mali_mem_ump_map(mem_backend);
	if (0 != ret) {
		ump_dd_reference_release(ump_mem);
		return _MALI_OSK_ERR_FAULT;
	}
	MALI_DEBUG_PRINT(3, ("Returning from UMP bind\n"));
	return _MALI_OSK_ERR_OK;
}

void mali_mem_unbind_ump_buf(mali_mem_backend *mem_backend)
{
	ump_dd_handle ump_mem;
	mali_mem_allocation *alloc;
	MALI_DEBUG_ASSERT_POINTER(mem_backend);
	MALI_DEBUG_ASSERT(MALI_MEM_UMP == mem_backend->type);
	ump_mem = mem_backend->ump_mem.handle;
	MALI_DEBUG_ASSERT(UMP_DD_HANDLE_INVALID != ump_mem);

	alloc = mem_backend->mali_allocation;
	MALI_DEBUG_ASSERT_POINTER(alloc);
	mali_mem_ump_unmap(alloc);
	ump_dd_reference_release(ump_mem);
}


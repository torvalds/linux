/*
 * Copyright (C) 2012-2014 ARM Limited. All rights reserved.
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

static int mali_ump_map(struct mali_session_data *session, mali_mem_allocation *descriptor)
{
	ump_dd_handle ump_mem;
	u32 nr_blocks;
	u32 i;
	ump_dd_physical_block *ump_blocks;
	struct mali_page_directory *pagedir;
	u32 offset = 0;
	u32 prop;
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT(MALI_MEM_UMP == descriptor->type);

	ump_mem = descriptor->ump_mem.handle;
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
	prop = descriptor->mali_mapping.properties;

	err = mali_mem_mali_map_prepare(descriptor);
	if (_MALI_OSK_ERR_OK != err) {
		MALI_DEBUG_PRINT(1, ("Mapping of UMP memory failed\n"));

		_mali_osk_free(ump_blocks);
		return -ENOMEM;
	}

	for (i = 0; i < nr_blocks; ++i) {
		u32 virt = descriptor->mali_mapping.addr + offset;

		MALI_DEBUG_PRINT(7, ("Mapping in 0x%08x size %d\n", ump_blocks[i].addr , ump_blocks[i].size));

		mali_mmu_pagedir_update(pagedir, virt, ump_blocks[i].addr,
					ump_blocks[i].size, prop);

		offset += ump_blocks[i].size;
	}

	if (descriptor->flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		u32 virt = descriptor->mali_mapping.addr + offset;

		/* Map in an extra virtual guard page at the end of the VMA */
		MALI_DEBUG_PRINT(6, ("Mapping in extra guard page\n"));

		mali_mmu_pagedir_update(pagedir, virt, ump_blocks[0].addr, _MALI_OSK_MALI_PAGE_SIZE, prop);

		offset += _MALI_OSK_MALI_PAGE_SIZE;
	}

	_mali_osk_free(ump_blocks);

	return 0;
}

void mali_ump_unmap(struct mali_session_data *session, mali_mem_allocation *descriptor)
{
	ump_dd_handle ump_mem;
	struct mali_page_directory *pagedir;

	ump_mem = descriptor->ump_mem.handle;
	pagedir = session->page_directory;

	MALI_DEBUG_ASSERT(UMP_DD_HANDLE_INVALID != ump_mem);

	mali_mem_mali_map_free(descriptor);

	ump_dd_reference_release(ump_mem);
	return;
}

_mali_osk_errcode_t _mali_ukk_attach_ump_mem(_mali_uk_attach_ump_mem_s *args)
{
	ump_dd_handle ump_mem;
	struct mali_session_data *session;
	mali_mem_allocation *descriptor;
	int md, ret;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;

	/* check arguments */
	/* NULL might be a valid Mali address */
	if (!args->size) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	/* size must be a multiple of the system page size */
	if (args->size % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	MALI_DEBUG_PRINT(3,
			 ("Requested to map ump memory with secure id %d into virtual memory 0x%08X, size 0x%08X\n",
			  args->secure_id, args->mali_address, args->size));

	ump_mem = ump_dd_handle_create_from_secure_id((int)args->secure_id);

	if (UMP_DD_HANDLE_INVALID == ump_mem) MALI_ERROR(_MALI_OSK_ERR_FAULT);

	descriptor = mali_mem_descriptor_create(session, MALI_MEM_UMP);
	if (NULL == descriptor) {
		ump_dd_reference_release(ump_mem);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	descriptor->ump_mem.handle = ump_mem;
	descriptor->mali_mapping.addr = args->mali_address;
	descriptor->size = args->size;
	descriptor->mali_mapping.properties = MALI_MMU_FLAGS_DEFAULT;
	descriptor->flags |= MALI_MEM_FLAG_DONT_CPU_MAP;

	if (args->flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		descriptor->flags = MALI_MEM_FLAG_MALI_GUARD_PAGE;
	}

	_mali_osk_mutex_wait(session->memory_lock);

	ret = mali_ump_map(session, descriptor);
	if (0 != ret) {
		_mali_osk_mutex_signal(session->memory_lock);
		ump_dd_reference_release(ump_mem);
		mali_mem_descriptor_destroy(descriptor);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	_mali_osk_mutex_signal(session->memory_lock);


	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session->descriptor_mapping, descriptor, &md)) {
		ump_dd_reference_release(ump_mem);
		mali_mem_descriptor_destroy(descriptor);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	args->cookie = md;

	MALI_DEBUG_PRINT(5, ("Returning from UMP attach\n"));

	MALI_SUCCESS;
}

void mali_mem_ump_release(mali_mem_allocation *descriptor)
{
	struct mali_session_data *session = descriptor->session;

	MALI_DEBUG_ASSERT(MALI_MEM_UMP == descriptor->type);

	mali_ump_unmap(session, descriptor);
}

_mali_osk_errcode_t _mali_ukk_release_ump_mem(_mali_uk_release_ump_mem_s *args)
{
	mali_mem_allocation *descriptor;
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;

	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_get(session->descriptor_mapping, args->cookie, (void **)&descriptor)) {
		MALI_DEBUG_PRINT(1, ("Invalid memory descriptor %d used to release ump memory\n", args->cookie));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	descriptor = mali_descriptor_mapping_free(session->descriptor_mapping, args->cookie);

	if (NULL != descriptor) {
		_mali_osk_mutex_wait(session->memory_lock);
		mali_mem_ump_release(descriptor);
		_mali_osk_mutex_signal(session->memory_lock);

		mali_mem_descriptor_destroy(descriptor);
	}

	MALI_SUCCESS;
}

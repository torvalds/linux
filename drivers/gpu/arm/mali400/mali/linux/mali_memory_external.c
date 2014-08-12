/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_osk.h"
#include "mali_memory.h"
#include "mali_kernel_descriptor_mapping.h"
#include "mali_mem_validation.h"
#include "mali_uk_types.h"

void mali_mem_external_release(mali_mem_allocation *descriptor)
{
	MALI_DEBUG_ASSERT(MALI_MEM_EXTERNAL == descriptor->type);

	mali_mem_mali_map_free(descriptor);
}

_mali_osk_errcode_t _mali_ukk_map_external_mem(_mali_uk_map_external_mem_s *args)
{
	struct mali_session_data *session;
	mali_mem_allocation * descriptor;
	int md;
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	session = (struct mali_session_data *)args->ctx;
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_INVALID_ARGS);

	/* check arguments */
	/* NULL might be a valid Mali address */
	if (! args->size) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	/* size must be a multiple of the system page size */
	if (args->size % _MALI_OSK_MALI_PAGE_SIZE) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	MALI_DEBUG_PRINT(3,
	                 ("Requested to map physical memory 0x%x-0x%x into virtual memory 0x%x\n",
	                  (void*)args->phys_addr,
	                  (void*)(args->phys_addr + args->size -1),
	                  (void*)args->mali_address)
	                );

	/* Validate the mali physical range */
	if (_MALI_OSK_ERR_OK != mali_mem_validation_check(args->phys_addr, args->size)) {
		return _MALI_OSK_ERR_FAULT;
	}

	descriptor = mali_mem_descriptor_create(session, MALI_MEM_EXTERNAL);
	if (NULL == descriptor) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

	descriptor->mali_mapping.addr = args->mali_address;
	descriptor->size = args->size;

	if (args->flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE) {
		descriptor->flags = MALI_MEM_FLAG_MALI_GUARD_PAGE;
	}

	_mali_osk_mutex_wait(session->memory_lock);
	{
		u32 virt = descriptor->mali_mapping.addr;
		u32 phys = args->phys_addr;
		u32 size = args->size;

		err = mali_mem_mali_map_prepare(descriptor);
		if (_MALI_OSK_ERR_OK != err) {
			_mali_osk_mutex_signal(session->memory_lock);
			mali_mem_descriptor_destroy(descriptor);
			return _MALI_OSK_ERR_NOMEM;
		}

		mali_mmu_pagedir_update(session->page_directory, virt, phys, size, MALI_MMU_FLAGS_DEFAULT);

		if (descriptor->flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
			mali_mmu_pagedir_update(session->page_directory, virt + size, phys, _MALI_OSK_MALI_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);
		}
	}
	_mali_osk_mutex_signal(session->memory_lock);

	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session->descriptor_mapping, descriptor, &md)) {
		_mali_osk_mutex_wait(session->memory_lock);
		mali_mem_external_release(descriptor);
		_mali_osk_mutex_signal(session->memory_lock);
		mali_mem_descriptor_destroy(descriptor);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	args->cookie = md;

	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_unmap_external_mem( _mali_uk_unmap_external_mem_s *args )
{
	mali_mem_allocation * descriptor;
	void* old_value;
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	session = (struct mali_session_data *)args->ctx;
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_INVALID_ARGS);

	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_get(session->descriptor_mapping, args->cookie, (void**)&descriptor)) {
		MALI_DEBUG_PRINT(1, ("Invalid memory descriptor %d used to unmap external memory\n", args->cookie));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	old_value = mali_descriptor_mapping_free(session->descriptor_mapping, args->cookie);

	if (NULL != old_value) {
		_mali_osk_mutex_wait(session->memory_lock);
		mali_mem_external_release(descriptor);
		_mali_osk_mutex_signal(session->memory_lock);
		mali_mem_descriptor_destroy(descriptor);
	}

	MALI_SUCCESS;
}

/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "ump_osk.h"
#include "ump_uk_types.h"

#include "ump_kernel_interface_ref_drv.h"
#include "ump_kernel_common.h"
#include "ump_kernel_descriptor_mapping.h"

#define UMP_MINIMUM_SIZE         4096
#define UMP_MINIMUM_SIZE_MASK    (~(UMP_MINIMUM_SIZE-1))
#define UMP_SIZE_ALIGN(x)        (((x)+UMP_MINIMUM_SIZE-1)&UMP_MINIMUM_SIZE_MASK)
#define UMP_ADDR_ALIGN_OFFSET(x) ((x)&(UMP_MINIMUM_SIZE-1))
static void phys_blocks_release(void * ctx, struct ump_dd_mem * descriptor);

UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_create_from_phys_blocks(ump_dd_physical_block * blocks, unsigned long num_blocks)
{
	ump_dd_mem * mem;
	unsigned long size_total = 0;
	int map_id;
	u32 i;

	/* Go through the input blocks and verify that they are sane */
	for (i=0; i < num_blocks; i++)
	{
		unsigned long addr = blocks[i].addr;
		unsigned long size = blocks[i].size;

		DBG_MSG(5, ("Adding physical memory to new handle. Address: 0x%08lx, size: %lu\n", addr, size));
		size_total += blocks[i].size;

		if (0 != UMP_ADDR_ALIGN_OFFSET(addr))
		{
			MSG_ERR(("Trying to create UMP memory from unaligned physical address. Address: 0x%08lx\n", addr));
			return UMP_DD_HANDLE_INVALID;
		}

		if (0 != UMP_ADDR_ALIGN_OFFSET(size))
		{
			MSG_ERR(("Trying to create UMP memory with unaligned size. Size: %lu\n", size));
			return UMP_DD_HANDLE_INVALID;
		}
	}

	/* Allocate the ump_dd_mem struct for this allocation */
	mem = _mali_osk_malloc(sizeof(*mem));
	if (NULL == mem)
	{
		DBG_MSG(1, ("Could not allocate ump_dd_mem in ump_dd_handle_create_from_phys_blocks()\n"));
		return UMP_DD_HANDLE_INVALID;
	}

	/* Find a secure ID for this allocation */
	_mali_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	map_id = ump_descriptor_mapping_allocate_mapping(device.secure_id_map, (void*) mem);

	if (map_id < 0)
	{
		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_mali_osk_free(mem);
		DBG_MSG(1, ("Failed to allocate secure ID in ump_dd_handle_create_from_phys_blocks()\n"));
		return UMP_DD_HANDLE_INVALID;
	}

	/* Now, make a copy of the block information supplied by the user */
	mem->block_array = _mali_osk_malloc(sizeof(ump_dd_physical_block)* num_blocks);
	if (NULL == mem->block_array)
	{
		ump_descriptor_mapping_free(device.secure_id_map, map_id);
		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_mali_osk_free(mem);
		DBG_MSG(1, ("Could not allocate a mem handle for function ump_dd_handle_create_from_phys_blocks().\n"));
		return UMP_DD_HANDLE_INVALID;
	}

	_mali_osk_memcpy(mem->block_array, blocks, sizeof(ump_dd_physical_block) * num_blocks);

	/* And setup the rest of the ump_dd_mem struct */
	_mali_osk_atomic_init(&mem->ref_count, 1);
	mem->secure_id = (ump_secure_id)map_id;
	mem->size_bytes = size_total;
	mem->nr_blocks = num_blocks;
	mem->backend_info = NULL;
	mem->ctx = NULL;
	mem->release_func = phys_blocks_release;
	/* For now UMP handles created by ump_dd_handle_create_from_phys_blocks() is forced to be Uncached */
	mem->is_cached = 0;

	_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	DBG_MSG(3, ("UMP memory created. ID: %u, size: %lu\n", mem->secure_id, mem->size_bytes));

	return (ump_dd_handle)mem;
}

static void phys_blocks_release(void * ctx, struct ump_dd_mem * descriptor)
{
	_mali_osk_free(descriptor->block_array);
	descriptor->block_array = NULL;
}

_mali_osk_errcode_t _ump_ukk_allocate( _ump_uk_allocate_s *user_interaction )
{
	ump_session_data * session_data = NULL;
	ump_dd_mem *new_allocation = NULL;
	ump_session_memory_list_element * session_memory_element = NULL;
	int map_id;

	DEBUG_ASSERT_POINTER( user_interaction );
	DEBUG_ASSERT_POINTER( user_interaction->ctx );

	session_data = (ump_session_data *) user_interaction->ctx;

	session_memory_element = _mali_osk_calloc( 1, sizeof(ump_session_memory_list_element));
	if (NULL == session_memory_element)
	{
		DBG_MSG(1, ("Failed to allocate ump_session_memory_list_element in ump_ioctl_allocate()\n"));
		return _MALI_OSK_ERR_NOMEM;
	}


	new_allocation = _mali_osk_calloc( 1, sizeof(ump_dd_mem));
	if (NULL==new_allocation)
	{
		_mali_osk_free(session_memory_element);
		DBG_MSG(1, ("Failed to allocate ump_dd_mem in _ump_ukk_allocate()\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	/* Create a secure ID for this allocation */
	_mali_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	map_id = ump_descriptor_mapping_allocate_mapping(device.secure_id_map, (void*)new_allocation);

	if (map_id < 0)
	{
		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_mali_osk_free(session_memory_element);
		_mali_osk_free(new_allocation);
		DBG_MSG(1, ("Failed to allocate secure ID in ump_ioctl_allocate()\n"));
		return - _MALI_OSK_ERR_INVALID_FUNC;
	}

	/* Initialize the part of the new_allocation that we know so for */
	new_allocation->secure_id = (ump_secure_id)map_id;
	_mali_osk_atomic_init(&new_allocation->ref_count,1);
	if ( 0==(UMP_REF_DRV_UK_CONSTRAINT_USE_CACHE & user_interaction->constraints) )
		 new_allocation->is_cached = 0;
	else new_allocation->is_cached = 1;

	/* special case a size of 0, we should try to emulate what malloc does in this case, which is to return a valid pointer that must be freed, but can't be dereferences */
	if (0 == user_interaction->size)
	{
		user_interaction->size = 1; /* emulate by actually allocating the minimum block size */
	}

	new_allocation->size_bytes = UMP_SIZE_ALIGN(user_interaction->size); /* Page align the size */

	/* Now, ask the active memory backend to do the actual memory allocation */
	if (!device.backend->allocate( device.backend->ctx, new_allocation ) )
	{
		DBG_MSG(3, ("OOM: No more UMP memory left. Failed to allocate memory in ump_ioctl_allocate(). Size: %lu, requested size: %lu\n", new_allocation->size_bytes, (unsigned long)user_interaction->size));
		ump_descriptor_mapping_free(device.secure_id_map, map_id);
		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		_mali_osk_free(new_allocation);
		_mali_osk_free(session_memory_element);
		return _MALI_OSK_ERR_INVALID_FUNC;
	}

	new_allocation->ctx = device.backend->ctx;
	new_allocation->release_func = device.backend->release;

	_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	/* Initialize the session_memory_element, and add it to the session object */
	session_memory_element->mem = new_allocation;
	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	_mali_osk_list_add(&(session_memory_element->list), &(session_data->list_head_session_memory_list));
	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	user_interaction->secure_id = new_allocation->secure_id;
	user_interaction->size = new_allocation->size_bytes;
	DBG_MSG(3, ("UMP memory allocated. ID: %u, size: %lu\n", new_allocation->secure_id, new_allocation->size_bytes));

	return _MALI_OSK_ERR_OK;
}

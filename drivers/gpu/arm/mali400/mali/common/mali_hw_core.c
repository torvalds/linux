/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_hw_core.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_osk_mali.h"

_mali_osk_errcode_t mali_hw_core_create(struct mali_hw_core *core, const _mali_osk_resource_t *resource, u32 reg_size)
{
	core->phys_addr = resource->base;
	core->phys_offset = resource->base - _mali_osk_resource_base_address();
	core->description = resource->description;
	core->size = reg_size;

	MALI_DEBUG_ASSERT(core->phys_offset < core->phys_addr);

	if (_MALI_OSK_ERR_OK == _mali_osk_mem_reqregion(core->phys_addr, core->size, core->description)) {
		core->mapped_registers = _mali_osk_mem_mapioregion(core->phys_addr, core->size, core->description);
		if (NULL != core->mapped_registers) {
			return _MALI_OSK_ERR_OK;
		} else {
			MALI_PRINT_ERROR(("Failed to map memory region for core %s at phys_addr 0x%08X\n", core->description, core->phys_addr));
		}
		_mali_osk_mem_unreqregion(core->phys_addr, core->size);
	} else {
		MALI_PRINT_ERROR(("Failed to request memory region for core %s at phys_addr 0x%08X\n", core->description, core->phys_addr));
	}

	return _MALI_OSK_ERR_FAULT;
}

void mali_hw_core_delete(struct mali_hw_core *core)
{
	_mali_osk_mem_unmapioregion(core->phys_addr, core->size, core->mapped_registers);
	core->mapped_registers = NULL;
	_mali_osk_mem_unreqregion(core->phys_addr, core->size);
}

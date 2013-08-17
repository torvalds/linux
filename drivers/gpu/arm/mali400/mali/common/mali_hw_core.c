/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_hw_core.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"

_mali_osk_errcode_t mali_hw_core_create(struct mali_hw_core *core, const _mali_osk_resource_t *resource, u32 reg_size)
{
	core->phys_addr = resource->base;
	core->description = resource->description;
	core->size = reg_size;

	if (_MALI_OSK_ERR_OK == _mali_osk_mem_reqregion(core->phys_addr, core->size, core->description))
	{
		core->mapped_registers = _mali_osk_mem_mapioregion(core->phys_addr, core->size, core->description);
		if (NULL != core->mapped_registers)
		{
			return _MALI_OSK_ERR_OK;
		}
		else
		{
			MALI_PRINT_ERROR(("Failed to map memory region for core %s at phys_addr 0x%08X\n", core->description, core->phys_addr));
		}
		_mali_osk_mem_unreqregion(core->phys_addr, core->size);
	}
	else
	{
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

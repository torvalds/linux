/*
 * Copyright (C) 2011-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_HW_CORE_H__
#define __MALI_HW_CORE_H__

#include "mali_osk.h"
#include "mali_kernel_common.h"

/**
 * The common parts for all Mali HW cores (GP, PP, MMU, L2 and PMU)
 * This struct is embedded inside all core specific structs.
 */
struct mali_hw_core {
	uintptr_t phys_addr;              /**< Physical address of the registers */
	u32 phys_offset;                  /**< Offset from start of Mali to registers */
	u32 size;                         /**< Size of registers */
	mali_io_address mapped_registers; /**< Virtual mapping of the registers */
	const char *description;          /**< Name of unit (as specified in device configuration) */
};

#define MALI_REG_POLL_COUNT_FAST 1000000
#define MALI_REG_POLL_COUNT_SLOW 1000000

/*
 * GP and PP core translate their int_stat/rawstat into one of these
 */
enum mali_interrupt_result {
	MALI_INTERRUPT_RESULT_NONE,
	MALI_INTERRUPT_RESULT_SUCCESS,
	MALI_INTERRUPT_RESULT_SUCCESS_VS,
	MALI_INTERRUPT_RESULT_SUCCESS_PLBU,
	MALI_INTERRUPT_RESULT_OOM,
	MALI_INTERRUPT_RESULT_ERROR
};

_mali_osk_errcode_t mali_hw_core_create(struct mali_hw_core *core, const _mali_osk_resource_t *resource, u32 reg_size);
void mali_hw_core_delete(struct mali_hw_core *core);

MALI_STATIC_INLINE u32 mali_hw_core_register_read(struct mali_hw_core *core, u32 relative_address)
{
	u32 read_val;
	read_val = _mali_osk_mem_ioread32(core->mapped_registers, relative_address);
	MALI_DEBUG_PRINT(6, ("register_read for core %s, relative addr=0x%04X, val=0x%08X\n",
			     core->description, relative_address, read_val));
	return read_val;
}

MALI_STATIC_INLINE void mali_hw_core_register_write_relaxed(struct mali_hw_core *core, u32 relative_address, u32 new_val)
{
	MALI_DEBUG_PRINT(6, ("register_write_relaxed for core %s, relative addr=0x%04X, val=0x%08X\n",
			     core->description, relative_address, new_val));
	_mali_osk_mem_iowrite32_relaxed(core->mapped_registers, relative_address, new_val);
}

/* Conditionally write a register.
 * The register will only be written if the new value is different from the old_value.
 * If the new value is different, the old value will also be updated */
MALI_STATIC_INLINE void mali_hw_core_register_write_relaxed_conditional(struct mali_hw_core *core, u32 relative_address, u32 new_val, const u32 old_val)
{
	MALI_DEBUG_PRINT(6, ("register_write_relaxed for core %s, relative addr=0x%04X, val=0x%08X\n",
			     core->description, relative_address, new_val));
	if (old_val != new_val) {
		_mali_osk_mem_iowrite32_relaxed(core->mapped_registers, relative_address, new_val);
	}
}

MALI_STATIC_INLINE void mali_hw_core_register_write(struct mali_hw_core *core, u32 relative_address, u32 new_val)
{
	MALI_DEBUG_PRINT(6, ("register_write for core %s, relative addr=0x%04X, val=0x%08X\n",
			     core->description, relative_address, new_val));
	_mali_osk_mem_iowrite32(core->mapped_registers, relative_address, new_val);
}

MALI_STATIC_INLINE void mali_hw_core_register_write_array_relaxed(struct mali_hw_core *core, u32 relative_address, u32 *write_array, u32 nr_of_regs)
{
	u32 i;
	MALI_DEBUG_PRINT(6, ("register_write_array: for core %s, relative addr=0x%04X, nr of regs=%u\n",
			     core->description, relative_address, nr_of_regs));

	/* Do not use burst writes against the registers */
	for (i = 0; i < nr_of_regs; i++) {
		mali_hw_core_register_write_relaxed(core, relative_address + i * 4, write_array[i]);
	}
}

/* Conditionally write a set of registers.
 * The register will only be written if the new value is different from the old_value.
 * If the new value is different, the old value will also be updated */
MALI_STATIC_INLINE void mali_hw_core_register_write_array_relaxed_conditional(struct mali_hw_core *core, u32 relative_address, u32 *write_array, u32 nr_of_regs, const u32 *old_array)
{
	u32 i;
	MALI_DEBUG_PRINT(6, ("register_write_array: for core %s, relative addr=0x%04X, nr of regs=%u\n",
			     core->description, relative_address, nr_of_regs));

	/* Do not use burst writes against the registers */
	for (i = 0; i < nr_of_regs; i++) {
		if (old_array[i] != write_array[i]) {
			mali_hw_core_register_write_relaxed(core, relative_address + i * 4, write_array[i]);
		}
	}
}

#endif /* __MALI_HW_CORE_H__ */

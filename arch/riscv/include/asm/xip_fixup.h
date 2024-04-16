/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * XIP fixup macros, only useful in assembly.
 */
#ifndef _ASM_RISCV_XIP_FIXUP_H
#define _ASM_RISCV_XIP_FIXUP_H

#include <linux/pgtable.h>

#ifdef CONFIG_XIP_KERNEL
.macro XIP_FIXUP_OFFSET reg
        REG_L t0, _xip_fixup
        add \reg, \reg, t0
.endm
.macro XIP_FIXUP_FLASH_OFFSET reg
	la t0, __data_loc
	REG_L t1, _xip_phys_offset
	sub \reg, \reg, t1
	add \reg, \reg, t0
.endm

_xip_fixup: .dword CONFIG_PHYS_RAM_BASE - CONFIG_XIP_PHYS_ADDR - XIP_OFFSET
_xip_phys_offset: .dword CONFIG_XIP_PHYS_ADDR + XIP_OFFSET
#else
.macro XIP_FIXUP_OFFSET reg
.endm
.macro XIP_FIXUP_FLASH_OFFSET reg
.endm
#endif /* CONFIG_XIP_KERNEL */

#endif

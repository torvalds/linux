/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * XIP fixup macros, only useful in assembly.
 */
#ifndef _ASM_RISCV_XIP_FIXUP_H
#define _ASM_RISCV_XIP_FIXUP_H

#include <linux/pgtable.h>

#ifdef CONFIG_XIP_KERNEL
.macro XIP_FIXUP_OFFSET reg
	/* Fix-up address in Flash into address in RAM early during boot before
	 * MMU is up. Because generated code "thinks" data is in Flash, but it
	 * is actually in RAM (actually data is also in Flash, but Flash is
	 * read-only, thus we need to use the data residing in RAM).
	 *
	 * The start of data in Flash is _sdata and the start of data in RAM is
	 * CONFIG_PHYS_RAM_BASE. So this fix-up essentially does this:
	 * reg += CONFIG_PHYS_RAM_BASE - _start
	 */
	li t0, CONFIG_PHYS_RAM_BASE
        add \reg, \reg, t0
	la t0, _sdata
	sub \reg, \reg, t0
.endm
.macro XIP_FIXUP_FLASH_OFFSET reg
	la t0, __data_loc
	REG_L t1, _xip_phys_offset
	sub \reg, \reg, t1
	add \reg, \reg, t0
.endm

_xip_phys_offset: .dword CONFIG_XIP_PHYS_ADDR + XIP_OFFSET
#else
.macro XIP_FIXUP_OFFSET reg
.endm
.macro XIP_FIXUP_FLASH_OFFSET reg
.endm
#endif /* CONFIG_XIP_KERNEL */

#endif

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
	/* In linker script, at the transition from read-only section to
	 * writable section, the VMA is increased while LMA remains the same.
	 * (See in linker script how _sdata, __data_loc and LOAD_OFFSET is
	 * changed)
	 *
	 * Consequently, early during boot before MMU is up, the generated code
	 * reads the "writable" section at wrong addresses, because VMA is used
	 * by compiler to generate code, but the data is located in Flash using
	 * LMA.
	 */
	la t0, _sdata
	sub \reg, \reg, t0
	la t0, __data_loc
	add \reg, \reg, t0
.endm
#else
.macro XIP_FIXUP_OFFSET reg
.endm
.macro XIP_FIXUP_FLASH_OFFSET reg
.endm
#endif /* CONFIG_XIP_KERNEL */

#endif

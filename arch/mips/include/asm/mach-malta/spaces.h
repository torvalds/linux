/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#ifndef _ASM_MALTA_SPACES_H
#define _ASM_MALTA_SPACES_H

#ifdef CONFIG_EVA

/*
 * Traditional Malta Board Memory Map for EVA
 *
 * 0x00000000 - 0x0fffffff: 1st RAM region, 256MB
 * 0x10000000 - 0x1bffffff: GIC and CPC Control Registers
 * 0x1c000000 - 0x1fffffff: I/O And Flash
 * 0x20000000 - 0x7fffffff: 2nd RAM region, 1.5GB
 * 0x80000000 - 0xffffffff: Physical memory aliases to 0x0 (2GB)
 *
 * The kernel is still located in 0x80000000(kseg0). However,
 * the physical mask has been shifted to 0x80000000 which exploits the alias
 * on the Malta board. As a result of which, we override the __pa_symbol
 * to peform direct mapping from virtual to physical addresses. In other
 * words, the 0x80000000 virtual address maps to 0x80000000 physical address
 * which in turn aliases to 0x0. We do this in order to be able to use a flat
 * 2GB of memory (0x80000000 - 0xffffffff) so we can avoid the I/O hole in
 * 0x10000000 - 0x1fffffff.
 * The last 64KB of physical memory are reserved for correct HIGHMEM
 * macros arithmetics.
 *
 */

#define PAGE_OFFSET	_AC(0x0, UL)
#define PHYS_OFFSET	_AC(0x80000000, UL)
#define HIGHMEM_START	_AC(0xffff0000, UL)

#define __pa_symbol(x)	(RELOC_HIDE((unsigned long)(x), 0))

#endif /* CONFIG_EVA */

#include <asm/mach-generic/spaces.h>

#endif /* _ASM_MALTA_SPACES_H */

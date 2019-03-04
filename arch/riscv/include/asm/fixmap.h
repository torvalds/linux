/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 */

#ifndef _ASM_RISCV_FIXMAP_H
#define _ASM_RISCV_FIXMAP_H

#include <linux/kernel.h>
#include <linux/sizes.h>
#include <asm/page.h>
#include <asm/pgtable.h>

/*
 * Here we define all the compile-time 'special' virtual addresses.
 * The point is to have a constant address at compile time, but to
 * set the physical address only in the boot process.
 *
 * These 'compile-time allocated' memory buffers are page-sized. Use
 * set_fixmap(idx,phys) to associate physical memory with fixmap indices.
 */
enum fixed_addresses {
	FIX_HOLE,
	FIX_EARLYCON_MEM_BASE,
	__end_of_fixed_addresses
};

#define FIXADDR_SIZE		(__end_of_fixed_addresses * PAGE_SIZE)
#define FIXADDR_TOP		(PAGE_OFFSET)
#define FIXADDR_START		(FIXADDR_TOP - FIXADDR_SIZE)

#define FIXMAP_PAGE_IO		PAGE_KERNEL

#define __early_set_fixmap	__set_fixmap

#define __late_set_fixmap	__set_fixmap
#define __late_clear_fixmap(idx) __set_fixmap((idx), 0, FIXMAP_PAGE_CLEAR)

extern void __set_fixmap(enum fixed_addresses idx,
			 phys_addr_t phys, pgprot_t prot);

#include <asm-generic/fixmap.h>

#endif /* _ASM_RISCV_FIXMAP_H */

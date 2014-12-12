/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 * Copyright (C) 2013 Mark Salter <msalter@redhat.com>
 *
 * Adapted from arch/x86_64 version.
 *
 */

#ifndef _ASM_ARM64_FIXMAP_H
#define _ASM_ARM64_FIXMAP_H

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/page.h>

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process.
 *
 * These 'compile-time allocated' memory buffers are
 * page-sized. Use set_fixmap(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 */
enum fixed_addresses {
	FIX_HOLE,
	FIX_EARLYCON_MEM_BASE,
	__end_of_permanent_fixed_addresses,

	/*
	 * Temporary boot-time mappings, used by early_ioremap(),
	 * before ioremap() is functional.
	 */
#ifdef CONFIG_ARM64_64K_PAGES
#define NR_FIX_BTMAPS		4
#else
#define NR_FIX_BTMAPS		64
#endif
#define FIX_BTMAPS_SLOTS	7
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)

	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
	__end_of_fixed_addresses
};

#define FIXADDR_SIZE	(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

#define FIXMAP_PAGE_IO     __pgprot(PROT_DEVICE_nGnRE)

void __init early_fixmap_init(void);

#define __early_set_fixmap __set_fixmap

extern void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot);

#include <asm-generic/fixmap.h>

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_ARM64_FIXMAP_H */

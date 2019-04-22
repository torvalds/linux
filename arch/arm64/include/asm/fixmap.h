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
 * Adapted from arch/x86 version.
 *
 */

#ifndef _ASM_ARM64_FIXMAP_H
#define _ASM_ARM64_FIXMAP_H

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <asm/boot.h>
#include <asm/page.h>
#include <asm/pgtable-prot.h>

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

	/*
	 * Reserve a virtual window for the FDT that is 2 MB larger than the
	 * maximum supported size, and put it at the top of the fixmap region.
	 * The additional space ensures that any FDT that does not exceed
	 * MAX_FDT_SIZE can be mapped regardless of whether it crosses any
	 * 2 MB alignment boundaries.
	 *
	 * Keep this at the top so it remains 2 MB aligned.
	 */
#define FIX_FDT_SIZE		(MAX_FDT_SIZE + SZ_2M)
	FIX_FDT_END,
	FIX_FDT = FIX_FDT_END + FIX_FDT_SIZE / PAGE_SIZE - 1,

	FIX_EARLYCON_MEM_BASE,
	FIX_TEXT_POKE0,

#ifdef CONFIG_ACPI_APEI_GHES
	/* Used for GHES mapping from assorted contexts */
	FIX_APEI_GHES_IRQ,
	FIX_APEI_GHES_SEA,
#ifdef CONFIG_ARM_SDE_INTERFACE
	FIX_APEI_GHES_SDEI_NORMAL,
	FIX_APEI_GHES_SDEI_CRITICAL,
#endif
#endif /* CONFIG_ACPI_APEI_GHES */

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
	FIX_ENTRY_TRAMP_DATA,
	FIX_ENTRY_TRAMP_TEXT,
#define TRAMP_VALIAS		(__fix_to_virt(FIX_ENTRY_TRAMP_TEXT))
#endif /* CONFIG_UNMAP_KERNEL_AT_EL0 */
	__end_of_permanent_fixed_addresses,

	/*
	 * Temporary boot-time mappings, used by early_ioremap(),
	 * before ioremap() is functional.
	 */
#define NR_FIX_BTMAPS		(SZ_256K / PAGE_SIZE)
#define FIX_BTMAPS_SLOTS	7
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)

	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,

	/*
	 * Used for kernel page table creation, so unmapped memory may be used
	 * for tables.
	 */
	FIX_PTE,
	FIX_PMD,
	FIX_PUD,
	FIX_PGD,

	__end_of_fixed_addresses
};

#define FIXADDR_SIZE	(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

#define FIXMAP_PAGE_IO     __pgprot(PROT_DEVICE_nGnRE)

void __init early_fixmap_init(void);

#define __early_set_fixmap __set_fixmap

#define __late_set_fixmap __set_fixmap
#define __late_clear_fixmap(idx) __set_fixmap((idx), 0, FIXMAP_PAGE_CLEAR)

extern void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot);

#include <asm-generic/fixmap.h>

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_ARM64_FIXMAP_H */

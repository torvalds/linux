/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 *
 * Copyright 2008 Freescale Semiconductor Inc.
 *   Port to powerpc added by Kumar Gala
 */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#ifndef __ASSEMBLY__
#include <linux/sizes.h>
#include <linux/pgtable.h>
#include <asm/page.h>
#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_types.h>
#endif

#ifdef CONFIG_KASAN
#include <asm/kasan.h>
#define FIXADDR_TOP	(KASAN_SHADOW_START - PAGE_SIZE)
#else
#define FIXADDR_TOP	((unsigned long)(-PAGE_SIZE))
#endif

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special addresses
 * from the end of virtual memory (0xfffff000) backwards.
 * Also this lets us do fail-safe vmalloc(), we
 * can guarantee that these special addresses and
 * vmalloc()-ed addresses never overlap.
 *
 * these 'compile-time allocated' memory buffers are
 * fixed-size 4k pages. (or larger if used with an increment
 * highger than 1) use fixmap_set(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 * TLB entries of such buffers will not be flushed across
 * task switches.
 */
enum fixed_addresses {
	FIX_HOLE,
	/* reserve the top 128K for early debugging purposes */
	FIX_EARLY_DEBUG_TOP = FIX_HOLE,
	FIX_EARLY_DEBUG_BASE = FIX_EARLY_DEBUG_TOP+(ALIGN(SZ_128, PAGE_SIZE)/PAGE_SIZE)-1,
#ifdef CONFIG_HIGHMEM
	FIX_KMAP_BEGIN,	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_END = FIX_KMAP_BEGIN+(KM_TYPE_NR*NR_CPUS)-1,
#endif
#ifdef CONFIG_PPC_8xx
	/* For IMMR we need an aligned 512K area */
#define FIX_IMMR_SIZE	(512 * 1024 / PAGE_SIZE)
	FIX_IMMR_START,
	FIX_IMMR_BASE = __ALIGN_MASK(FIX_IMMR_START, FIX_IMMR_SIZE - 1) - 1 +
		       FIX_IMMR_SIZE,
#endif
#ifdef CONFIG_PPC_83xx
	/* For IMMR we need an aligned 2M area */
#define FIX_IMMR_SIZE	(SZ_2M / PAGE_SIZE)
	FIX_IMMR_START,
	FIX_IMMR_BASE = __ALIGN_MASK(FIX_IMMR_START, FIX_IMMR_SIZE - 1) - 1 +
		       FIX_IMMR_SIZE,
#endif
	/* FIX_PCIE_MCFG, */
	__end_of_permanent_fixed_addresses,

#define NR_FIX_BTMAPS		(SZ_256K / PAGE_SIZE)
#define FIX_BTMAPS_SLOTS	16
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)

	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
	__end_of_fixed_addresses
};

#define __FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP - __FIXADDR_SIZE)

#define FIXMAP_ALIGNED_SIZE	(ALIGN(FIXADDR_TOP, PGDIR_SIZE) - \
				 ALIGN_DOWN(FIXADDR_START, PGDIR_SIZE))
#define FIXMAP_PTE_SIZE	(FIXMAP_ALIGNED_SIZE / PGDIR_SIZE * PTE_TABLE_SIZE)

#define FIXMAP_PAGE_NOCACHE PAGE_KERNEL_NCG
#define FIXMAP_PAGE_IO	PAGE_KERNEL_NCG

#include <asm-generic/fixmap.h>

static inline void __set_fixmap(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t flags)
{
	if (__builtin_constant_p(idx))
		BUILD_BUG_ON(idx >= __end_of_fixed_addresses);
	else if (WARN_ON(idx >= __end_of_fixed_addresses))
		return;

	map_kernel_page(__fix_to_virt(idx), phys, flags);
}

#define __early_set_fixmap	__set_fixmap

#endif /* !__ASSEMBLY__ */
#endif

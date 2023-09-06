/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel page table mapping
 *
 * Copyright (C) 2015 ARM Ltd.
 */

#ifndef __ASM_KERNEL_PGTABLE_H
#define __ASM_KERNEL_PGTABLE_H

#include <asm/pgtable-hwdef.h>
#include <asm/sparsemem.h>

/*
 * The linear mapping and the start of memory are both 2M aligned (per
 * the arm64 booting.txt requirements). Hence we can use section mapping
 * with 4K (section size = 2M) but not with 16K (section size = 32M) or
 * 64K (section size = 512M).
 */
#ifdef CONFIG_ARM64_4K_PAGES
#define ARM64_KERNEL_USES_PMD_MAPS 1
#else
#define ARM64_KERNEL_USES_PMD_MAPS 0
#endif

/*
 * The idmap and swapper page tables need some space reserved in the kernel
 * image. Both require pgd, pud (4 levels only) and pmd tables to (section)
 * map the kernel. With the 64K page configuration, swapper and idmap need to
 * map to pte level. The swapper also maps the FDT (see __create_page_tables
 * for more information). Note that the number of ID map translation levels
 * could be increased on the fly if system RAM is out of reach for the default
 * VA range, so pages required to map highest possible PA are reserved in all
 * cases.
 */
#if ARM64_KERNEL_USES_PMD_MAPS
#define SWAPPER_PGTABLE_LEVELS	(CONFIG_PGTABLE_LEVELS - 1)
#define IDMAP_PGTABLE_LEVELS	(ARM64_HW_PGTABLE_LEVELS(PHYS_MASK_SHIFT) - 1)
#else
#define SWAPPER_PGTABLE_LEVELS	(CONFIG_PGTABLE_LEVELS)
#define IDMAP_PGTABLE_LEVELS	(ARM64_HW_PGTABLE_LEVELS(PHYS_MASK_SHIFT))
#endif


/*
 * If KASLR is enabled, then an offset K is added to the kernel address
 * space. The bottom 21 bits of this offset are zero to guarantee 2MB
 * alignment for PA and VA.
 *
 * For each pagetable level of the swapper, we know that the shift will
 * be larger than 21 (for the 4KB granule case we use section maps thus
 * the smallest shift is actually 30) thus there is the possibility that
 * KASLR can increase the number of pagetable entries by 1, so we make
 * room for this extra entry.
 *
 * Note KASLR cannot increase the number of required entries for a level
 * by more than one because it increments both the virtual start and end
 * addresses equally (the extra entry comes from the case where the end
 * address is just pushed over a boundary and the start address isn't).
 */

#ifdef CONFIG_RANDOMIZE_BASE
#define EARLY_KASLR	(1)
#else
#define EARLY_KASLR	(0)
#endif

#define EARLY_ENTRIES(vstart, vend, shift) \
	((((vend) - 1) >> (shift)) - ((vstart) >> (shift)) + 1 + EARLY_KASLR)

#define EARLY_PGDS(vstart, vend) (EARLY_ENTRIES(vstart, vend, PGDIR_SHIFT))

#if SWAPPER_PGTABLE_LEVELS > 3
#define EARLY_PUDS(vstart, vend) (EARLY_ENTRIES(vstart, vend, PUD_SHIFT))
#else
#define EARLY_PUDS(vstart, vend) (0)
#endif

#if SWAPPER_PGTABLE_LEVELS > 2
#define EARLY_PMDS(vstart, vend) (EARLY_ENTRIES(vstart, vend, SWAPPER_TABLE_SHIFT))
#else
#define EARLY_PMDS(vstart, vend) (0)
#endif

#define EARLY_PAGES(vstart, vend) ( 1 			/* PGDIR page */				\
			+ EARLY_PGDS((vstart), (vend)) 	/* each PGDIR needs a next level page table */	\
			+ EARLY_PUDS((vstart), (vend))	/* each PUD needs a next level page table */	\
			+ EARLY_PMDS((vstart), (vend)))	/* each PMD needs a next level page table */
#define INIT_DIR_SIZE (PAGE_SIZE * EARLY_PAGES(KIMAGE_VADDR, _end))
#define IDMAP_DIR_SIZE		(IDMAP_PGTABLE_LEVELS * PAGE_SIZE)

/* Initial memory map size */
#if ARM64_KERNEL_USES_PMD_MAPS
#define SWAPPER_BLOCK_SHIFT	PMD_SHIFT
#define SWAPPER_BLOCK_SIZE	PMD_SIZE
#define SWAPPER_TABLE_SHIFT	PUD_SHIFT
#else
#define SWAPPER_BLOCK_SHIFT	PAGE_SHIFT
#define SWAPPER_BLOCK_SIZE	PAGE_SIZE
#define SWAPPER_TABLE_SHIFT	PMD_SHIFT
#endif

/*
 * Initial memory map attributes.
 */
#define SWAPPER_PTE_FLAGS	(PTE_TYPE_PAGE | PTE_AF | PTE_SHARED | PTE_UXN)
#define SWAPPER_PMD_FLAGS	(PMD_TYPE_SECT | PMD_SECT_AF | PMD_SECT_S | PMD_SECT_UXN)

#if ARM64_KERNEL_USES_PMD_MAPS
#define SWAPPER_MM_MMUFLAGS	(PMD_ATTRINDX(MT_NORMAL) | SWAPPER_PMD_FLAGS)
#else
#define SWAPPER_MM_MMUFLAGS	(PTE_ATTRINDX(MT_NORMAL) | SWAPPER_PTE_FLAGS)
#endif

/*
 * To make optimal use of block mappings when laying out the linear
 * mapping, round down the base of physical memory to a size that can
 * be mapped efficiently, i.e., either PUD_SIZE (4k granule) or PMD_SIZE
 * (64k granule), or a multiple that can be mapped using contiguous bits
 * in the page tables: 32 * PMD_SIZE (16k granule)
 */
#if defined(CONFIG_ARM64_4K_PAGES)
#define ARM64_MEMSTART_SHIFT		PUD_SHIFT
#elif defined(CONFIG_ARM64_16K_PAGES)
#define ARM64_MEMSTART_SHIFT		CONT_PMD_SHIFT
#else
#define ARM64_MEMSTART_SHIFT		PMD_SHIFT
#endif

/*
 * sparsemem vmemmap imposes an additional requirement on the alignment of
 * memstart_addr, due to the fact that the base of the vmemmap region
 * has a direct correspondence, and needs to appear sufficiently aligned
 * in the virtual address space.
 */
#if ARM64_MEMSTART_SHIFT < SECTION_SIZE_BITS
#define ARM64_MEMSTART_ALIGN	(1UL << SECTION_SIZE_BITS)
#else
#define ARM64_MEMSTART_ALIGN	(1UL << ARM64_MEMSTART_SHIFT)
#endif

#endif	/* __ASM_KERNEL_PGTABLE_H */

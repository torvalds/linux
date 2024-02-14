/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel page table mapping
 *
 * Copyright (C) 2015 ARM Ltd.
 */

#ifndef __ASM_KERNEL_PGTABLE_H
#define __ASM_KERNEL_PGTABLE_H

#include <asm/boot.h>
#include <asm/pgtable-hwdef.h>
#include <asm/sparsemem.h>

/*
 * The physical and virtual addresses of the start of the kernel image are
 * equal modulo 2 MiB (per the arm64 booting.txt requirements). Hence we can
 * use section mapping with 4K (section size = 2M) but not with 16K (section
 * size = 32M) or 64K (section size = 512M).
 */
#if defined(PMD_SIZE) && PMD_SIZE <= MIN_KIMG_ALIGN
#define SWAPPER_BLOCK_SHIFT	PMD_SHIFT
#define SWAPPER_SKIP_LEVEL	1
#else
#define SWAPPER_BLOCK_SHIFT	PAGE_SHIFT
#define SWAPPER_SKIP_LEVEL	0
#endif
#define SWAPPER_BLOCK_SIZE	(UL(1) << SWAPPER_BLOCK_SHIFT)
#define SWAPPER_TABLE_SHIFT	(SWAPPER_BLOCK_SHIFT + PAGE_SHIFT - 3)

#define SWAPPER_PGTABLE_LEVELS		(CONFIG_PGTABLE_LEVELS - SWAPPER_SKIP_LEVEL)

#define IDMAP_VA_BITS		48
#define IDMAP_LEVELS		ARM64_HW_PGTABLE_LEVELS(IDMAP_VA_BITS)
#define IDMAP_ROOT_LEVEL	(4 - IDMAP_LEVELS)

/*
 * A relocatable kernel may execute from an address that differs from the one at
 * which it was linked. In the worst case, its runtime placement may intersect
 * with two adjacent PGDIR entries, which means that an additional page table
 * may be needed at each subordinate level.
 */
#define EXTRA_PAGE	__is_defined(CONFIG_RELOCATABLE)

#define SPAN_NR_ENTRIES(vstart, vend, shift) \
	((((vend) - 1) >> (shift)) - ((vstart) >> (shift)) + 1)

#define EARLY_ENTRIES(vstart, vend, shift, add) \
	(SPAN_NR_ENTRIES(vstart, vend, shift) + (add))

#define EARLY_LEVEL(lvl, vstart, vend, add)	\
	(SWAPPER_PGTABLE_LEVELS > lvl ? EARLY_ENTRIES(vstart, vend, SWAPPER_BLOCK_SHIFT + lvl * (PAGE_SHIFT - 3), add) : 0)

#define EARLY_PAGES(vstart, vend, add) (1 	/* PGDIR page */				\
	+ EARLY_LEVEL(3, (vstart), (vend), add) /* each entry needs a next level page table */	\
	+ EARLY_LEVEL(2, (vstart), (vend), add)	/* each entry needs a next level page table */	\
	+ EARLY_LEVEL(1, (vstart), (vend), add))/* each entry needs a next level page table */
#define INIT_DIR_SIZE (PAGE_SIZE * (EARLY_PAGES(KIMAGE_VADDR, _end, EXTRA_PAGE) + EARLY_SEGMENT_EXTRA_PAGES))

/* the initial ID map may need two extra pages if it needs to be extended */
#if VA_BITS < 48
#define INIT_IDMAP_DIR_SIZE	((INIT_IDMAP_DIR_PAGES + 2) * PAGE_SIZE)
#else
#define INIT_IDMAP_DIR_SIZE	(INIT_IDMAP_DIR_PAGES * PAGE_SIZE)
#endif
#define INIT_IDMAP_DIR_PAGES	EARLY_PAGES(KIMAGE_VADDR, _end + MAX_FDT_SIZE + SWAPPER_BLOCK_SIZE, 1)

/* The number of segments in the kernel image (text, rodata, inittext, initdata, data+bss) */
#define KERNEL_SEGMENT_COUNT	5

#if SWAPPER_BLOCK_SIZE > SEGMENT_ALIGN
#define EARLY_SEGMENT_EXTRA_PAGES (KERNEL_SEGMENT_COUNT + 1)
#else
#define EARLY_SEGMENT_EXTRA_PAGES 0
#endif

/*
 * Initial memory map attributes.
 */
#define SWAPPER_PTE_FLAGS	(PTE_TYPE_PAGE | PTE_AF | PTE_SHARED | PTE_UXN)
#define SWAPPER_PMD_FLAGS	(PMD_TYPE_SECT | PMD_SECT_AF | PMD_SECT_S | PTE_UXN)

#ifdef CONFIG_ARM64_4K_PAGES
#define SWAPPER_RW_MMUFLAGS	(PMD_ATTRINDX(MT_NORMAL) | SWAPPER_PMD_FLAGS | PTE_WRITE)
#define SWAPPER_RX_MMUFLAGS	(SWAPPER_RW_MMUFLAGS | PMD_SECT_RDONLY)
#else
#define SWAPPER_RW_MMUFLAGS	(PTE_ATTRINDX(MT_NORMAL) | SWAPPER_PTE_FLAGS | PTE_WRITE)
#define SWAPPER_RX_MMUFLAGS	(SWAPPER_RW_MMUFLAGS | PTE_RDONLY)
#endif

#endif	/* __ASM_KERNEL_PGTABLE_H */

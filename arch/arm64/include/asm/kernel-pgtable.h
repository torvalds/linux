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
#define INIT_IDMAP_PGTABLE_LEVELS	(IDMAP_LEVELS - SWAPPER_SKIP_LEVEL)

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

#define EARLY_LEVEL(lvl, lvls, vstart, vend, add)	\
	(lvls > lvl ? EARLY_ENTRIES(vstart, vend, SWAPPER_BLOCK_SHIFT + lvl * (PAGE_SHIFT - 3), add) : 0)

#define EARLY_PAGES(lvls, vstart, vend, add) (1 	/* PGDIR page */				\
	+ EARLY_LEVEL(3, (lvls), (vstart), (vend), add) /* each entry needs a next level page table */	\
	+ EARLY_LEVEL(2, (lvls), (vstart), (vend), add)	/* each entry needs a next level page table */	\
	+ EARLY_LEVEL(1, (lvls), (vstart), (vend), add))/* each entry needs a next level page table */
#define INIT_DIR_SIZE (PAGE_SIZE * (EARLY_PAGES(SWAPPER_PGTABLE_LEVELS, KIMAGE_VADDR, _end, EXTRA_PAGE) \
				    + EARLY_SEGMENT_EXTRA_PAGES))

#define INIT_IDMAP_DIR_PAGES	(EARLY_PAGES(INIT_IDMAP_PGTABLE_LEVELS, KIMAGE_VADDR, _end, 1))
#define INIT_IDMAP_DIR_SIZE	((INIT_IDMAP_DIR_PAGES + EARLY_IDMAP_EXTRA_PAGES) * PAGE_SIZE)

#define INIT_IDMAP_FDT_PAGES	(EARLY_PAGES(INIT_IDMAP_PGTABLE_LEVELS, 0UL, UL(MAX_FDT_SIZE), 1) - 1)
#define INIT_IDMAP_FDT_SIZE	((INIT_IDMAP_FDT_PAGES + EARLY_IDMAP_EXTRA_FDT_PAGES) * PAGE_SIZE)

/* The number of segments in the kernel image (text, rodata, inittext, initdata, data+bss) */
#define KERNEL_SEGMENT_COUNT	5

#if SWAPPER_BLOCK_SIZE > SEGMENT_ALIGN
#define EARLY_SEGMENT_EXTRA_PAGES (KERNEL_SEGMENT_COUNT + 1)
/*
 * The initial ID map consists of the kernel image, mapped as two separate
 * segments, and may appear misaligned wrt the swapper block size. This means
 * we need 3 additional pages. The DT could straddle a swapper block boundary,
 * so it may need 2.
 */
#define EARLY_IDMAP_EXTRA_PAGES		3
#define EARLY_IDMAP_EXTRA_FDT_PAGES	2
#else
#define EARLY_SEGMENT_EXTRA_PAGES	0
#define EARLY_IDMAP_EXTRA_PAGES		0
#define EARLY_IDMAP_EXTRA_FDT_PAGES	0
#endif

#endif	/* __ASM_KERNEL_PGTABLE_H */

/* linux/drivers/media/video/samsung/fimg2d4x/fimg2d_cache.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>

#include "fimg2d.h"
#include "fimg2d_cache.h"

#define LV1_SHIFT		20
#define LV1_PT_SIZE		SZ_1M
#define LV2_PT_SIZE		SZ_1K
#define LV2_BASE_MASK		0x3ff
#define LV2_PT_MASK		0xff000
#define LV2_SHIFT		12
#define LV1_DESC_MASK		0x3
#define LV2_DESC_MASK		0x2

static inline unsigned long virt2phys(struct mm_struct *mm, unsigned long vaddr)
{
	unsigned long *pgd;
	unsigned long *lv1d, *lv2d;

	pgd = (unsigned long *)mm->pgd;

	lv1d = pgd + (vaddr >> LV1_SHIFT);

	if ((*lv1d & LV1_DESC_MASK) != 0x1) {
		fimg2d_debug("invalid LV1 descriptor, "
				"pgd %p lv1d 0x%lx vaddr 0x%lx\n",
				pgd, *lv1d, vaddr);
		return 0;
	}

	lv2d = (unsigned long *)phys_to_virt(*lv1d & ~LV2_BASE_MASK) +
				((vaddr & LV2_PT_MASK) >> LV2_SHIFT);

	if ((*lv2d & LV2_DESC_MASK) != 0x2) {
		fimg2d_debug("invalid LV2 descriptor, "
				"pgd %p lv2d 0x%lx vaddr 0x%lx\n",
				pgd, *lv2d, vaddr);
		return 0;
	}

	return (*lv2d & PAGE_MASK) | (vaddr & (PAGE_SIZE-1));
}

#ifdef CONFIG_OUTER_CACHE
void fimg2d_dma_sync_outer(struct mm_struct *mm, unsigned long vaddr,
					size_t size, enum cache_opr opr)
{
	int len;
	unsigned long cur, end, next, paddr;

	cur = vaddr;
	end = vaddr + size;

	if (opr == CACHE_CLEAN) {
		while (cur < end) {
			next = (cur + PAGE_SIZE) & PAGE_MASK;
			if (next > end)
				next = end;
			len = next - cur;

			paddr = virt2phys(mm, cur);
			if (paddr)
				outer_clean_range(paddr, paddr + len);
			cur += len;
		}
	} else if (opr == CACHE_FLUSH) {
		while (cur < end) {
			next = (cur + PAGE_SIZE) & PAGE_MASK;
			if (next > end)
				next = end;
			len = next - cur;

			paddr = virt2phys(mm, cur);
			if (paddr)
				outer_flush_range(paddr, paddr + len);
			cur += len;
		}
	}
}

void fimg2d_clean_outer_pagetable(struct mm_struct *mm, unsigned long vaddr,
					size_t size)
{
	unsigned long *pgd;
	unsigned long *lv1, *lv1end;
	unsigned long lv2pa;

	pgd = (unsigned long *)mm->pgd;

	lv1 = pgd + (vaddr >> LV1_SHIFT);
	lv1end = pgd + ((vaddr + size + LV1_PT_SIZE-1) >> LV1_SHIFT);

	/* clean level1 page table */
	outer_clean_range(virt_to_phys(lv1), virt_to_phys(lv1end));

	do {
		lv2pa = *lv1 & ~LV2_BASE_MASK;	/* lv2 pt base */
		/* clean level2 page table */
		outer_clean_range(lv2pa, lv2pa + LV2_PT_SIZE);
		lv1++;
	} while (lv1 != lv1end);
}

void fimg2d_clean_outer_pagetable_clone(unsigned long *pgd_clone,
					unsigned long vaddr, size_t size)
{
	unsigned long *pgd;
	unsigned long *lv1, *lv1end;
	unsigned long lv2pa;

	pgd = (unsigned long *)pgd_clone;

	lv1 = pgd + (vaddr >> LV1_SHIFT);
	lv1end = pgd + ((vaddr + size + LV1_PT_SIZE-1) >> LV1_SHIFT);

	/* clean level1 page table */
	outer_clean_range(virt_to_phys(lv1), virt_to_phys(lv1end));

	do {
		/* if page size is 4KB, clean level2 page table entry */
		if ((*lv1 & 0x3) == 0x1) {
			lv2pa = *lv1 & ~LV2_BASE_MASK;	/* lv2 pt base */
			/* clean level2 page table */
			outer_clean_range(lv2pa, lv2pa + LV2_PT_SIZE);
		}
		lv1++;
	} while (lv1 != lv1end);
}
#endif /* CONFIG_OUTER_CACHE */

void fimg2d_clean_inner_pagetable_clone(unsigned long *pgd_clone,
					unsigned long vaddr, size_t size)
{
	unsigned long *pgd;
	unsigned long *lv1, *lv1end;

	pgd = (unsigned long *)pgd_clone;

	lv1 = pgd + (vaddr >> LV1_SHIFT);
	lv1end = pgd + ((vaddr + size + LV1_PT_SIZE-1) >> LV1_SHIFT);
	fimg2d_dma_sync_inner((unsigned long)lv1,
			(unsigned int)lv1end - (unsigned int)lv1, DMA_TO_DEVICE);
}

enum pt_status fimg2d_check_pagetable(struct mm_struct *mm, unsigned long vaddr,
					size_t size)
{
	unsigned long *pgd;
	unsigned long *lv1d, *lv2d;

	pgd = (unsigned long *)mm->pgd;

	size += offset_in_page(vaddr);
	size = PAGE_ALIGN(size);

	while ((long)size > 0) {
		lv1d = pgd + (vaddr >> LV1_SHIFT);

		/*
		 * check level 1 descriptor
		 *	lv1 desc[1:0] = 00 --> fault
		 *	lv1 desc[1:0] = 01 --> page table
		 *	lv1 desc[1:0] = 10 --> section or supersection
		 *	lv1 desc[1:0] = 11 --> reserved
		 */
		if ((*lv1d & LV1_DESC_MASK) != 0x1) {
			fimg2d_debug("invalid LV1 descriptor, "
					"pgd %p lv1d 0x%lx vaddr 0x%lx\n",
					pgd, *lv1d, vaddr);
			return PT_FAULT;
		}

		lv2d = (unsigned long *)phys_to_virt(*lv1d & ~LV2_BASE_MASK) +
				((vaddr & LV2_PT_MASK) >> LV2_SHIFT);

		/*
		 * check level 2 descriptor
		 *	lv2 desc[1:0] = 00 --> fault
		 *	lv2 desc[1:0] = 01 --> 64k pgae
		 *	lv2 desc[1:0] = 1x --> 4k page
		 */
		if ((*lv2d & LV2_DESC_MASK) != 0x2) {
			fimg2d_debug("invalid LV2 descriptor, "
					"pgd %p lv2d 0x%lx vaddr 0x%lx\n",
					pgd, *lv2d, vaddr);
			return PT_FAULT;
		}

		vaddr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return PT_NORMAL;
}

#define PT_NS		0x0	// Non Secure
#define PT_AP		0x8c00	// Access permission
#define PT_ENTRY	0x2	// 1MB Page

enum pt_status fimg2d_migrate_pagetable(unsigned long *pgd_clone,
					unsigned long vaddr, unsigned long paddr, size_t size)
{
	unsigned long *pgd;
	unsigned long *lv1d;

	pgd = (unsigned long *)pgd_clone;

	size += vaddr & (SZ_1M - 1);
	size = ALIGN(size, SZ_1M);

	while ((long)size > 0) {
		lv1d = pgd + (vaddr >> LV1_SHIFT);

		*lv1d = (paddr & 0xfff00000) | PT_NS | PT_AP | PT_ENTRY;

		vaddr += SZ_1M;
		paddr += SZ_1M;
		size -= SZ_1M;
	}
	return PT_NORMAL;
}

void fimg2d_mmutable_value_replace(struct fimg2d_bltcmd *cmd,
					unsigned long fault_addr, unsigned long l2d_value)
{
	unsigned long *pgd;
	unsigned long *lv1d, *lv2d;

	pgd = (unsigned long *)cmd->ctx->mm->pgd;
	lv1d = pgd + (fault_addr >> LV1_SHIFT);

	lv2d = (unsigned long *)phys_to_virt(*lv1d & ~LV2_BASE_MASK) +
			((fault_addr & LV2_PT_MASK) >> LV2_SHIFT);

	*lv2d = l2d_value;

	flush_all_cpu_caches();
	fimg2d_clean_outer_pagetable(cmd->ctx->mm, fault_addr, 4);

	printk(KERN_INFO "MMU Level2 value replaced [0x%lx]", l2d_value);
}

/*
 * linux/arch/unicore32/include/asm/tlb.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_TLB_H__
#define __UNICORE_TLB_H__

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>

/*
 * TLB handling.  This allows us to remove pages from the page
 * tables, and efficiently handle the TLB issues.
 */
struct mmu_gather {
	struct mm_struct	*mm;
	unsigned int		fullmm;
	unsigned long		range_start;
	unsigned long		range_end;
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

static inline struct mmu_gather *
tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
	struct mmu_gather *tlb = &get_cpu_var(mmu_gathers);

	tlb->mm = mm;
	tlb->fullmm = full_mm_flush;

	return tlb;
}

static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	if (tlb->fullmm)
		flush_tlb_mm(tlb->mm);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	put_cpu_var(mmu_gathers);
}

/*
 * Memorize the range for the TLB flush.
 */
static inline void
tlb_remove_tlb_entry(struct mmu_gather *tlb, pte_t *ptep, unsigned long addr)
{
	if (!tlb->fullmm) {
		if (addr < tlb->range_start)
			tlb->range_start = addr;
		if (addr + PAGE_SIZE > tlb->range_end)
			tlb->range_end = addr + PAGE_SIZE;
	}
}

/*
 * In the case of tlb vma handling, we can optimise these away in the
 * case where we're doing a full MM flush.  When we're doing a munmap,
 * the vmas are adjusted to only cover the region to be torn down.
 */
static inline void
tlb_start_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm) {
		flush_cache_range(vma, vma->vm_start, vma->vm_end);
		tlb->range_start = TASK_SIZE;
		tlb->range_end = 0;
	}
}

static inline void
tlb_end_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm && tlb->range_end > 0)
		flush_tlb_range(vma, tlb->range_start, tlb->range_end);
}

#define tlb_remove_page(tlb, page)	free_page_and_swap_cache(page)
#define pte_free_tlb(tlb, ptep, addr)	pte_free((tlb)->mm, ptep)
#define pmd_free_tlb(tlb, pmdp, addr)	pmd_free((tlb)->mm, pmdp)
#define pud_free_tlb(tlb, x, addr)      do { } while (0)

#define tlb_migrate_finish(mm)		do { } while (0)

#endif

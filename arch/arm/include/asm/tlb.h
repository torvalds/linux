/*
 *  arch/arm/include/asm/tlb.h
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Experimentation shows that on a StrongARM, it appears to be faster
 *  to use the "invalidate whole tlb" rather than "invalidate single
 *  tlb" for this.
 *
 *  This appears true for both the process fork+exit case, as well as
 *  the munmap-large-area case.
 */
#ifndef __ASMARM_TLB_H
#define __ASMARM_TLB_H

#include <asm/cacheflush.h>

#ifndef CONFIG_MMU

#include <linux/pagemap.h>

#define tlb_flush(tlb)	((void) tlb)

#include <asm-generic/tlb.h>

#else /* !CONFIG_MMU */

#include <linux/swap.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/*
 * We need to delay page freeing for SMP as other CPUs can access pages
 * which have been removed but not yet had their TLB entries invalidated.
 * Also, as ARMv7 speculative prefetch can drag new entries into the TLB,
 * we need to apply this same delaying tactic to ensure correct operation.
 */
#if defined(CONFIG_SMP) || defined(CONFIG_CPU_32v7)
#define tlb_fast_mode(tlb)	0
#define FREE_PTE_NR		500
#else
#define tlb_fast_mode(tlb)	1
#define FREE_PTE_NR		0
#endif

/*
 * TLB handling.  This allows us to remove pages from the page
 * tables, and efficiently handle the TLB issues.
 */
struct mmu_gather {
	struct mm_struct	*mm;
	unsigned int		fullmm;
	struct vm_area_struct	*vma;
	unsigned long		range_start;
	unsigned long		range_end;
	unsigned int		nr;
	struct page		*pages[FREE_PTE_NR];
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

/*
 * This is unnecessarily complex.  There's three ways the TLB shootdown
 * code is used:
 *  1. Unmapping a range of vmas.  See zap_page_range(), unmap_region().
 *     tlb->fullmm = 0, and tlb_start_vma/tlb_end_vma will be called.
 *     tlb->vma will be non-NULL.
 *  2. Unmapping all vmas.  See exit_mmap().
 *     tlb->fullmm = 1, and tlb_start_vma/tlb_end_vma will be called.
 *     tlb->vma will be non-NULL.  Additionally, page tables will be freed.
 *  3. Unmapping argument pages.  See shift_arg_pages().
 *     tlb->fullmm = 0, but tlb_start_vma/tlb_end_vma will not be called.
 *     tlb->vma will be NULL.
 */
static inline void tlb_flush(struct mmu_gather *tlb)
{
	if (tlb->fullmm || !tlb->vma)
		flush_tlb_mm(tlb->mm);
	else if (tlb->range_end > 0) {
		flush_tlb_range(tlb->vma, tlb->range_start, tlb->range_end);
		tlb->range_start = TASK_SIZE;
		tlb->range_end = 0;
	}
}

static inline void tlb_add_flush(struct mmu_gather *tlb, unsigned long addr)
{
	if (!tlb->fullmm) {
		if (addr < tlb->range_start)
			tlb->range_start = addr;
		if (addr + PAGE_SIZE > tlb->range_end)
			tlb->range_end = addr + PAGE_SIZE;
	}
}

static inline void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush(tlb);
	if (!tlb_fast_mode(tlb)) {
		free_pages_and_swap_cache(tlb->pages, tlb->nr);
		tlb->nr = 0;
	}
}

static inline struct mmu_gather *
tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
	struct mmu_gather *tlb = &get_cpu_var(mmu_gathers);

	tlb->mm = mm;
	tlb->fullmm = full_mm_flush;
	tlb->vma = NULL;
	tlb->nr = 0;

	return tlb;
}

static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	tlb_flush_mmu(tlb);

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
	tlb_add_flush(tlb, addr);
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
		tlb->vma = vma;
		tlb->range_start = TASK_SIZE;
		tlb->range_end = 0;
	}
}

static inline void
tlb_end_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm)
		tlb_flush(tlb);
}

static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	if (tlb_fast_mode(tlb)) {
		free_page_and_swap_cache(page);
	} else {
		tlb->pages[tlb->nr++] = page;
		if (tlb->nr >= FREE_PTE_NR)
			tlb_flush_mmu(tlb);
	}
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pte,
	unsigned long addr)
{
	pgtable_page_dtor(pte);
	tlb_add_flush(tlb, addr);
	tlb_remove_page(tlb, pte);
}

#define pte_free_tlb(tlb, ptep, addr)	__pte_free_tlb(tlb, ptep, addr)
#define pmd_free_tlb(tlb, pmdp, addr)	pmd_free((tlb)->mm, pmdp)

#define tlb_migrate_finish(mm)		do { } while (0)

#endif /* CONFIG_MMU */
#endif

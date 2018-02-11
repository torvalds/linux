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

#define MMU_GATHER_BUNDLE	8

#ifdef CONFIG_HAVE_RCU_TABLE_FREE
static inline void __tlb_remove_table(void *_table)
{
	free_page_and_swap_cache((struct page *)_table);
}

struct mmu_table_batch {
	struct rcu_head		rcu;
	unsigned int		nr;
	void			*tables[0];
};

#define MAX_TABLE_BATCH		\
	((PAGE_SIZE - sizeof(struct mmu_table_batch)) / sizeof(void *))

extern void tlb_table_flush(struct mmu_gather *tlb);
extern void tlb_remove_table(struct mmu_gather *tlb, void *table);

#define tlb_remove_entry(tlb, entry)	tlb_remove_table(tlb, entry)
#else
#define tlb_remove_entry(tlb, entry)	tlb_remove_page(tlb, entry)
#endif /* CONFIG_HAVE_RCU_TABLE_FREE */

/*
 * TLB handling.  This allows us to remove pages from the page
 * tables, and efficiently handle the TLB issues.
 */
struct mmu_gather {
	struct mm_struct	*mm;
#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	struct mmu_table_batch	*batch;
	unsigned int		need_flush;
#endif
	unsigned int		fullmm;
	struct vm_area_struct	*vma;
	unsigned long		start, end;
	unsigned long		range_start;
	unsigned long		range_end;
	unsigned int		nr;
	unsigned int		max;
	struct page		**pages;
	struct page		*local[MMU_GATHER_BUNDLE];
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

static inline void __tlb_alloc_page(struct mmu_gather *tlb)
{
	unsigned long addr = __get_free_pages(GFP_NOWAIT | __GFP_NOWARN, 0);

	if (addr) {
		tlb->pages = (void *)addr;
		tlb->max = PAGE_SIZE / sizeof(struct page *);
	}
}

static inline void tlb_flush_mmu_tlbonly(struct mmu_gather *tlb)
{
	tlb_flush(tlb);
#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb_table_flush(tlb);
#endif
}

static inline void tlb_flush_mmu_free(struct mmu_gather *tlb)
{
	free_pages_and_swap_cache(tlb->pages, tlb->nr);
	tlb->nr = 0;
	if (tlb->pages == tlb->local)
		__tlb_alloc_page(tlb);
}

static inline void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush_mmu_tlbonly(tlb);
	tlb_flush_mmu_free(tlb);
}

static inline void
arch_tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
			unsigned long start, unsigned long end)
{
	tlb->mm = mm;
	tlb->fullmm = !(start | (end+1));
	tlb->start = start;
	tlb->end = end;
	tlb->vma = NULL;
	tlb->max = ARRAY_SIZE(tlb->local);
	tlb->pages = tlb->local;
	tlb->nr = 0;
	__tlb_alloc_page(tlb);

#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb->batch = NULL;
#endif
}

static inline void
arch_tlb_finish_mmu(struct mmu_gather *tlb,
			unsigned long start, unsigned long end, bool force)
{
	if (force) {
		tlb->range_start = start;
		tlb->range_end = end;
	}

	tlb_flush_mmu(tlb);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	if (tlb->pages != tlb->local)
		free_pages((unsigned long)tlb->pages, 0);
}

/*
 * Memorize the range for the TLB flush.
 */
static inline void
tlb_remove_tlb_entry(struct mmu_gather *tlb, pte_t *ptep, unsigned long addr)
{
	tlb_add_flush(tlb, addr);
}

#define tlb_remove_huge_tlb_entry(h, tlb, ptep, address)	\
	tlb_remove_tlb_entry(tlb, ptep, address)
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

static inline bool __tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	tlb->pages[tlb->nr++] = page;
	VM_WARN_ON(tlb->nr > tlb->max);
	if (tlb->nr == tlb->max)
		return true;
	return false;
}

static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	if (__tlb_remove_page(tlb, page))
		tlb_flush_mmu(tlb);
}

static inline bool __tlb_remove_page_size(struct mmu_gather *tlb,
					  struct page *page, int page_size)
{
	return __tlb_remove_page(tlb, page);
}

static inline void tlb_remove_page_size(struct mmu_gather *tlb,
					struct page *page, int page_size)
{
	return tlb_remove_page(tlb, page);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pte,
	unsigned long addr)
{
	pgtable_page_dtor(pte);

#ifdef CONFIG_ARM_LPAE
	tlb_add_flush(tlb, addr);
#else
	/*
	 * With the classic ARM MMU, a pte page has two corresponding pmd
	 * entries, each covering 1MB.
	 */
	addr &= PMD_MASK;
	tlb_add_flush(tlb, addr + SZ_1M - PAGE_SIZE);
	tlb_add_flush(tlb, addr + SZ_1M);
#endif

	tlb_remove_entry(tlb, pte);
}

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmdp,
				  unsigned long addr)
{
#ifdef CONFIG_ARM_LPAE
	tlb_add_flush(tlb, addr);
	tlb_remove_entry(tlb, virt_to_page(pmdp));
#endif
}

static inline void
tlb_remove_pmd_tlb_entry(struct mmu_gather *tlb, pmd_t *pmdp, unsigned long addr)
{
	tlb_add_flush(tlb, addr);
}

#define pte_free_tlb(tlb, ptep, addr)	__pte_free_tlb(tlb, ptep, addr)
#define pmd_free_tlb(tlb, pmdp, addr)	__pmd_free_tlb(tlb, pmdp, addr)
#define pud_free_tlb(tlb, pudp, addr)	pud_free((tlb)->mm, pudp)

#define tlb_migrate_finish(mm)		do { } while (0)

#define tlb_remove_check_page_size_change tlb_remove_check_page_size_change
static inline void tlb_remove_check_page_size_change(struct mmu_gather *tlb,
						     unsigned int page_size)
{
}

#endif /* CONFIG_MMU */
#endif

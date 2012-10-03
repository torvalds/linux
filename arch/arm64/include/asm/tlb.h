/*
 * Based on arch/arm/include/asm/tlb.h
 *
 * Copyright (C) 2002 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_TLB_H
#define __ASM_TLB_H

#include <linux/pagemap.h>
#include <linux/swap.h>

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

#define MMU_GATHER_BUNDLE	8

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
	unsigned int		max;
	struct page		**pages;
	struct page		*local[MMU_GATHER_BUNDLE];
};

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

static inline void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush(tlb);
	free_pages_and_swap_cache(tlb->pages, tlb->nr);
	tlb->nr = 0;
	if (tlb->pages == tlb->local)
		__tlb_alloc_page(tlb);
}

static inline void
tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm, unsigned int fullmm)
{
	tlb->mm = mm;
	tlb->fullmm = fullmm;
	tlb->vma = NULL;
	tlb->max = ARRAY_SIZE(tlb->local);
	tlb->pages = tlb->local;
	tlb->nr = 0;
	__tlb_alloc_page(tlb);
}

static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
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

/*
 * In the case of tlb vma handling, we can optimise these away in the
 * case where we're doing a full MM flush.  When we're doing a munmap,
 * the vmas are adjusted to only cover the region to be torn down.
 */
static inline void
tlb_start_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm) {
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

static inline int __tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	tlb->pages[tlb->nr++] = page;
	VM_BUG_ON(tlb->nr > tlb->max);
	return tlb->max - tlb->nr;
}

static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	if (!__tlb_remove_page(tlb, page))
		tlb_flush_mmu(tlb);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pte,
	unsigned long addr)
{
	pgtable_page_dtor(pte);
	tlb_add_flush(tlb, addr);
	tlb_remove_page(tlb, pte);
}

#ifndef CONFIG_ARM64_64K_PAGES
static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmdp,
				  unsigned long addr)
{
	tlb_add_flush(tlb, addr);
	tlb_remove_page(tlb, virt_to_page(pmdp));
}
#endif

#define pte_free_tlb(tlb, ptep, addr)	__pte_free_tlb(tlb, ptep, addr)
#define pmd_free_tlb(tlb, pmdp, addr)	__pmd_free_tlb(tlb, pmdp, addr)
#define pud_free_tlb(tlb, pudp, addr)	pud_free((tlb)->mm, pudp)

#define tlb_migrate_finish(mm)		do { } while (0)

#endif

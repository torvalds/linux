#ifndef __ASM_SH_TLB_H
#define __ASM_SH_TLB_H

#ifdef CONFIG_SUPERH64
# include "tlb_64.h"
#endif

#ifndef __ASSEMBLY__
#include <linux/pagemap.h>

#ifdef CONFIG_MMU
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/*
 * TLB handling.  This allows us to remove pages from the page
 * tables, and efficiently handle the TLB issues.
 */
struct mmu_gather {
	struct mm_struct	*mm;
	unsigned int		fullmm;
	unsigned long		start, end;
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

static inline void init_tlb_gather(struct mmu_gather *tlb)
{
	tlb->start = TASK_SIZE;
	tlb->end = 0;

	if (tlb->fullmm) {
		tlb->start = 0;
		tlb->end = TASK_SIZE;
	}
}

static inline struct mmu_gather *
tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
	struct mmu_gather *tlb = &get_cpu_var(mmu_gathers);

	tlb->mm = mm;
	tlb->fullmm = full_mm_flush;

	init_tlb_gather(tlb);

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

static inline void
tlb_remove_tlb_entry(struct mmu_gather *tlb, pte_t *ptep, unsigned long address)
{
	if (tlb->start > address)
		tlb->start = address;
	if (tlb->end < address + PAGE_SIZE)
		tlb->end = address + PAGE_SIZE;
}

/*
 * In the case of tlb vma handling, we can optimise these away in the
 * case where we're doing a full MM flush.  When we're doing a munmap,
 * the vmas are adjusted to only cover the region to be torn down.
 */
static inline void
tlb_start_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm)
		flush_cache_range(vma, vma->vm_start, vma->vm_end);
}

static inline void
tlb_end_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm && tlb->end) {
		flush_tlb_range(vma, tlb->start, tlb->end);
		init_tlb_gather(tlb);
	}
}

#define tlb_remove_page(tlb,page)	free_page_and_swap_cache(page)
#define pte_free_tlb(tlb, ptep)		pte_free((tlb)->mm, ptep)
#define pmd_free_tlb(tlb, pmdp)		pmd_free((tlb)->mm, pmdp)
#define pud_free_tlb(tlb, pudp)		pud_free((tlb)->mm, pudp)

#define tlb_migrate_finish(mm)		do { } while (0)

#else /* CONFIG_MMU */

#define tlb_start_vma(tlb, vma)				do { } while (0)
#define tlb_end_vma(tlb, vma)				do { } while (0)
#define __tlb_remove_tlb_entry(tlb, pte, address)	do { } while (0)
#define tlb_flush(tlb)					do { } while (0)

#include <asm-generic/tlb.h>

#endif /* CONFIG_MMU */
#endif /* __ASSEMBLY__ */
#endif /* __ASM_SH_TLB_H */

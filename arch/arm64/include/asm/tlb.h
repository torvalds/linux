/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/tlb.h
 *
 * Copyright (C) 2002 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_TLB_H
#define __ASM_TLB_H

#include <linux/pagemap.h>
#include <linux/swap.h>

static inline void __tlb_remove_table(void *_table)
{
	free_page_and_swap_cache((struct page *)_table);
}

#define tlb_flush tlb_flush
static void tlb_flush(struct mmu_gather *tlb);

#include <asm-generic/tlb.h>

/*
 * get the tlbi levels in arm64.  Default value is 0 if more than one
 * of cleared_* is set or neither is set.
 * Arm64 doesn't support p4ds now.
 */
static inline int tlb_get_level(struct mmu_gather *tlb)
{
	if (tlb->cleared_ptes && !(tlb->cleared_pmds ||
				   tlb->cleared_puds ||
				   tlb->cleared_p4ds))
		return 3;

	if (tlb->cleared_pmds && !(tlb->cleared_ptes ||
				   tlb->cleared_puds ||
				   tlb->cleared_p4ds))
		return 2;

	if (tlb->cleared_puds && !(tlb->cleared_ptes ||
				   tlb->cleared_pmds ||
				   tlb->cleared_p4ds))
		return 1;

	return 0;
}

static inline void tlb_flush(struct mmu_gather *tlb)
{
	struct vm_area_struct vma = TLB_FLUSH_VMA(tlb->mm, 0);
	bool last_level = !tlb->freed_tables;
	unsigned long stride = tlb_get_unmap_size(tlb);
	int tlb_level = tlb_get_level(tlb);

	/*
	 * If we're tearing down the address space then we only care about
	 * invalidating the walk-cache, since the ASID allocator won't
	 * reallocate our ASID without invalidating the entire TLB.
	 */
	if (tlb->fullmm) {
		if (!last_level)
			flush_tlb_mm(tlb->mm);
		return;
	}

	__flush_tlb_range(&vma, tlb->start, tlb->end, stride,
			  last_level, tlb_level);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pte,
				  unsigned long addr)
{
	pgtable_pte_page_dtor(pte);
	tlb_remove_table(tlb, pte);
}

#if CONFIG_PGTABLE_LEVELS > 2
static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmdp,
				  unsigned long addr)
{
	struct page *page = virt_to_page(pmdp);

	pgtable_pmd_page_dtor(page);
	tlb_remove_table(tlb, page);
}
#endif

#if CONFIG_PGTABLE_LEVELS > 3
static inline void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pudp,
				  unsigned long addr)
{
	tlb_remove_table(tlb, virt_to_page(pudp));
}
#endif

#endif

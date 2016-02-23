/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_PGALLOC_H
#define _ASM_TILE_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <asm/fixmap.h>
#include <asm/page.h>
#include <hv/hypervisor.h>

/* Bits for the size of the second-level page table. */
#define L2_KERNEL_PGTABLE_SHIFT _HV_LOG2_L2_SIZE(HPAGE_SHIFT, PAGE_SHIFT)

/* How big is a kernel L2 page table? */
#define L2_KERNEL_PGTABLE_SIZE (1UL << L2_KERNEL_PGTABLE_SHIFT)

/* We currently allocate user L2 page tables by page (unlike kernel L2s). */
#if L2_KERNEL_PGTABLE_SHIFT < PAGE_SHIFT
#define L2_USER_PGTABLE_SHIFT PAGE_SHIFT
#else
#define L2_USER_PGTABLE_SHIFT L2_KERNEL_PGTABLE_SHIFT
#endif

/* How many pages do we need, as an "order", for a user L2 page table? */
#define L2_USER_PGTABLE_ORDER (L2_USER_PGTABLE_SHIFT - PAGE_SHIFT)

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
#ifdef CONFIG_64BIT
	set_pte(pmdp, pmd);
#else
	set_pte(&pmdp->pud.pgd, pmd.pud.pgd);
#endif
}

static inline void pmd_populate_kernel(struct mm_struct *mm,
				       pmd_t *pmd, pte_t *ptep)
{
	set_pmd(pmd, ptfn_pmd(HV_CPA_TO_PTFN(__pa(ptep)),
			      __pgprot(_PAGE_PRESENT)));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t page)
{
	set_pmd(pmd, ptfn_pmd(HV_CPA_TO_PTFN(PFN_PHYS(page_to_pfn(page))),
			      __pgprot(_PAGE_PRESENT)));
}

/*
 * Allocate and free page tables.
 */

extern pgd_t *pgd_alloc(struct mm_struct *mm);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pgtable_t pgtable_alloc_one(struct mm_struct *mm, unsigned long address,
				   int order);
extern void pgtable_free(struct mm_struct *mm, struct page *pte, int order);

static inline pgtable_t pte_alloc_one(struct mm_struct *mm,
				      unsigned long address)
{
	return pgtable_alloc_one(mm, address, L2_USER_PGTABLE_ORDER);
}

static inline void pte_free(struct mm_struct *mm, struct page *pte)
{
	pgtable_free(mm, pte, L2_USER_PGTABLE_ORDER);
}

#define pmd_pgtable(pmd) pmd_page(pmd)

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return pfn_to_kaddr(page_to_pfn(pte_alloc_one(mm, address)));
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	BUG_ON((unsigned long)pte & (PAGE_SIZE-1));
	pte_free(mm, virt_to_page(pte));
}

extern void __pgtable_free_tlb(struct mmu_gather *tlb, struct page *pte,
			       unsigned long address, int order);
static inline void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte,
				  unsigned long address)
{
	__pgtable_free_tlb(tlb, pte, address, L2_USER_PGTABLE_ORDER);
}

#define check_pgt_cache()	do { } while (0)

/*
 * Get the small-page pte_t lowmem entry for a given pfn.
 * This may or may not be in use, depending on whether the initial
 * huge-page entry for the page has already been shattered.
 */
pte_t *get_prealloc_pte(unsigned long pfn);

/* During init, we can shatter kernel huge pages if needed. */
void shatter_pmd(pmd_t *pmd);

/* After init, a more complex technique is required. */
void shatter_huge_page(unsigned long addr);

#ifdef __tilegx__

#define pud_populate(mm, pud, pmd) \
  pmd_populate_kernel((mm), (pmd_t *)(pud), (pte_t *)(pmd))

/* Bits for the size of the L1 (intermediate) page table. */
#define L1_KERNEL_PGTABLE_SHIFT _HV_LOG2_L1_SIZE(HPAGE_SHIFT)

/* How big is a kernel L2 page table? */
#define L1_KERNEL_PGTABLE_SIZE (1UL << L1_KERNEL_PGTABLE_SHIFT)

/* We currently allocate L1 page tables by page. */
#if L1_KERNEL_PGTABLE_SHIFT < PAGE_SHIFT
#define L1_USER_PGTABLE_SHIFT PAGE_SHIFT
#else
#define L1_USER_PGTABLE_SHIFT L1_KERNEL_PGTABLE_SHIFT
#endif

/* How many pages do we need, as an "order", for an L1 page table? */
#define L1_USER_PGTABLE_ORDER (L1_USER_PGTABLE_SHIFT - PAGE_SHIFT)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *p = pgtable_alloc_one(mm, address, L1_USER_PGTABLE_ORDER);
	return (pmd_t *)page_to_virt(p);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmdp)
{
	pgtable_free(mm, virt_to_page(pmdp), L1_USER_PGTABLE_ORDER);
}

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmdp,
				  unsigned long address)
{
	__pgtable_free_tlb(tlb, virt_to_page(pmdp), address,
			   L1_USER_PGTABLE_ORDER);
}

#endif /* __tilegx__ */

#endif /* _ASM_TILE_PGALLOC_H */

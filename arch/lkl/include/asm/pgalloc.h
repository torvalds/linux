/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_PGALLOC_H
#define _LKL_PGALLOC_H

#include <linux/mm.h>
#include <linux/mmzone.h>

#include <asm-generic/pgalloc.h>

#ifdef CONFIG_MMU

static inline void pmd_populate_kernel(struct mm_struct *mm,
			pmd_t *pmd, pte_t *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE + (unsigned long) __pa(pte)));
}

static inline void pmd_populate(struct mm_struct *mm,
			pmd_t *pmd, pgtable_t pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE + (page_to_pfn(pte) << PAGE_SHIFT)));
}

#define pmd_pgtable(pmd) pmd_page(pmd)

extern pgd_t *pgd_alloc(struct mm_struct *mm);

#define __pte_free_tlb(tlb, pte, address) tlb_remove_page((tlb), (pte))

#define __pmd_free_tlb(tlb, pmd, address)			\
do {								\
	pagetable_pmd_dtor(virt_to_ptdesc(pmd));			\
	tlb_remove_page_ptdesc((tlb), virt_to_ptdesc(pmd));	\
} while (0)

#endif // CONFIG_MMU

#endif /* _LKL_PGALLOC_H */

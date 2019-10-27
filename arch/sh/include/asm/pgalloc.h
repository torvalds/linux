/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <asm/page.h>
#include <asm-generic/pgalloc.h>

extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

#if PAGETABLE_LEVELS > 2
extern void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd);
extern pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address);
extern void pmd_free(struct mm_struct *mm, pmd_t *pmd);
#endif

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t pte)
{
	set_pmd(pmd, __pmd((unsigned long)page_address(pte)));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

#define __pte_free_tlb(tlb,pte,addr)			\
do {							\
	pgtable_pte_page_dtor(pte);			\
	tlb_remove_page((tlb), (pte));			\
} while (0)

#if CONFIG_PGTABLE_LEVELS > 2
#define __pmd_free_tlb(tlb, pmdp, addr)			\
do {							\
	struct page *page = virt_to_page(pmdp);		\
	pgtable_pmd_page_dtor(page);			\
	tlb_remove_page((tlb), page);			\
} while (0);
#endif

#endif /* __ASM_SH_PGALLOC_H */

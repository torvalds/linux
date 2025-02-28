/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <linux/mm.h>
#include <asm/page.h>

#define __HAVE_ARCH_PMD_ALLOC_ONE
#define __HAVE_ARCH_PMD_FREE
#define __HAVE_ARCH_PGD_FREE
#include <asm-generic/pgalloc.h>

extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

#if PAGETABLE_LEVELS > 2
extern void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd);
extern pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address);
extern void pmd_free(struct mm_struct *mm, pmd_t *pmd);
#define __pmd_free_tlb(tlb, pmdp, addr)		pmd_free((tlb)->mm, (pmdp))
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

#define __pte_free_tlb(tlb, pte, addr)				\
do {								\
	pagetable_dtor(page_ptdesc(pte));			\
	tlb_remove_page_ptdesc((tlb), (page_ptdesc(pte)));	\
} while (0)

#endif /* __ASM_SH_PGALLOC_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <linux/quicklist.h>
#include <asm/page.h>

#define QUICK_PT 0	/* Other page table pages that are zero on free */

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

/*
 * Allocate and free page tables.
 */
static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	return quicklist_alloc(QUICK_PT, GFP_KERNEL, NULL);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct page *page;
	void *pg;

	pg = quicklist_alloc(QUICK_PT, GFP_KERNEL, NULL);
	if (!pg)
		return NULL;
	page = virt_to_page(pg);
	if (!pgtable_page_ctor(page)) {
		quicklist_free(QUICK_PT, NULL, pg);
		return NULL;
	}
	return page;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	quicklist_free(QUICK_PT, NULL, pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	quicklist_free_page(QUICK_PT, NULL, pte);
}

#define __pte_free_tlb(tlb,pte,addr)			\
do {							\
	pgtable_page_dtor(pte);				\
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

static inline void check_pgt_cache(void)
{
	quicklist_trim(QUICK_PT, NULL, 25, 16);
}

#endif /* __ASM_SH_PGALLOC_H */

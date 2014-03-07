#ifndef _ASM_SCORE_PGALLOC_H
#define _ASM_SCORE_PGALLOC_H

#include <linux/mm.h>
#include <linux/highmem.h>
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

#define pmd_pgtable(pmd)	pmd_page(pmd)

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret, *init;

	ret = (pgd_t *) __get_free_pages(GFP_KERNEL, PGD_ORDER);
	if (ret) {
		init = pgd_offset(&init_mm, 0UL);
		pgd_init((unsigned long)ret);
		memcpy(ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
		(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return ret;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_pages((unsigned long)pgd, PGD_ORDER);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
	unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO,
					PTE_ORDER);

	return pte;
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
	unsigned long address)
{
	struct page *pte;

	pte = alloc_pages(GFP_KERNEL | __GFP_REPEAT, PTE_ORDER);
	if (!pte)
		return NULL;
	clear_highpage(pte);
	if (!pgtable_page_ctor(pte)) {
		__free_page(pte);
		return NULL;
	}
	return pte;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_pages((unsigned long)pte, PTE_ORDER);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	__free_pages(pte, PTE_ORDER);
}

#define __pte_free_tlb(tlb, pte, buf)			\
do {							\
	pgtable_page_dtor(pte);				\
	tlb_remove_page((tlb), pte);			\
} while (0)

#define check_pgt_cache()		do {} while (0)

#endif /* _ASM_SCORE_PGALLOC_H */

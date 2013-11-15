#ifndef _METAG_PGALLOC_H
#define _METAG_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>

#define pmd_populate_kernel(mm, pmd, pte) \
	set_pmd(pmd, __pmd(_PAGE_TABLE | __pa(pte)))

#define pmd_populate(mm, pmd, pte) \
	set_pmd(pmd, __pmd(_PAGE_TABLE | page_to_phys(pte)))

#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * Allocate and free page tables.
 */
#ifdef CONFIG_METAG_META21_MMU
static inline void pgd_ctor(pgd_t *pgd)
{
	memcpy(pgd + USER_PTRS_PER_PGD,
	       swapper_pg_dir + USER_PTRS_PER_PGD,
	       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
}
#else
#define pgd_ctor(x)	do { } while (0)
#endif

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)get_zeroed_page(GFP_KERNEL);
	if (pgd)
		pgd_ctor(pgd);
	return pgd;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	pte_t *pte = (pte_t *)__get_free_page(GFP_KERNEL | __GFP_REPEAT |
					      __GFP_ZERO);
	return pte;
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm,
				      unsigned long address)
{
	struct page *pte;
	pte = alloc_pages(GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO, 0);
	if (!pte)
		return NULL;
	if (!pgtable_page_ctor(pte)) {
		__free_page(pte);
		return NULL;
	}
	return pte;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	__free_page(pte);
}

#define __pte_free_tlb(tlb, pte, addr)				\
	do {							\
		pgtable_page_dtor(pte);				\
		tlb_remove_page((tlb), (pte));			\
	} while (0)

#define check_pgt_cache()	do { } while (0)

#endif

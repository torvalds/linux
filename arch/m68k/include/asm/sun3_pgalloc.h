/* sun3_pgalloc.h --
 * reorganization around 2.3.39, routines moved from sun3_pgtable.h
 *
 *
 * 02/27/2002 -- Modified to support "highpte" implementation in 2.5.5 (Sam)
 *
 * moved 1/26/2000 Sam Creasey
 */

#ifndef _SUN3_PGALLOC_H
#define _SUN3_PGALLOC_H

#include <asm/tlb.h>

extern const char bad_pmd_string[];

#define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); })


static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
        free_page((unsigned long) pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t page)
{
	pgtable_page_dtor(page);
        __free_page(page);
}

#define __pte_free_tlb(tlb,pte,addr)			\
do {							\
	pgtable_page_dtor(pte);				\
	tlb_remove_page((tlb), pte);			\
} while (0)

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	unsigned long page = __get_free_page(GFP_KERNEL);

	if (!page)
		return NULL;

	memset((void *)page, 0, PAGE_SIZE);
	return (pte_t *) (page);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm,
					unsigned long address)
{
        struct page *page = alloc_pages(GFP_KERNEL, 0);

	if (page == NULL)
		return NULL;

	clear_highpage(page);
	if (!pgtable_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}
	return page;

}

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_val(*pmd) = __pa((unsigned long)pte);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t page)
{
	pmd_val(*pmd) = __pa((unsigned long)page_address(page));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
#define pmd_free(mm, x)			do { } while (0)
#define __pmd_free_tlb(tlb, x, addr)	do { } while (0)

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
        free_page((unsigned long) pgd);
}

static inline pgd_t * pgd_alloc(struct mm_struct *mm)
{
     pgd_t *new_pgd;

     new_pgd = (pgd_t *)get_zeroed_page(GFP_KERNEL);
     memcpy(new_pgd, swapper_pg_dir, PAGE_SIZE);
     memset(new_pgd, 0, (PAGE_OFFSET >> PGDIR_SHIFT));
     return new_pgd;
}

#define pgd_populate(mm, pmd, pte) BUG()

#endif /* SUN3_PGALLOC_H */

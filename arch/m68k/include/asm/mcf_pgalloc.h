/* SPDX-License-Identifier: GPL-2.0 */
#ifndef M68K_MCF_PGALLOC_H
#define M68K_MCF_PGALLOC_H

#include <asm/tlb.h>
#include <asm/tlbflush.h>

extern inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long) pte);
}

extern const char bad_pmd_string[];

extern inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	unsigned long page = __get_free_page(GFP_DMA);

	if (!page)
		return NULL;

	memset((void *)page, 0, PAGE_SIZE);
	return (pte_t *) (page);
}

extern inline pmd_t *pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

#define pmd_populate(mm, pmd, pte) (pmd_val(*pmd) = (unsigned long)(pte))

#define pmd_populate_kernel pmd_populate

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pgtable,
				  unsigned long address)
{
	struct page *page = virt_to_page(pgtable);

	pgtable_pte_page_dtor(page);
	__free_page(page);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct page *page = alloc_pages(GFP_DMA, 0);
	pte_t *pte;

	if (!page)
		return NULL;
	if (!pgtable_pte_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}

	pte = page_address(page);
	clear_page(pte);

	return pte;
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pgtable)
{
	struct page *page = virt_to_page(pgtable);

	pgtable_pte_page_dtor(page);
	__free_page(page);
}

/*
 * In our implementation, each pgd entry contains 1 pmd that is never allocated
 * or freed.  pgd_present is always 1, so this should never be called. -NL
 */
#define pmd_free(mm, pmd) BUG()

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long) pgd);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *new_pgd;

	new_pgd = (pgd_t *)__get_free_page(GFP_DMA | __GFP_NOWARN);
	if (!new_pgd)
		return NULL;
	memcpy(new_pgd, swapper_pg_dir, PTRS_PER_PGD * sizeof(pgd_t));
	memset(new_pgd, 0, PAGE_OFFSET >> PGDIR_SHIFT);
	return new_pgd;
}

#endif /* M68K_MCF_PGALLOC_H */

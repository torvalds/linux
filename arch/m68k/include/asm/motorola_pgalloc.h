/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MOTOROLA_PGALLOC_H
#define _MOTOROLA_PGALLOC_H

#include <asm/tlb.h>
#include <asm/tlbflush.h>

extern pmd_t *get_pointer_table(void);
extern int free_pointer_table(pmd_t *);

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_ZERO);
	if (pte) {
		__flush_page_to_ram(pte);
		flush_tlb_kernel_page(pte);
		nocache_page(pte);
	}

	return pte;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	cache_page(pte);
	free_page((unsigned long) pte);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct page *page;
	pte_t *pte;

	page = alloc_pages(GFP_KERNEL|__GFP_ZERO, 0);
	if(!page)
		return NULL;
	if (!pgtable_pte_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}

	pte = kmap(page);
	__flush_page_to_ram(pte);
	flush_tlb_kernel_page(pte);
	nocache_page(pte);
	kunmap(page);
	return page;
}

static inline void pte_free(struct mm_struct *mm, pgtable_t page)
{
	pgtable_pte_page_dtor(page);
	cache_page(kmap(page));
	kunmap(page);
	__free_page(page);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t page,
				  unsigned long address)
{
	pgtable_pte_page_dtor(page);
	cache_page(kmap(page));
	kunmap(page);
	__free_page(page);
}


static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return get_pointer_table();
}

static inline int pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	return free_pointer_table(pmd);
}

static inline int __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
				 unsigned long address)
{
	return free_pointer_table(pmd);
}


static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	pmd_free(mm, (pmd_t *)pgd);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t *)get_pointer_table();
}


static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_set(pmd, pte);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t page)
{
	pmd_set(pmd, page_address(page));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmd)
{
	pgd_set(pgd, pmd);
}

#endif /* _MOTOROLA_PGALLOC_H */

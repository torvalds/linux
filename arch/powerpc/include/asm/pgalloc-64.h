#ifndef _ASM_POWERPC_PGALLOC_64_H
#define _ASM_POWERPC_PGALLOC_64_H
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>

/*
 * Functions that deal with pagetables that could be at any level of
 * the table need to be passed an "index_size" so they know how to
 * handle allocation.  For PTE pages (which are linked to a struct
 * page for now, and drawn from the main get_free_pages() pool), the
 * allocation size will be (2^index_size * sizeof(pointer)) and
 * allocations are drawn from the kmem_cache in PGT_CACHE(index_size).
 *
 * The maximum index size needs to be big enough to allow any
 * pagetable sizes we need, but small enough to fit in the low bits of
 * any page table pointer.  In other words all pagetables, even tiny
 * ones, must be aligned to allow at least enough low 0 bits to
 * contain this value.  This value is also used as a mask, so it must
 * be one less than a power of two.
 */
#define MAX_PGTABLE_INDEX_SIZE	0xf

#ifndef CONFIG_PPC_SUBPAGE_PROT
static inline void subpage_prot_free(pgd_t *pgd) {}
#endif

extern struct kmem_cache *pgtable_cache[];
#define PGT_CACHE(shift) (pgtable_cache[(shift)-1])

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(PGT_CACHE(PGD_INDEX_SIZE), GFP_KERNEL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	subpage_prot_free(pgd);
	kmem_cache_free(PGT_CACHE(PGD_INDEX_SIZE), pgd);
}

#ifndef CONFIG_PPC_64K_PAGES

#define pgd_populate(MM, PGD, PUD)	pgd_set(PGD, PUD)

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(PGT_CACHE(PUD_INDEX_SIZE),
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	kmem_cache_free(PGT_CACHE(PUD_INDEX_SIZE), pud);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_set(pud, (unsigned long)pmd);
}

#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))
#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, (unsigned long)(pte))
#define pmd_pgtable(pmd) pmd_page(pmd)


#else /* CONFIG_PPC_64K_PAGES */

#define pud_populate(mm, pud, pmd)	pud_set(pud, (unsigned long)pmd)

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	pmd_set(pmd, (unsigned long)pte);
}

#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))
#define pmd_pgtable(pmd) pmd_page(pmd)

#endif /* CONFIG_PPC_64K_PAGES */

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(PGT_CACHE(PMD_INDEX_SIZE),
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	kmem_cache_free(PGT_CACHE(PMD_INDEX_SIZE), pmd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
        return (pte_t *)__get_free_page(GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm,
					unsigned long address)
{
	struct page *page;
	pte_t *pte;

	pte = pte_alloc_one_kernel(mm, address);
	if (!pte)
		return NULL;
	page = virt_to_page(pte);
	pgtable_page_ctor(page);
	return page;
}

static inline void pgtable_free(void *table, unsigned index_size)
{
	if (!index_size)
		free_page((unsigned long)table);
	else {
		BUG_ON(index_size > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(index_size), table);
	}
}

#define __pmd_free_tlb(tlb, pmd, addr)		      \
	pgtable_free_tlb(tlb, pmd, PMD_INDEX_SIZE)
#ifndef CONFIG_PPC_64K_PAGES
#define __pud_free_tlb(tlb, pud, addr)		      \
	pgtable_free_tlb(tlb, pud, PUD_INDEX_SIZE)

#endif /* CONFIG_PPC_64K_PAGES */

#define check_pgt_cache()	do { } while (0)

#endif /* _ASM_POWERPC_PGALLOC_64_H */

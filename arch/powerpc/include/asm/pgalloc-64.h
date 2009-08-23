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

#ifndef CONFIG_PPC_SUBPAGE_PROT
static inline void subpage_prot_free(pgd_t *pgd) {}
#endif

extern struct kmem_cache *pgtable_cache[];

#define PGD_CACHE_NUM		0
#define PUD_CACHE_NUM		1
#define PMD_CACHE_NUM		1
#define HUGEPTE_CACHE_NUM	2
#define PTE_NONCACHE_NUM	7  /* from GFP rather than kmem_cache */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(pgtable_cache[PGD_CACHE_NUM], GFP_KERNEL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	subpage_prot_free(pgd);
	kmem_cache_free(pgtable_cache[PGD_CACHE_NUM], pgd);
}

#ifndef CONFIG_PPC_64K_PAGES

#define pgd_populate(MM, PGD, PUD)	pgd_set(PGD, PUD)

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache[PUD_CACHE_NUM],
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	kmem_cache_free(pgtable_cache[PUD_CACHE_NUM], pud);
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
	return kmem_cache_alloc(pgtable_cache[PMD_CACHE_NUM],
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	kmem_cache_free(pgtable_cache[PMD_CACHE_NUM], pmd);
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

static inline void pgtable_free(pgtable_free_t pgf)
{
	void *p = (void *)(pgf.val & ~PGF_CACHENUM_MASK);
	int cachenum = pgf.val & PGF_CACHENUM_MASK;

	if (cachenum == PTE_NONCACHE_NUM)
		free_page((unsigned long)p);
	else
		kmem_cache_free(pgtable_cache[cachenum], p);
}

#define __pmd_free_tlb(tlb, pmd,addr)		      \
	pgtable_free_tlb(tlb, pgtable_free_cache(pmd, \
		PMD_CACHE_NUM, PMD_TABLE_SIZE-1))
#ifndef CONFIG_PPC_64K_PAGES
#define __pud_free_tlb(tlb, pud, addr)		      \
	pgtable_free_tlb(tlb, pgtable_free_cache(pud, \
		PUD_CACHE_NUM, PUD_TABLE_SIZE-1))
#endif /* CONFIG_PPC_64K_PAGES */

#define check_pgt_cache()	do { } while (0)

#endif /* _ASM_POWERPC_PGALLOC_64_H */

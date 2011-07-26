#ifndef _SPARC64_PGALLOC_H
#define _SPARC64_PGALLOC_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/spitfire.h>
#include <asm/cpudata.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

/* Page table allocation/freeing. */

extern struct kmem_cache *pgtable_cache;

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(pgtable_cache, GFP_KERNEL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	kmem_cache_free(pgtable_cache, pgd);
}

#define pud_populate(MM, PUD, PMD)	pud_set(PUD, PMD)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache,
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	kmem_cache_free(pgtable_cache, pmd);
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

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptepage)
{
	pgtable_page_dtor(ptepage);
	__free_page(ptepage);
}

#define pmd_populate_kernel(MM, PMD, PTE)	pmd_set(PMD, PTE)
#define pmd_populate(MM,PMD,PTE_PAGE)		\
	pmd_populate_kernel(MM,PMD,page_address(PTE_PAGE))
#define pmd_pgtable(pmd) pmd_page(pmd)

#define check_pgt_cache()	do { } while (0)

#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, pte)
#define __pmd_free_tlb(tlb, pmd, addr)	pmd_free((tlb)->mm, pmd)

#endif /* _SPARC64_PGALLOC_H */

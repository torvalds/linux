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

static inline void __pgd_populate(pgd_t *pgd, pud_t *pud)
{
	pgd_set(pgd, pud);
}

#define pgd_populate(MM, PGD, PUD)	__pgd_populate(PGD, PUD)

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(pgtable_cache, GFP_KERNEL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	kmem_cache_free(pgtable_cache, pgd);
}

static inline void __pud_populate(pud_t *pud, pmd_t *pmd)
{
	pud_set(pud, pmd);
}

#define pud_populate(MM, PUD, PMD)	__pud_populate(PUD, PMD)

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache,
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	kmem_cache_free(pgtable_cache, pud);
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache,
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	kmem_cache_free(pgtable_cache, pmd);
}

pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
			    unsigned long address);
pgtable_t pte_alloc_one(struct mm_struct *mm,
			unsigned long address);
void pte_free_kernel(struct mm_struct *mm, pte_t *pte);
void pte_free(struct mm_struct *mm, pgtable_t ptepage);

#define pmd_populate_kernel(MM, PMD, PTE)	pmd_set(MM, PMD, PTE)
#define pmd_populate(MM, PMD, PTE)		pmd_set(MM, PMD, PTE)
#define pmd_pgtable(PMD)			((pte_t *)__pmd_page(PMD))

#define check_pgt_cache()	do { } while (0)

void pgtable_free(void *table, bool is_page);

#ifdef CONFIG_SMP

struct mmu_gather;
void tlb_remove_table(struct mmu_gather *, void *);

static inline void pgtable_free_tlb(struct mmu_gather *tlb, void *table, bool is_page)
{
	unsigned long pgf = (unsigned long)table;
	if (is_page)
		pgf |= 0x1UL;
	tlb_remove_table(tlb, (void *)pgf);
}

static inline void __tlb_remove_table(void *_table)
{
	void *table = (void *)((unsigned long)_table & ~0x1UL);
	bool is_page = false;

	if ((unsigned long)_table & 0x1UL)
		is_page = true;
	pgtable_free(table, is_page);
}
#else /* CONFIG_SMP */
static inline void pgtable_free_tlb(struct mmu_gather *tlb, void *table, bool is_page)
{
	pgtable_free(table, is_page);
}
#endif /* !CONFIG_SMP */

static inline void __pte_free_tlb(struct mmu_gather *tlb, pte_t *pte,
				  unsigned long address)
{
	pgtable_free_tlb(tlb, pte, true);
}

#define __pmd_free_tlb(tlb, pmd, addr)		      \
	pgtable_free_tlb(tlb, pmd, false)

#define __pud_free_tlb(tlb, pud, addr)		      \
	pgtable_free_tlb(tlb, pud, false)

#endif /* _SPARC64_PGALLOC_H */

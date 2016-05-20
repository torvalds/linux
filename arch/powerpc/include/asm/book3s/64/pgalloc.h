#ifndef _ASM_POWERPC_BOOK3S_64_PGALLOC_H
#define _ASM_POWERPC_BOOK3S_64_PGALLOC_H
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>

struct vmemmap_backing {
	struct vmemmap_backing *list;
	unsigned long phys;
	unsigned long virt_addr;
};
extern struct vmemmap_backing *vmemmap_list;

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

extern struct kmem_cache *pgtable_cache[];
#define PGT_CACHE(shift) ({				\
			BUG_ON(!(shift));		\
			pgtable_cache[(shift) - 1];	\
		})

#define PGALLOC_GFP GFP_KERNEL | __GFP_NOTRACK | __GFP_REPEAT | __GFP_ZERO

extern pte_t *pte_fragment_alloc(struct mm_struct *, unsigned long, int);
extern void pte_fragment_free(unsigned long *, int);
extern void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift);
#ifdef CONFIG_SMP
extern void __tlb_remove_table(void *_table);
#endif

static inline pgd_t *radix__pgd_alloc(struct mm_struct *mm)
{
#ifdef CONFIG_PPC_64K_PAGES
	return (pgd_t *)__get_free_page(PGALLOC_GFP);
#else
	struct page *page;
	page = alloc_pages(PGALLOC_GFP, 4);
	if (!page)
		return NULL;
	return (pgd_t *) page_address(page);
#endif
}

static inline void radix__pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
#ifdef CONFIG_PPC_64K_PAGES
	free_page((unsigned long)pgd);
#else
	free_pages((unsigned long)pgd, 4);
#endif
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	if (radix_enabled())
		return radix__pgd_alloc(mm);
	return kmem_cache_alloc(PGT_CACHE(PGD_INDEX_SIZE), GFP_KERNEL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	if (radix_enabled())
		return radix__pgd_free(mm, pgd);
	kmem_cache_free(PGT_CACHE(PGD_INDEX_SIZE), pgd);
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	pgd_set(pgd, __pgtable_ptr_val(pud) | PGD_VAL_BITS);
}

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
	pud_set(pud, __pgtable_ptr_val(pmd) | PUD_VAL_BITS);
}

static inline void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud,
                                  unsigned long address)
{
        pgtable_free_tlb(tlb, pud, PUD_INDEX_SIZE);
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(PGT_CACHE(PMD_CACHE_INDEX),
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	kmem_cache_free(PGT_CACHE(PMD_CACHE_INDEX), pmd);
}

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
                                  unsigned long address)
{
        return pgtable_free_tlb(tlb, pmd, PMD_CACHE_INDEX);
}

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	pmd_set(pmd, __pgtable_ptr_val(pte) | PMD_VAL_BITS);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t pte_page)
{
	pmd_set(pmd, __pgtable_ptr_val(pte_page) | PMD_VAL_BITS);
}

static inline pgtable_t pmd_pgtable(pmd_t pmd)
{
	return (pgtable_t)pmd_page_vaddr(pmd);
}

#ifdef CONFIG_PPC_4K_PAGES
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
	if (!pgtable_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}
	return pte;
}
#else /* if CONFIG_PPC_64K_PAGES */

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return (pte_t *)pte_fragment_alloc(mm, address, 1);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm,
				      unsigned long address)
{
	return (pgtable_t)pte_fragment_alloc(mm, address, 0);
}
#endif

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	pte_fragment_free((unsigned long *)pte, 1);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptepage)
{
	pte_fragment_free((unsigned long *)ptepage, 0);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t table,
				  unsigned long address)
{
	tlb_flush_pgtable(tlb, address);
	pgtable_free_tlb(tlb, table, 0);
}

#define check_pgt_cache()	do { } while (0)

#endif /* _ASM_POWERPC_BOOK3S_64_PGALLOC_H */

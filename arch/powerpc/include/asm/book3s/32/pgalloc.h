/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_PGALLOC_H
#define _ASM_POWERPC_BOOK3S_32_PGALLOC_H

#include <linux/threads.h>
#include <linux/slab.h>

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

extern void __bad_pte(pmd_t *pmd);

extern struct kmem_cache *pgtable_cache[];
#define PGT_CACHE(shift) pgtable_cache[shift]

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(PGT_CACHE(PGD_INDEX_SIZE),
			pgtable_gfp_flags(mm, GFP_KERNEL));
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	kmem_cache_free(PGT_CACHE(PGD_INDEX_SIZE), pgd);
}

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
/* #define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); }) */
#define pmd_free(mm, x) 		do { } while (0)
#define __pmd_free_tlb(tlb,x,a)		do { } while (0)
/* #define pgd_populate(mm, pmd, pte)      BUG() */

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp,
				       pte_t *pte)
{
	*pmdp = __pmd(__pa(pte) | _PMD_PRESENT);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pte_page)
{
	*pmdp = __pmd(__pa(pte_page) | _PMD_PRESENT);
}

#define pmd_pgtable(pmd) ((pgtable_t)pmd_page_vaddr(pmd))

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr);
extern pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long addr);
void pte_frag_destroy(void *pte_frag);
pte_t *pte_fragment_alloc(struct mm_struct *mm, unsigned long vmaddr, int kernel);
void pte_fragment_free(unsigned long *table, int kernel);

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	pte_fragment_free((unsigned long *)pte, 1);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptepage)
{
	pte_fragment_free((unsigned long *)ptepage, 0);
}

static inline void pgtable_free(void *table, unsigned index_size)
{
	if (!index_size) {
		pte_fragment_free((unsigned long *)table, 0);
	} else {
		BUG_ON(index_size > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(index_size), table);
	}
}

#define check_pgt_cache()	do { } while (0)
#define get_hugepd_cache_index(x)  (x)

#ifdef CONFIG_SMP
static inline void pgtable_free_tlb(struct mmu_gather *tlb,
				    void *table, int shift)
{
	unsigned long pgf = (unsigned long)table;
	BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
	pgf |= shift;
	tlb_remove_table(tlb, (void *)pgf);
}

static inline void __tlb_remove_table(void *_table)
{
	void *table = (void *)((unsigned long)_table & ~MAX_PGTABLE_INDEX_SIZE);
	unsigned shift = (unsigned long)_table & MAX_PGTABLE_INDEX_SIZE;

	pgtable_free(table, shift);
}
#else
static inline void pgtable_free_tlb(struct mmu_gather *tlb,
				    void *table, int shift)
{
	pgtable_free(table, shift);
}
#endif

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t table,
				  unsigned long address)
{
	pgtable_free_tlb(tlb, table, 0);
}
#endif /* _ASM_POWERPC_BOOK3S_32_PGALLOC_H */

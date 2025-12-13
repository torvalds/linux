/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_PGALLOC_H
#define _ASM_POWERPC_BOOK3S_32_PGALLOC_H

#include <linux/threads.h>
#include <linux/slab.h>

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = kmem_cache_alloc(PGT_CACHE(PGD_INDEX_SIZE),
				      pgtable_gfp_flags(mm, GFP_KERNEL));

#ifdef CONFIG_PPC_BOOK3S_603
	memcpy(pgd + USER_PTRS_PER_PGD, swapper_pg_dir + USER_PTRS_PER_PGD,
	       (MAX_PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
#endif
	return pgd;
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

static inline void pgtable_free(void *table, unsigned index_size)
{
	if (!index_size) {
		pte_fragment_free((unsigned long *)table, 0);
	} else {
		BUG_ON(index_size > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(index_size), table);
	}
}

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

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t table,
				  unsigned long address)
{
	pgtable_free_tlb(tlb, table, 0);
}
#endif /* _ASM_POWERPC_BOOK3S_32_PGALLOC_H */

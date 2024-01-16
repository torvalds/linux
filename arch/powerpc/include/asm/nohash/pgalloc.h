/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_PGALLOC_H
#define _ASM_POWERPC_NOHASH_PGALLOC_H

#include <linux/mm.h>
#include <linux/slab.h>

extern void tlb_remove_table(struct mmu_gather *tlb, void *table);
#ifdef CONFIG_PPC64
extern void tlb_flush_pgtable(struct mmu_gather *tlb, unsigned long address);
#else
/* 44x etc which is BOOKE not BOOK3E */
static inline void tlb_flush_pgtable(struct mmu_gather *tlb,
				     unsigned long address)
{

}
#endif /* !CONFIG_PPC_BOOK3E_64 */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(PGT_CACHE(PGD_INDEX_SIZE),
			pgtable_gfp_flags(mm, GFP_KERNEL));
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	kmem_cache_free(PGT_CACHE(PGD_INDEX_SIZE), pgd);
}

#ifdef CONFIG_PPC64
#include <asm/nohash/64/pgalloc.h>
#else
#include <asm/nohash/32/pgalloc.h>
#endif

static inline void pgtable_free(void *table, int shift)
{
	if (!shift) {
		pte_fragment_free((unsigned long *)table, 0);
	} else {
		BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(shift), table);
	}
}

#define get_hugepd_cache_index(x)	(x)

static inline void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift)
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
	tlb_flush_pgtable(tlb, address);
	pgtable_free_tlb(tlb, table, 0);
}
#endif /* _ASM_POWERPC_NOHASH_PGALLOC_H */

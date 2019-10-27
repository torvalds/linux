/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGALLOC_H
#define _ASM_POWERPC_PGALLOC_H

#include <linux/mm.h>

#ifndef MODULE
static inline gfp_t pgtable_gfp_flags(struct mm_struct *mm, gfp_t gfp)
{
	if (unlikely(mm == &init_mm))
		return gfp;
	return gfp | __GFP_ACCOUNT;
}
#else /* !MODULE */
static inline gfp_t pgtable_gfp_flags(struct mm_struct *mm, gfp_t gfp)
{
	return gfp | __GFP_ACCOUNT;
}
#endif /* MODULE */

#define PGALLOC_GFP (GFP_KERNEL | __GFP_ZERO)

pte_t *pte_fragment_alloc(struct mm_struct *mm, int kernel);

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	return (pte_t *)pte_fragment_alloc(mm, 1);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return (pgtable_t)pte_fragment_alloc(mm, 0);
}

void pte_frag_destroy(void *pte_frag);
void pte_fragment_free(unsigned long *table, int kernel);

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	pte_fragment_free((unsigned long *)pte, 1);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptepage)
{
	pte_fragment_free((unsigned long *)ptepage, 0);
}

/*
 * Functions that deal with pagetables that could be at any level of
 * the table need to be passed an "index_size" so they know how to
 * handle allocation.  For PTE pages, the allocation size will be
 * (2^index_size * sizeof(pointer)) and allocations are drawn from
 * the kmem_cache in PGT_CACHE(index_size).
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
#define PGT_CACHE(shift) pgtable_cache[shift]

#ifdef CONFIG_PPC_BOOK3S
#include <asm/book3s/pgalloc.h>
#else
#include <asm/nohash/pgalloc.h>
#endif

static inline pgtable_t pmd_pgtable(pmd_t pmd)
{
	return (pgtable_t)pmd_page_vaddr(pmd);
}

#endif /* _ASM_POWERPC_PGALLOC_H */

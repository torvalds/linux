/* SPDX-License-Identifier: GPL-2.0 */
#ifndef M68K_MCF_PGALLOC_H
#define M68K_MCF_PGALLOC_H

#include <asm/tlb.h>
#include <asm/tlbflush.h>

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	pagetable_dtor_free(virt_to_ptdesc(pte));
}

extern const char bad_pmd_string[];

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	struct ptdesc *ptdesc = pagetable_alloc((GFP_DMA | __GFP_ZERO) &
			~__GFP_HIGHMEM, 0);

	if (!ptdesc)
		return NULL;
	if (!pagetable_pte_ctor(mm, ptdesc)) {
		pagetable_free(ptdesc);
		return NULL;
	}

	return ptdesc_address(ptdesc);
}

extern inline pmd_t *pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

#define pmd_populate(mm, pmd, pte) (pmd_val(*pmd) = (unsigned long)(pte))

#define pmd_populate_kernel pmd_populate

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pgtable,
				  unsigned long address)
{
	struct ptdesc *ptdesc = virt_to_ptdesc(pgtable);

	pagetable_dtor(ptdesc);
	pagetable_free(ptdesc);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct ptdesc *ptdesc = pagetable_alloc(GFP_DMA | __GFP_ZERO, 0);
	pte_t *pte;

	if (!ptdesc)
		return NULL;
	if (!pagetable_pte_ctor(mm, ptdesc)) {
		pagetable_free(ptdesc);
		return NULL;
	}

	pte = ptdesc_address(ptdesc);
	return pte;
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pgtable)
{
	struct ptdesc *ptdesc = virt_to_ptdesc(pgtable);

	pagetable_dtor(ptdesc);
	pagetable_free(ptdesc);
}

/*
 * In our implementation, each pgd entry contains 1 pmd that is never allocated
 * or freed.  pgd_present is always 1, so this should never be called. -NL
 */
#define pmd_free(mm, pmd) BUG()

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	pagetable_dtor_free(virt_to_ptdesc(pgd));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *new_pgd;
	struct ptdesc *ptdesc = pagetable_alloc((GFP_DMA | __GFP_NOWARN) &
			~__GFP_HIGHMEM, 0);

	if (!ptdesc)
		return NULL;
	pagetable_pgd_ctor(ptdesc);
	new_pgd = ptdesc_address(ptdesc);

	memcpy(new_pgd, swapper_pg_dir, PTRS_PER_PGD * sizeof(pgd_t));
	memset(new_pgd, 0, PAGE_OFFSET >> PGDIR_SHIFT);
	return new_pgd;
}

#endif /* M68K_MCF_PGALLOC_H */

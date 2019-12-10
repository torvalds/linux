/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/fixmap.h>

#include <asm/cache.h>

#include <asm-generic/pgalloc.h>	/* for pte_{alloc,free}_one */

/* Allocate the top level pgd (page directory)
 *
 * Here (for 64 bit kernels) we implement a Hybrid L2/L3 scheme: we
 * allocate the first pmd adjacent to the pgd.  This means that we can
 * subtract a constant offset to get to it.  The pmd and pgd sizes are
 * arranged so that a single pmd covers 4GB (giving a full 64-bit
 * process access to 8TB) so our lookups are effectively L2 for the
 * first 4GB of the kernel (i.e. for all ILP32 processes and all the
 * kernel for machines with under 4GB of memory) */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_pages(GFP_KERNEL,
					       PGD_ALLOC_ORDER);
	pgd_t *actual_pgd = pgd;

	if (likely(pgd != NULL)) {
		memset(pgd, 0, PAGE_SIZE<<PGD_ALLOC_ORDER);
#if CONFIG_PGTABLE_LEVELS == 3
		actual_pgd += PTRS_PER_PGD;
		/* Populate first pmd with allocated memory.  We mark it
		 * with PxD_FLAG_ATTACHED as a signal to the system that this
		 * pmd entry may not be cleared. */
		set_pgd(actual_pgd, __pgd((PxD_FLAG_PRESENT |
				        PxD_FLAG_VALID |
					PxD_FLAG_ATTACHED)
			+ (__u32)(__pa((unsigned long)pgd) >> PxD_VALUE_SHIFT)));
		/* The first pmd entry also is marked with PxD_FLAG_ATTACHED as
		 * a signal that this pmd may not be freed */
		set_pgd(pgd, __pgd(PxD_FLAG_ATTACHED));
#endif
	}
	spin_lock_init(pgd_spinlock(actual_pgd));
	return actual_pgd;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
#if CONFIG_PGTABLE_LEVELS == 3
	pgd -= PTRS_PER_PGD;
#endif
	free_pages((unsigned long)pgd, PGD_ALLOC_ORDER);
}

#if CONFIG_PGTABLE_LEVELS == 3

/* Three Level Page Table Support for pmd's */

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud((PxD_FLAG_PRESENT | PxD_FLAG_VALID) +
			(__u32)(__pa((unsigned long)pmd) >> PxD_VALUE_SHIFT)));
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pmd_t *pmd = (pmd_t *)__get_free_pages(GFP_KERNEL, PMD_ORDER);
	if (pmd)
		memset(pmd, 0, PAGE_SIZE<<PMD_ORDER);
	return pmd;
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	if (pmd_flag(*pmd) & PxD_FLAG_ATTACHED) {
		/*
		 * This is the permanent pmd attached to the pgd;
		 * cannot free it.
		 * Increment the counter to compensate for the decrement
		 * done by generic mm code.
		 */
		mm_inc_nr_pmds(mm);
		return;
	}
	free_pages((unsigned long)pmd, PMD_ORDER);
}

#endif

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
#if CONFIG_PGTABLE_LEVELS == 3
	/* preserve the gateway marker if this is the beginning of
	 * the permanent pmd */
	if(pmd_flag(*pmd) & PxD_FLAG_ATTACHED)
		set_pmd(pmd, __pmd((PxD_FLAG_PRESENT |
				PxD_FLAG_VALID |
				PxD_FLAG_ATTACHED)
			+ (__u32)(__pa((unsigned long)pte) >> PxD_VALUE_SHIFT)));
	else
#endif
		set_pmd(pmd, __pmd((PxD_FLAG_PRESENT | PxD_FLAG_VALID)
			+ (__u32)(__pa((unsigned long)pte) >> PxD_VALUE_SHIFT)));
}

#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))
#define pmd_pgtable(pmd) pmd_page(pmd)

#endif

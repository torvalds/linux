/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_64_PGTABLE_4K_H
#define _ASM_POWERPC_NOHASH_64_PGTABLE_4K_H

#include <asm-generic/pgtable-nop4d.h>

/*
 * Entries per page directory level.  The PTE level must use a 64b record
 * for each page table entry.  The PMD and PGD level use a 32b record for
 * each entry by assuming that each entry is page aligned.
 */
#define PTE_INDEX_SIZE  9
#define PMD_INDEX_SIZE  7
#define PUD_INDEX_SIZE  9
#define PGD_INDEX_SIZE  9

#ifndef __ASSEMBLY__
#define PTE_TABLE_SIZE	(sizeof(pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#define PUD_TABLE_SIZE	(sizeof(pud_t) << PUD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)
#endif	/* __ASSEMBLY__ */

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PUD	(1 << PUD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PUD_SHIFT determines what a third-level page table entry can map */
#define PUD_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */
#define PGDIR_SHIFT	(PUD_SHIFT + PUD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0
/* Bits to mask out from a PUD to get to the PMD page */
#define PUD_MASKED_BITS		0
/* Bits to mask out from a P4D to get to the PUD page */
#define P4D_MASKED_BITS		0


/*
 * 4-level page tables related bits
 */

#define p4d_none(p4d)		(!p4d_val(p4d))
#define p4d_bad(p4d)		(p4d_val(p4d) == 0)
#define p4d_present(p4d)	(p4d_val(p4d) != 0)
#define p4d_page_vaddr(p4d)	(p4d_val(p4d) & ~P4D_MASKED_BITS)

#ifndef __ASSEMBLY__

static inline void p4d_clear(p4d_t *p4dp)
{
	*p4dp = __p4d(0);
}

static inline pte_t p4d_pte(p4d_t p4d)
{
	return __pte(p4d_val(p4d));
}

static inline p4d_t pte_p4d(pte_t pte)
{
	return __p4d(pte_val(pte));
}
extern struct page *p4d_page(p4d_t p4d);

#endif /* !__ASSEMBLY__ */

#define pud_offset(p4dp, addr)	\
  (((pud_t *) p4d_page_vaddr(*(p4dp))) + \
    (((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1)))

#define pud_ERROR(e) \
	pr_err("%s:%d: bad pud %08lx.\n", __FILE__, __LINE__, pud_val(e))

/*
 * On all 4K setups, remap_4k_pfn() equates to remap_pfn_range() */
#define remap_4k_pfn(vma, addr, pfn, prot)	\
	remap_pfn_range((vma), (addr), (pfn), PAGE_SIZE, (prot))

#endif /* _ _ASM_POWERPC_NOHASH_64_PGTABLE_4K_H */

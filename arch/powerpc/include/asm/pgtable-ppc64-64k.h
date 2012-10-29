#ifndef _ASM_POWERPC_PGTABLE_PPC64_64K_H
#define _ASM_POWERPC_PGTABLE_PPC64_64K_H

#include <asm-generic/pgtable-nopud.h>


#define PTE_INDEX_SIZE  12
#define PMD_INDEX_SIZE  12
#define PUD_INDEX_SIZE	0
#define PGD_INDEX_SIZE  6

#ifndef __ASSEMBLY__
#define PTE_TABLE_SIZE	(sizeof(real_pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)
#endif	/* __ASSEMBLY__ */

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* With 4k base page size, hugepage PTEs go at the PMD level */
#define MIN_HUGEPTE_SHIFT	PAGE_SHIFT

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0x1ff
/* Bits to mask out from a PGD/PUD to get to the PMD page */
#define PUD_MASKED_BITS		0x1ff

#endif /* _ASM_POWERPC_PGTABLE_PPC64_64K_H */

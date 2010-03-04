#ifndef __ASM_SH_PGTABLE_2LEVEL_H
#define __ASM_SH_PGTABLE_2LEVEL_H

#include <asm-generic/pgtable-nopmd.h>

/*
 * traditional two-level paging structure
 */
#define PAGETABLE_LEVELS	2

/* PTE bits */
#define PTE_MAGNITUDE		2	/* 32-bit PTEs */

#define PTE_SHIFT		PAGE_SHIFT
#define PTE_BITS		(PTE_SHIFT - PTE_MAGNITUDE)

/* PGD bits */
#define PGDIR_SHIFT		(PTE_SHIFT + PTE_BITS)

#define PTRS_PER_PGD		(PAGE_SIZE / (1 << PTE_MAGNITUDE))
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

#endif /* __ASM_SH_PGTABLE_2LEVEL_H */

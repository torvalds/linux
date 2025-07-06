/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_3LEVEL_DEFS_H
#define _ASM_X86_PGTABLE_3LEVEL_DEFS_H

#ifndef __ASSEMBLER__
#include <linux/types.h>

typedef u64	pteval_t;
typedef u64	pmdval_t;
typedef u64	pudval_t;
typedef u64	p4dval_t;
typedef u64	pgdval_t;
typedef u64	pgprotval_t;

typedef union {
	struct {
		unsigned long pte_low, pte_high;
	};
	pteval_t pte;
} pte_t;

typedef union {
	struct {
		unsigned long pmd_low, pmd_high;
	};
	pmdval_t pmd;
} pmd_t;
#endif	/* !__ASSEMBLER__ */

#define ARCH_PAGE_TABLE_SYNC_MASK	PGTBL_PMD_MODIFIED

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT	30
#define PTRS_PER_PGD	4

/*
 * PMD_SHIFT determines the size of the area a middle-level
 * page table can map
 */
#define PMD_SHIFT	21
#define PTRS_PER_PMD	512

/*
 * entries per page directory level
 */
#define PTRS_PER_PTE	512

#define MAX_POSSIBLE_PHYSMEM_BITS	36
#define PGD_KERNEL_START	(CONFIG_PAGE_OFFSET >> PGDIR_SHIFT)

#endif /* _ASM_X86_PGTABLE_3LEVEL_DEFS_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_2LEVEL_DEFS_H
#define _ASM_X86_PGTABLE_2LEVEL_DEFS_H

#ifndef __ASSEMBLER__
#include <linux/types.h>

typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pudval_t;
typedef unsigned long	p4dval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;

typedef union {
	pteval_t pte;
	pteval_t pte_low;
} pte_t;
#endif	/* !__ASSEMBLER__ */

#define ARCH_PAGE_TABLE_SYNC_MASK	PGTBL_PMD_MODIFIED

/*
 * Traditional i386 two-level paging structure:
 */

#define PGDIR_SHIFT	22
#define PTRS_PER_PGD	1024

/*
 * The i386 is two-level, so we don't really have any
 * PMD directory physically:
 */
#define PTRS_PER_PMD	1

#define PTRS_PER_PTE	1024

/* This covers all VMSPLIT_* and VMSPLIT_*_OPT variants */
#define PGD_KERNEL_START	(CONFIG_PAGE_OFFSET >> PGDIR_SHIFT)

#endif /* _ASM_X86_PGTABLE_2LEVEL_DEFS_H */

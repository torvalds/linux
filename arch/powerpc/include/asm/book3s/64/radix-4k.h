/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGTABLE_RADIX_4K_H
#define _ASM_POWERPC_PGTABLE_RADIX_4K_H

/*
 * For 4K page size supported index is 13/9/9/9
 */
#define RADIX_PTE_INDEX_SIZE  9  /* 2MB huge page */
#define RADIX_PMD_INDEX_SIZE  9  /* 1G huge page */
#define RADIX_PUD_INDEX_SIZE	 9
#define RADIX_PGD_INDEX_SIZE  13

#endif /* _ASM_POWERPC_PGTABLE_RADIX_4K_H */

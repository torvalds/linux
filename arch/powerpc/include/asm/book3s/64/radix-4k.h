/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGTABLE_RADIX_4K_H
#define _ASM_POWERPC_PGTABLE_RADIX_4K_H

/*
 * For 4K page size supported index is 13/9/9/9
 */
#define RADIX_PTE_INDEX_SIZE   9  // size: 8B <<  9 =  4KB, maps 2^9  x    4K =   2MB
#define RADIX_PMD_INDEX_SIZE   9  // size: 8B <<  9 =  4KB, maps 2^9  x   2MB =   1GB
#define RADIX_PUD_INDEX_SIZE   9  // size: 8B <<  9 =  4KB, maps 2^9  x   1GB = 512GB
#define RADIX_PGD_INDEX_SIZE  13  // size: 8B << 13 = 64KB, maps 2^13 x 512GB =   4PB

/*
 * One fragment per per page
 */
#define RADIX_PTE_FRAG_SIZE_SHIFT  (RADIX_PTE_INDEX_SIZE + 3)
#define RADIX_PTE_FRAG_NR	(PAGE_SIZE >> RADIX_PTE_FRAG_SIZE_SHIFT)

#define RADIX_PMD_FRAG_SIZE_SHIFT  (RADIX_PMD_INDEX_SIZE + 3)
#define RADIX_PMD_FRAG_NR	(PAGE_SIZE >> RADIX_PMD_FRAG_SIZE_SHIFT)

#endif /* _ASM_POWERPC_PGTABLE_RADIX_4K_H */

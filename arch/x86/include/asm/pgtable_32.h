/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PGTABLE_32_H
#define _ASM_X86_PGTABLE_32_H

#include <asm/pgtable_32_types.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * the i386, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the i386 page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/threads.h>
#include <asm/paravirt.h>

#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct mm_struct;
struct vm_area_struct;

extern pgd_t swapper_pg_dir[1024];
extern pgd_t initial_page_table[1024];
extern pmd_t initial_pg_pmd[];

void paging_init(void);
void sync_initial_page_table(void);

#ifdef CONFIG_X86_PAE
# include <asm/pgtable-3level.h>
#else
# include <asm/pgtable-2level.h>
#endif

/* Clear a kernel PTE and flush it from the TLB */
#define kpte_clear_flush(ptep, vaddr)		\
do {						\
	pte_clear(&init_mm, (vaddr), (ptep));	\
	flush_tlb_one_kernel((vaddr));		\
} while (0)

#endif /* !__ASSEMBLY__ */

/*
 * This is used to calculate the .brk reservation for initial pagetables.
 * Enough space is reserved to allocate pagetables sufficient to cover all
 * of LOWMEM_PAGES, which is an upper bound on the size of the direct map of
 * lowmem.
 *
 * With PAE paging (PTRS_PER_PMD > 1), we allocate PTRS_PER_PGD == 4 pages for
 * the PMD's in addition to the pages required for the last level pagetables.
 */
#if PTRS_PER_PMD > 1
#define PAGE_TABLE_SIZE(pages) (((pages) / PTRS_PER_PMD) + PTRS_PER_PGD)
#else
#define PAGE_TABLE_SIZE(pages) ((pages) / PTRS_PER_PGD)
#endif

/*
 * Number of possible pages in the lowmem region.
 *
 * We shift 2 by 31 instead of 1 by 32 to the left in order to avoid a
 * gas warning about overflowing shift count when gas has been compiled
 * with only a host target support using a 32-bit type for internal
 * representation.
 */
#define LOWMEM_PAGES ((((_ULL(2)<<31) - __PAGE_OFFSET) >> PAGE_SHIFT))

#endif /* _ASM_X86_PGTABLE_32_H */

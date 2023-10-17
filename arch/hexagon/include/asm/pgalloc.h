/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Page table support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <asm/mem-layout.h>
#include <asm/atomic.h>

#include <asm-generic/pgalloc.h>

extern unsigned long long kmap_generation;

/*
 * Page table creation interface
 */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);

	/*
	 * There may be better ways to do this, but to ensure
	 * that new address spaces always contain the kernel
	 * base mapping, and to ensure that the user area is
	 * initially marked invalid, initialize the new map
	 * map with a copy of the kernel's persistent map.
	 */

	memcpy(pgd, swapper_pg_dir, PTRS_PER_PGD*sizeof(pgd_t));
	mm->context.generation = kmap_generation;

	/* Physical version is what is passed to virtual machine on switch */
	mm->context.ptbase = __pa(pgd);

	return pgd;
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t pte)
{
	/*
	 * Conveniently, zero in 3 LSB means indirect 4K page table.
	 * Not so convenient when you're trying to vary the page size.
	 */
	set_pmd(pmd, __pmd(((unsigned long)page_to_pfn(pte) << PAGE_SHIFT) |
		HEXAGON_L1_PTE_SIZE));
}

/*
 * Other architectures seem to have ways of making all processes
 * share the same pmd's for their kernel mappings, but the v0.3
 * Hexagon VM spec has a "monolithic" L1 table for user and kernel
 * segments.  We track "generations" of the kernel map to minimize
 * overhead, and update the "slave" copies of the kernel mappings
 * as part of switch_mm.  However, we still need to update the
 * kernel map of the active thread who's calling pmd_populate_kernel...
 */
static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	extern spinlock_t kmap_gen_lock;
	pmd_t *ppmd;
	int pmdindex;

	spin_lock(&kmap_gen_lock);
	kmap_generation++;
	mm->context.generation = kmap_generation;
	current->active_mm->context.generation = kmap_generation;
	spin_unlock(&kmap_gen_lock);

	set_pmd(pmd, __pmd(((unsigned long)__pa(pte)) | HEXAGON_L1_PTE_SIZE));

	/*
	 * Now the "slave" copy of the current thread.
	 * This is pointer arithmetic, not byte addresses!
	 */
	pmdindex = (pgd_t *)pmd - mm->pgd;
	ppmd = (pmd_t *)current->active_mm->pgd + pmdindex;
	set_pmd(ppmd, __pmd(((unsigned long)__pa(pte)) | HEXAGON_L1_PTE_SIZE));
	if (pmdindex > max_kernel_seg)
		max_kernel_seg = pmdindex;
}

#define __pte_free_tlb(tlb, pte, addr)				\
do {								\
	pagetable_pte_dtor((page_ptdesc(pte)));			\
	tlb_remove_page_ptdesc((tlb), (page_ptdesc(pte)));	\
} while (0)

#endif

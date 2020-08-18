/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_PGALLOC_H
#define _ASM_IA64_PGALLOC_H

/*
 * This file contains the functions and defines necessary to allocate
 * page tables.
 *
 * This hopefully works with any (fixed) ia-64 page-size, as defined
 * in <asm/page.h> (currently 8192).
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000, Goutham Rao <goutham.rao@intel.com>
 */


#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/threads.h>

#include <asm-generic/pgalloc.h>

#include <asm/mmu_context.h>

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
}

#if CONFIG_PGTABLE_LEVELS == 4
static inline void
p4d_populate(struct mm_struct *mm, p4d_t * p4d_entry, pud_t * pud)
{
	p4d_val(*p4d_entry) = __pa(pud);
}

#define __pud_free_tlb(tlb, pud, address)	pud_free((tlb)->mm, pud)
#endif /* CONFIG_PGTABLE_LEVELS == 4 */

static inline void
pud_populate(struct mm_struct *mm, pud_t * pud_entry, pmd_t * pmd)
{
	pud_val(*pud_entry) = __pa(pmd);
}

#define __pmd_free_tlb(tlb, pmd, address)	pmd_free((tlb)->mm, pmd)

static inline void
pmd_populate(struct mm_struct *mm, pmd_t * pmd_entry, pgtable_t pte)
{
	pmd_val(*pmd_entry) = page_to_phys(pte);
}
#define pmd_pgtable(pmd) pmd_page(pmd)

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t * pmd_entry, pte_t * pte)
{
	pmd_val(*pmd_entry) = __pa(pte);
}

#define __pte_free_tlb(tlb, pte, address)	pte_free((tlb)->mm, pte)

#endif				/* _ASM_IA64_PGALLOC_H */

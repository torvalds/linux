/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2001, 2003 by Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */

#ifndef _ASM_NIOS2_PGALLOC_H
#define _ASM_NIOS2_PGALLOC_H

#include <linux/mm.h>

#include <asm-generic/pgalloc.h>

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
	pte_t *pte)
{
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
	pgtable_t pte)
{
	set_pmd(pmd, __pmd((unsigned long)page_address(pte)));
}

extern pgd_t *pgd_alloc(struct mm_struct *mm);

#define __pte_free_tlb(tlb, pte, addr)	\
	tlb_remove_ptdesc((tlb), page_ptdesc(pte))

#endif /* _ASM_NIOS2_PGALLOC_H */

/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PGALLOC_H
#define _ASM_MICROBLAZE_PGALLOC_H

#ifdef CONFIG_MMU

#include <linux/kernel.h>	/* For min/max macros */
#include <linux/highmem.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/pgtable.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#include <asm-generic/pgalloc.h>

extern void __bad_pte(pmd_t *pmd);

static inline pgd_t *get_pgd(void)
{
	return (pgd_t *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, 0);
}

static inline void free_pgd(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#define pgd_free(mm, pgd)	free_pgd(pgd)
#define pgd_alloc(mm)		get_pgd()

#define pmd_pgtable(pmd)	pmd_page(pmd)

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm);

#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, (pte))

#define pmd_populate(mm, pmd, pte) \
			(pmd_val(*(pmd)) = (unsigned long)page_address(pte))

#define pmd_populate_kernel(mm, pmd, pte) \
		(pmd_val(*(pmd)) = (unsigned long) (pte))

#endif /* CONFIG_MMU */

#endif /* _ASM_MICROBLAZE_PGALLOC_H */

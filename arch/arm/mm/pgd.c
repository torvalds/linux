// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/pgd.c
 *
 *  Copyright (C) 1998-2005 Russell King
 */
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include <asm/cp15.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/tlbflush.h>

#include "mm.h"

#ifdef CONFIG_ARM_LPAE
#define _pgd_alloc(mm)		kmalloc_array(PTRS_PER_PGD, sizeof(pgd_t), GFP_KERNEL | __GFP_ZERO)
#define _pgd_free(mm, pgd)	kfree(pgd)
#else
#define _pgd_alloc(mm)		__pgd_alloc(mm, 2)
#define _pgd_free(mm, pgd)	__pgd_free(mm, pgd)
#endif

/*
 * need to get a 16k page for level 1
 */
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *new_pgd, *init_pgd;
	p4d_t *new_p4d, *init_p4d;
	pud_t *new_pud, *init_pud;
	pmd_t *new_pmd, *init_pmd;
	pte_t *new_pte, *init_pte;

	new_pgd = _pgd_alloc(mm);
	if (!new_pgd)
		goto no_pgd;

	/*
	 * Copy over the kernel and IO PGD entries
	 */
	init_pgd = pgd_offset_k(0);
	memcpy(new_pgd + USER_PTRS_PER_PGD, init_pgd + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));

	clean_dcache_area(new_pgd, PTRS_PER_PGD * sizeof(pgd_t));

#ifdef CONFIG_ARM_LPAE
	/*
	 * Allocate PMD table for modules and pkmap mappings.
	 */
	new_p4d = p4d_alloc(mm, new_pgd + pgd_index(MODULES_VADDR),
			    MODULES_VADDR);
	if (!new_p4d)
		goto no_p4d;

	new_pud = pud_alloc(mm, new_p4d, MODULES_VADDR);
	if (!new_pud)
		goto no_pud;

	new_pmd = pmd_alloc(mm, new_pud, 0);
	if (!new_pmd)
		goto no_pmd;
#ifdef CONFIG_KASAN
	/*
	 * Copy PMD table for KASAN shadow mappings.
	 */
	init_pgd = pgd_offset_k(TASK_SIZE);
	init_p4d = p4d_offset(init_pgd, TASK_SIZE);
	init_pud = pud_offset(init_p4d, TASK_SIZE);
	init_pmd = pmd_offset(init_pud, TASK_SIZE);
	new_pmd = pmd_offset(new_pud, TASK_SIZE);
	memcpy(new_pmd, init_pmd,
	       (pmd_index(MODULES_VADDR) - pmd_index(TASK_SIZE))
	       * sizeof(pmd_t));
	clean_dcache_area(new_pmd, PTRS_PER_PMD * sizeof(pmd_t));
#endif /* CONFIG_KASAN */
#endif /* CONFIG_LPAE */

	if (!vectors_high()) {
		/*
		 * On ARM, first page must always be allocated since it
		 * contains the machine vectors. The vectors are always high
		 * with LPAE.
		 */
		new_p4d = p4d_alloc(mm, new_pgd, 0);
		if (!new_p4d)
			goto no_p4d;

		new_pud = pud_alloc(mm, new_p4d, 0);
		if (!new_pud)
			goto no_pud;

		new_pmd = pmd_alloc(mm, new_pud, 0);
		if (!new_pmd)
			goto no_pmd;

		new_pte = pte_alloc_map(mm, new_pmd, 0);
		if (!new_pte)
			goto no_pte;

#ifndef CONFIG_ARM_LPAE
		/*
		 * Modify the PTE pointer to have the correct domain.  This
		 * needs to be the vectors domain to avoid the low vectors
		 * being unmapped.
		 */
		pmd_val(*new_pmd) &= ~PMD_DOMAIN_MASK;
		pmd_val(*new_pmd) |= PMD_DOMAIN(DOMAIN_VECTORS);
#endif

		init_p4d = p4d_offset(init_pgd, 0);
		init_pud = pud_offset(init_p4d, 0);
		init_pmd = pmd_offset(init_pud, 0);
		init_pte = pte_offset_map(init_pmd, 0);
		set_pte_ext(new_pte + 0, init_pte[0], 0);
		set_pte_ext(new_pte + 1, init_pte[1], 0);
		pte_unmap(init_pte);
		pte_unmap(new_pte);
	}

	return new_pgd;

no_pte:
	pmd_free(mm, new_pmd);
	mm_dec_nr_pmds(mm);
no_pmd:
	pud_free(mm, new_pud);
no_pud:
	p4d_free(mm, new_p4d);
no_p4d:
	_pgd_free(mm, new_pgd);
no_pgd:
	return NULL;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd_base)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pgtable_t pte;

	if (!pgd_base)
		return;

	pgd = pgd_base + pgd_index(0);
	if (pgd_none_or_clear_bad(pgd))
		goto no_pgd;

	p4d = p4d_offset(pgd, 0);
	if (p4d_none_or_clear_bad(p4d))
		goto no_p4d;

	pud = pud_offset(p4d, 0);
	if (pud_none_or_clear_bad(pud))
		goto no_pud;

	pmd = pmd_offset(pud, 0);
	if (pmd_none_or_clear_bad(pmd))
		goto no_pmd;

	pte = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	pte_free(mm, pte);
	mm_dec_nr_ptes(mm);
no_pmd:
	pud_clear(pud);
	pmd_free(mm, pmd);
	mm_dec_nr_pmds(mm);
no_pud:
	p4d_clear(p4d);
	pud_free(mm, pud);
no_p4d:
	pgd_clear(pgd);
	p4d_free(mm, p4d);
no_pgd:
#ifdef CONFIG_ARM_LPAE
	/*
	 * Free modules/pkmap or identity pmd tables.
	 */
	for (pgd = pgd_base; pgd < pgd_base + PTRS_PER_PGD; pgd++) {
		if (pgd_none_or_clear_bad(pgd))
			continue;
		if (pgd_val(*pgd) & L_PGD_SWAPPER)
			continue;
		p4d = p4d_offset(pgd, 0);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		pud = pud_offset(p4d, 0);
		if (pud_none_or_clear_bad(pud))
			continue;
		pmd = pmd_offset(pud, 0);
		pud_clear(pud);
		pmd_free(mm, pmd);
		mm_dec_nr_pmds(mm);
		p4d_clear(p4d);
		pud_free(mm, pud);
		mm_dec_nr_puds(mm);
		pgd_clear(pgd);
		p4d_free(mm, p4d);
	}
#endif
	_pgd_free(mm, pgd_base);
}

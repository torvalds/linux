// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/init_task.h>
#include <asm/pgalloc.h>

#define FIRST_KERNEL_PGD_NR	(USER_PTRS_PER_PGD)

/*
 * need to get a page for level 1
 */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *new_pgd, *init_pgd;
	int i;

	new_pgd = (pgd_t *) __get_free_pages(GFP_KERNEL, 0);
	if (!new_pgd)
		return NULL;
	for (i = 0; i < PTRS_PER_PGD; i++) {
		(*new_pgd) = 1;
		new_pgd++;
	}
	new_pgd -= PTRS_PER_PGD;

	init_pgd = pgd_offset_k(0);

	memcpy(new_pgd + FIRST_KERNEL_PGD_NR, init_pgd + FIRST_KERNEL_PGD_NR,
	       (PTRS_PER_PGD - FIRST_KERNEL_PGD_NR) * sizeof(pgd_t));

	cpu_dcache_wb_range((unsigned long)new_pgd,
			    (unsigned long)new_pgd +
			    PTRS_PER_PGD * sizeof(pgd_t));
	inc_zone_page_state(virt_to_page((unsigned long *)new_pgd),
			    NR_PAGETABLE);

	return new_pgd;
}

void pgd_free(struct mm_struct *mm, pgd_t * pgd)
{
	pmd_t *pmd;
	struct page *pte;

	if (!pgd)
		return;

	pmd = (pmd_t *) pgd;
	if (pmd_none(*pmd))
		goto free;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto free;
	}

	pte = pmd_page(*pmd);
	pmd_clear(pmd);
	dec_zone_page_state(virt_to_page((unsigned long *)pgd), NR_PAGETABLE);
	pte_free(mm, pte);
	mm_dec_nr_ptes(mm);
	pmd_free(mm, pmd);
free:
	free_pages((unsigned long)pgd, 0);
}

/*
 * In order to soft-boot, we need to insert a 1:1 mapping in place of
 * the user-mode pages.  This will then ensure that we have predictable
 * results when turning the mmu off
 */
void setup_mm_for_reboot(char mode)
{
	unsigned long pmdval;
	pgd_t *pgd;
	pmd_t *pmd;
	int i;

	if (current->mm && current->mm->pgd)
		pgd = current->mm->pgd;
	else
		pgd = init_mm.pgd;

	for (i = 0; i < USER_PTRS_PER_PGD; i++) {
		pmdval = (i << PGDIR_SHIFT);
		pmd = pmd_offset(pgd + i, i << PGDIR_SHIFT);
		set_pmd(pmd, __pmd(pmdval));
	}
}

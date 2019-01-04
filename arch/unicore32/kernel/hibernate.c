/*
 *  linux/arch/unicore32/kernel/hibernate.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gfp.h>
#include <linux/suspend.h>
#include <linux/memblock.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/suspend.h>

#include "mach/pm.h"

/* Pointer to the temporary resume page tables */
pgd_t *resume_pg_dir;

struct swsusp_arch_regs swsusp_arch_regs_cpu0;

/*
 * Create a middle page table on a resume-safe page and put a pointer to it in
 * the given global directory entry.  This only returns the gd entry
 * in non-PAE compilation mode, since the middle layer is folded.
 */
static pmd_t *resume_one_md_table_init(pgd_t *pgd)
{
	pud_t *pud;
	pmd_t *pmd_table;

	pud = pud_offset(pgd, 0);
	pmd_table = pmd_offset(pud, 0);

	return pmd_table;
}

/*
 * Create a page table on a resume-safe page and place a pointer to it in
 * a middle page directory entry.
 */
static pte_t *resume_one_page_table_init(pmd_t *pmd)
{
	if (pmd_none(*pmd)) {
		pte_t *page_table = (pte_t *)get_safe_page(GFP_ATOMIC);
		if (!page_table)
			return NULL;

		set_pmd(pmd, __pmd(__pa(page_table) | _PAGE_KERNEL_TABLE));

		BUG_ON(page_table != pte_offset_kernel(pmd, 0));

		return page_table;
	}

	return pte_offset_kernel(pmd, 0);
}

/*
 * This maps the physical memory to kernel virtual address space, a total
 * of max_low_pfn pages, by creating page tables starting from address
 * PAGE_OFFSET.  The page tables are allocated out of resume-safe pages.
 */
static int resume_physical_mapping_init(pgd_t *pgd_base)
{
	unsigned long pfn;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int pgd_idx, pmd_idx;

	pgd_idx = pgd_index(PAGE_OFFSET);
	pgd = pgd_base + pgd_idx;
	pfn = 0;

	for (; pgd_idx < PTRS_PER_PGD; pgd++, pgd_idx++) {
		pmd = resume_one_md_table_init(pgd);
		if (!pmd)
			return -ENOMEM;

		if (pfn >= max_low_pfn)
			continue;

		for (pmd_idx = 0; pmd_idx < PTRS_PER_PMD; pmd++, pmd_idx++) {
			pte_t *max_pte;

			if (pfn >= max_low_pfn)
				break;

			/* Map with normal page tables.
			 * NOTE: We can mark everything as executable here
			 */
			pte = resume_one_page_table_init(pmd);
			if (!pte)
				return -ENOMEM;

			max_pte = pte + PTRS_PER_PTE;
			for (; pte < max_pte; pte++, pfn++) {
				if (pfn >= max_low_pfn)
					break;

				set_pte(pte, pfn_pte(pfn, PAGE_KERNEL_EXEC));
			}
		}
	}

	return 0;
}

static inline void resume_init_first_level_page_table(pgd_t *pg_dir)
{
}

int swsusp_arch_resume(void)
{
	int error;

	resume_pg_dir = (pgd_t *)get_safe_page(GFP_ATOMIC);
	if (!resume_pg_dir)
		return -ENOMEM;

	resume_init_first_level_page_table(resume_pg_dir);
	error = resume_physical_mapping_init(resume_pg_dir);
	if (error)
		return error;

	/* We have got enough memory and from now on we cannot recover */
	restore_image(resume_pg_dir, restore_pblist);
	return 0;
}

/*
 *	pfn_is_nosave - check if given pfn is in the 'nosave' section
 */

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long end_pfn = PAGE_ALIGN(__pa(&__nosave_end)) >> PAGE_SHIFT;

	return (pfn >= begin_pfn) && (pfn < end_pfn);
}

void save_processor_state(void)
{
}

void restore_processor_state(void)
{
	local_flush_tlb_all();
}

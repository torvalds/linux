/*
 * Hibernation support specific for i386 - temporary page tables
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 */

#include <linux/gfp.h>
#include <linux/suspend.h>
#include <linux/bootmem.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmzone.h>
#include <asm/sections.h>

/* Defined in hibernate_asm_32.S */
extern int restore_image(void);

/* Pointer to the temporary resume page tables */
pgd_t *resume_pg_dir;

/* The following three functions are based on the analogous code in
 * arch/x86/mm/init_32.c
 */

/*
 * Create a middle page table on a resume-safe page and put a pointer to it in
 * the given global directory entry.  This only returns the gd entry
 * in non-PAE compilation mode, since the middle layer is folded.
 */
static pmd_t *resume_one_md_table_init(pgd_t *pgd)
{
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd_table;

#ifdef CONFIG_X86_PAE
	pmd_table = (pmd_t *)get_safe_page(GFP_ATOMIC);
	if (!pmd_table)
		return NULL;

	set_pgd(pgd, __pgd(__pa(pmd_table) | _PAGE_PRESENT));
	p4d = p4d_offset(pgd, 0);
	pud = pud_offset(p4d, 0);

	BUG_ON(pmd_table != pmd_offset(pud, 0));
#else
	p4d = p4d_offset(pgd, 0);
	pud = pud_offset(p4d, 0);
	pmd_table = pmd_offset(pud, 0);
#endif

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

		set_pmd(pmd, __pmd(__pa(page_table) | _PAGE_TABLE));

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
			if (pfn >= max_low_pfn)
				break;

			/* Map with big pages if possible, otherwise create
			 * normal page tables.
			 * NOTE: We can mark everything as executable here
			 */
			if (boot_cpu_has(X86_FEATURE_PSE)) {
				set_pmd(pmd, pfn_pmd(pfn, PAGE_KERNEL_LARGE_EXEC));
				pfn += PTRS_PER_PTE;
			} else {
				pte_t *max_pte;

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
	}

	return 0;
}

static inline void resume_init_first_level_page_table(pgd_t *pg_dir)
{
#ifdef CONFIG_X86_PAE
	int i;

	/* Init entries of the first-level page table to the zero page */
	for (i = 0; i < PTRS_PER_PGD; i++)
		set_pgd(pg_dir + i,
			__pgd(__pa(empty_zero_page) | _PAGE_PRESENT));
#endif
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
	restore_image();
	return 0;
}

/*
 *	pfn_is_nosave - check if given pfn is in the 'nosave' section
 */

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa_symbol(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa_symbol(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

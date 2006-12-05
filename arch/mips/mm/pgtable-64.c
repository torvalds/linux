/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000 by Silicon Graphics
 * Copyright (C) 2003 by Ralf Baechle
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>

void pgd_init(unsigned long page)
{
	unsigned long *p, *end;

 	p = (unsigned long *) page;
	end = p + PTRS_PER_PGD;

	while (p < end) {
		p[0] = (unsigned long) invalid_pmd_table;
		p[1] = (unsigned long) invalid_pmd_table;
		p[2] = (unsigned long) invalid_pmd_table;
		p[3] = (unsigned long) invalid_pmd_table;
		p[4] = (unsigned long) invalid_pmd_table;
		p[5] = (unsigned long) invalid_pmd_table;
		p[6] = (unsigned long) invalid_pmd_table;
		p[7] = (unsigned long) invalid_pmd_table;
		p += 8;
	}
}

void pmd_init(unsigned long addr, unsigned long pagetable)
{
	unsigned long *p, *end;

 	p = (unsigned long *) addr;
	end = p + PTRS_PER_PMD;

	while (p < end) {
		p[0] = (unsigned long)pagetable;
		p[1] = (unsigned long)pagetable;
		p[2] = (unsigned long)pagetable;
		p[3] = (unsigned long)pagetable;
		p[4] = (unsigned long)pagetable;
		p[5] = (unsigned long)pagetable;
		p[6] = (unsigned long)pagetable;
		p[7] = (unsigned long)pagetable;
		p += 8;
	}
}

void __init pagetable_init(void)
{
	unsigned long vaddr;
	pgd_t *pgd_base;

	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
#ifdef MODULE_START
	pgd_init((unsigned long)module_pg_dir);
#endif
	pmd_init((unsigned long)invalid_pmd_table, (unsigned long)invalid_pte_table);

	pgd_base = swapper_pg_dir;
	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, 0, pgd_base);
}

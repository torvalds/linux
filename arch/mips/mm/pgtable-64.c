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
#include <asm/pgalloc.h>

void pgd_init(unsigned long page)
{
	unsigned long *p, *end;
	unsigned long entry;

#ifdef __PAGETABLE_PMD_FOLDED
	entry = (unsigned long)invalid_pte_table;
#else
	entry = (unsigned long)invalid_pmd_table;
#endif

 	p = (unsigned long *) page;
	end = p + PTRS_PER_PGD;

	while (p < end) {
		p[0] = entry;
		p[1] = entry;
		p[2] = entry;
		p[3] = entry;
		p[4] = entry;
		p[5] = entry;
		p[6] = entry;
		p[7] = entry;
		p += 8;
	}
}

#ifndef __PAGETABLE_PMD_FOLDED
void pmd_init(unsigned long addr, unsigned long pagetable)
{
	unsigned long *p, *end;

 	p = (unsigned long *) addr;
	end = p + PTRS_PER_PMD;

	while (p < end) {
		p[0] = pagetable;
		p[1] = pagetable;
		p[2] = pagetable;
		p[3] = pagetable;
		p[4] = pagetable;
		p[5] = pagetable;
		p[6] = pagetable;
		p[7] = pagetable;
		p += 8;
	}
}
#endif

void __init pagetable_init(void)
{
	unsigned long vaddr;
	pgd_t *pgd_base;

	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
#ifndef __PAGETABLE_PMD_FOLDED
	pmd_init((unsigned long)invalid_pmd_table, (unsigned long)invalid_pte_table);
#endif
	pgd_base = swapper_pg_dir;
	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, vaddr + FIXADDR_SIZE, pgd_base);
}

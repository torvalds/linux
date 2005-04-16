/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 by Ralf Baechle
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <asm/pgtable.h>

void pgd_init(unsigned long page)
{
	unsigned long *p = (unsigned long *) page;
	int i;

	for (i = 0; i < USER_PTRS_PER_PGD; i+=8) {
		p[i + 0] = (unsigned long) invalid_pte_table;
		p[i + 1] = (unsigned long) invalid_pte_table;
		p[i + 2] = (unsigned long) invalid_pte_table;
		p[i + 3] = (unsigned long) invalid_pte_table;
		p[i + 4] = (unsigned long) invalid_pte_table;
		p[i + 5] = (unsigned long) invalid_pte_table;
		p[i + 6] = (unsigned long) invalid_pte_table;
		p[i + 7] = (unsigned long) invalid_pte_table;
	}
}

#ifdef CONFIG_HIGHMEM
static void __init fixrange_init (unsigned long start, unsigned long end,
	pgd_t *pgd_base)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int i, j;
	unsigned long vaddr;

	vaddr = start;
	i = __pgd_offset(vaddr);
	j = __pmd_offset(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr != end); pgd++, i++) {
		pmd = (pmd_t *)pgd;
		for (; (j < PTRS_PER_PMD) && (vaddr != end); pmd++, j++) {
			if (pmd_none(*pmd)) {
				pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
				set_pmd(pmd, __pmd((unsigned long)pte));
				if (pte != pte_offset_kernel(pmd, 0))
					BUG();
			}
			vaddr += PMD_SIZE;
		}
		j = 0;
	}
}
#endif

void __init pagetable_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long vaddr;
	pgd_t *pgd, *pgd_base;
	pmd_t *pmd;
	pte_t *pte;
#endif

	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
	pgd_init((unsigned long)swapper_pg_dir
		 + sizeof(pgd_t) * USER_PTRS_PER_PGD);

#ifdef CONFIG_HIGHMEM
	pgd_base = swapper_pg_dir;

	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, 0, pgd_base);

	/*
	 * Permanent kmaps:
	 */
	vaddr = PKMAP_BASE;
	fixrange_init(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP, pgd_base);

	pgd = swapper_pg_dir + __pgd_offset(vaddr);
	pmd = pmd_offset(pgd, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	pkmap_page_table = pte;
#endif
}

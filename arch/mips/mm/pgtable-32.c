/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 by Ralf Baechle
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/highmem.h>
#include <asm/fixmap.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

void pgd_init(void *addr)
{
	unsigned long *p = (unsigned long *)addr;
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

#if defined(CONFIG_TRANSPARENT_HUGEPAGE)
void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}
#endif /* defined(CONFIG_TRANSPARENT_HUGEPAGE) */

void __init pagetable_init(void)
{
	unsigned long vaddr;
	pgd_t *pgd_base;
#ifdef CONFIG_HIGHMEM
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
#endif

	/* Initialize the entire pgd.  */
	pgd_init(swapper_pg_dir);
	pgd_init(&swapper_pg_dir[USER_PTRS_PER_PGD]);

	pgd_base = swapper_pg_dir;

	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1);
	fixrange_init(vaddr & PMD_MASK, vaddr + FIXADDR_SIZE, pgd_base);

#ifdef CONFIG_HIGHMEM
	/*
	 * Permanent kmaps:
	 */
	vaddr = PKMAP_BASE;
	fixrange_init(vaddr & PMD_MASK, vaddr + PAGE_SIZE*LAST_PKMAP, pgd_base);

	pgd = swapper_pg_dir + pgd_index(vaddr);
	p4d = p4d_offset(pgd, vaddr);
	pud = pud_offset(p4d, vaddr);
	pmd = pmd_offset(pud, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	pkmap_page_table = pte;
#endif
}

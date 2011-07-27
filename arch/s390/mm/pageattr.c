/*
 * Copyright IBM Corp. 2011
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/pgtable.h>

static void change_page_attr(unsigned long addr, int numpages,
			     pte_t (*set) (pte_t))
{
	pte_t *ptep, pte;
	pmd_t *pmdp;
	pud_t *pudp;
	pgd_t *pgdp;
	int i;

	for (i = 0; i < numpages; i++) {
		pgdp = pgd_offset(&init_mm, addr);
		pudp = pud_offset(pgdp, addr);
		pmdp = pmd_offset(pudp, addr);
		if (pmd_huge(*pmdp)) {
			WARN_ON_ONCE(1);
			continue;
		}
		ptep = pte_offset_kernel(pmdp, addr);

		pte = *ptep;
		pte = set(pte);
		__ptep_ipte(addr, ptep);
		*ptep = pte;
		addr += PAGE_SIZE;
	}
}

int set_memory_ro(unsigned long addr, int numpages)
{
	change_page_attr(addr, numpages, pte_wrprotect);
	return 0;
}
EXPORT_SYMBOL_GPL(set_memory_ro);

int set_memory_rw(unsigned long addr, int numpages)
{
	change_page_attr(addr, numpages, pte_mkwrite);
	return 0;
}
EXPORT_SYMBOL_GPL(set_memory_rw);

/* not possible */
int set_memory_nx(unsigned long addr, int numpages)
{
	return 0;
}
EXPORT_SYMBOL_GPL(set_memory_nx);

int set_memory_x(unsigned long addr, int numpages)
{
	return 0;
}

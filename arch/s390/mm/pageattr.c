/*
 * Copyright IBM Corp. 2011
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>

static pte_t *walk_page_table(unsigned long addr)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = pgd_offset_k(addr);
	if (pgd_none(*pgdp))
		return NULL;
	pudp = pud_offset(pgdp, addr);
	if (pud_none(*pudp))
		return NULL;
	pmdp = pmd_offset(pudp, addr);
	if (pmd_none(*pmdp) || pmd_large(*pmdp))
		return NULL;
	ptep = pte_offset_kernel(pmdp, addr);
	if (pte_none(*ptep))
		return NULL;
	return ptep;
}

static void change_page_attr(unsigned long addr, int numpages,
			     pte_t (*set) (pte_t))
{
	pte_t *ptep, pte;
	int i;

	for (i = 0; i < numpages; i++) {
		ptep = walk_page_table(addr);
		if (WARN_ON_ONCE(!ptep))
			break;
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

int set_memory_rw(unsigned long addr, int numpages)
{
	change_page_attr(addr, numpages, pte_mkwrite);
	return 0;
}

/* not possible */
int set_memory_nx(unsigned long addr, int numpages)
{
	return 0;
}

int set_memory_x(unsigned long addr, int numpages)
{
	return 0;
}

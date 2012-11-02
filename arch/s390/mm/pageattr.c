/*
 * Copyright IBM Corp. 2011
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <asm/page.h>

void storage_key_init_range(unsigned long start, unsigned long end)
{
	unsigned long boundary, function, size;

	while (start < end) {
		if (MACHINE_HAS_EDAT2) {
			/* set storage keys for a 2GB frame */
			function = 0x22000 | PAGE_DEFAULT_KEY;
			size = 1UL << 31;
			boundary = (start + size) & ~(size - 1);
			if (boundary <= end) {
				do {
					start = pfmf(function, start);
				} while (start < boundary);
				continue;
			}
		}
		if (MACHINE_HAS_EDAT1) {
			/* set storage keys for a 1MB frame */
			function = 0x21000 | PAGE_DEFAULT_KEY;
			size = 1UL << 20;
			boundary = (start + size) & ~(size - 1);
			if (boundary <= end) {
				do {
					start = pfmf(function, start);
				} while (start < boundary);
				continue;
			}
		}
		page_set_storage_key(start, PAGE_DEFAULT_KEY, 0);
		start += PAGE_SIZE;
	}
}

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
	if (pud_none(*pudp) || pud_large(*pudp))
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

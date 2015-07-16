/*
 * Copyright IBM Corp. 2011
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/facility.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#if PAGE_DEFAULT_KEY
static inline unsigned long sske_frame(unsigned long addr, unsigned char skey)
{
	asm volatile(".insn rrf,0xb22b0000,%[skey],%[addr],9,0"
		     : [addr] "+a" (addr) : [skey] "d" (skey));
	return addr;
}

void __storage_key_init_range(unsigned long start, unsigned long end)
{
	unsigned long boundary, size;

	while (start < end) {
		if (MACHINE_HAS_EDAT1) {
			/* set storage keys for a 1MB frame */
			size = 1UL << 20;
			boundary = (start + size) & ~(size - 1);
			if (boundary <= end) {
				do {
					start = sske_frame(start, PAGE_DEFAULT_KEY);
				} while (start < boundary);
				continue;
			}
		}
		page_set_storage_key(start, PAGE_DEFAULT_KEY, 0);
		start += PAGE_SIZE;
	}
}
#endif

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

#ifdef CONFIG_DEBUG_PAGEALLOC

static void ipte_range(pte_t *pte, unsigned long address, int nr)
{
	int i;

	if (test_facility(13)) {
		__ptep_ipte_range(address, nr - 1, pte);
		return;
	}
	for (i = 0; i < nr; i++) {
		__ptep_ipte(address, pte);
		address += PAGE_SIZE;
		pte++;
	}
}

void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	unsigned long address;
	int nr, i, j;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	for (i = 0; i < numpages;) {
		address = page_to_phys(page + i);
		pgd = pgd_offset_k(address);
		pud = pud_offset(pgd, address);
		pmd = pmd_offset(pud, address);
		pte = pte_offset_kernel(pmd, address);
		nr = (unsigned long)pte >> ilog2(sizeof(long));
		nr = PTRS_PER_PTE - (nr & (PTRS_PER_PTE - 1));
		nr = min(numpages - i, nr);
		if (enable) {
			for (j = 0; j < nr; j++) {
				pte_val(*pte) = __pa(address);
				address += PAGE_SIZE;
				pte++;
			}
		} else {
			ipte_range(pte, address, nr);
		}
		i += nr;
	}
}

#ifdef CONFIG_HIBERNATION
bool kernel_page_present(struct page *page)
{
	unsigned long addr;
	int cc;

	addr = page_to_phys(page);
	asm volatile(
		"	lra	%1,0(%1)\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (cc), "+a" (addr) : : "cc");
	return cc == 0;
}
#endif /* CONFIG_HIBERNATION */

#endif /* CONFIG_DEBUG_PAGEALLOC */

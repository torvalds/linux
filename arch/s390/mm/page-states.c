// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2008
 *
 * Guest page hinting for unused pages.
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <asm/asm-extable.h>
#include <asm/facility.h>
#include <asm/page-states.h>

int __bootdata_preserved(cmma_flag);

static void mark_kernel_pmd(pud_t *pud, unsigned long addr, unsigned long end)
{
	unsigned long next;
	struct page *page;
	pmd_t *pmd;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd) || pmd_large(*pmd))
			continue;
		page = phys_to_page(pmd_val(*pmd));
		set_bit(PG_arch_1, &page->flags);
	} while (pmd++, addr = next, addr != end);
}

static void mark_kernel_pud(p4d_t *p4d, unsigned long addr, unsigned long end)
{
	unsigned long next;
	struct page *page;
	pud_t *pud;
	int i;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none(*pud) || pud_large(*pud))
			continue;
		if (!pud_folded(*pud)) {
			page = phys_to_page(pud_val(*pud));
			for (i = 0; i < 4; i++)
				set_bit(PG_arch_1, &page[i].flags);
		}
		mark_kernel_pmd(pud, addr, next);
	} while (pud++, addr = next, addr != end);
}

static void mark_kernel_p4d(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	unsigned long next;
	struct page *page;
	p4d_t *p4d;
	int i;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none(*p4d))
			continue;
		if (!p4d_folded(*p4d)) {
			page = phys_to_page(p4d_val(*p4d));
			for (i = 0; i < 4; i++)
				set_bit(PG_arch_1, &page[i].flags);
		}
		mark_kernel_pud(p4d, addr, next);
	} while (p4d++, addr = next, addr != end);
}

static void mark_kernel_pgd(void)
{
	unsigned long addr, next, max_addr;
	struct page *page;
	pgd_t *pgd;
	int i;

	addr = 0;
	/*
	 * Figure out maximum virtual address accessible with the
	 * kernel ASCE. This is required to keep the page table walker
	 * from accessing non-existent entries.
	 */
	max_addr = (S390_lowcore.kernel_asce.val & _ASCE_TYPE_MASK) >> 2;
	max_addr = 1UL << (max_addr * 11 + 31);
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, max_addr);
		if (pgd_none(*pgd))
			continue;
		if (!pgd_folded(*pgd)) {
			page = phys_to_page(pgd_val(*pgd));
			for (i = 0; i < 4; i++)
				set_bit(PG_arch_1, &page[i].flags);
		}
		mark_kernel_p4d(pgd, addr, next);
	} while (pgd++, addr = next, addr != max_addr);
}

void __init cmma_init_nodat(void)
{
	struct page *page;
	unsigned long start, end, ix;
	int i;

	if (cmma_flag < 2)
		return;
	/* Mark pages used in kernel page tables */
	mark_kernel_pgd();
	page = virt_to_page(&swapper_pg_dir);
	for (i = 0; i < 4; i++)
		set_bit(PG_arch_1, &page[i].flags);
	page = virt_to_page(&invalid_pg_dir);
	for (i = 0; i < 4; i++)
		set_bit(PG_arch_1, &page[i].flags);

	/* Set all kernel pages not used for page tables to stable/no-dat */
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start, &end, NULL) {
		page = pfn_to_page(start);
		for (ix = start; ix < end; ix++, page++) {
			if (__test_and_clear_bit(PG_arch_1, &page->flags))
				continue;	/* skip page table pages */
			if (!list_empty(&page->lru))
				continue;	/* skip free pages */
			__set_page_stable_nodat(page_to_virt(page), 1);
		}
	}
}

void arch_free_page(struct page *page, int order)
{
	if (!cmma_flag)
		return;
	__set_page_unused(page_to_virt(page), 1UL << order);
}

void arch_alloc_page(struct page *page, int order)
{
	if (!cmma_flag)
		return;
	if (cmma_flag < 2)
		__set_page_stable_dat(page_to_virt(page), 1UL << order);
	else
		__set_page_stable_nodat(page_to_virt(page), 1UL << order);
}

void arch_set_page_dat(struct page *page, int order)
{
	if (!cmma_flag)
		return;
	__set_page_stable_dat(page_to_virt(page), 1UL << order);
}

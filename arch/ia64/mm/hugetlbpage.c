// SPDX-License-Identifier: GPL-2.0
/*
 * IA-64 Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2002-2004 Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 2003-2004 Ken Chen <kenneth.w.chen@intel.com>
 *
 * Sep, 2003: add numa support
 * Feb, 2004: dynamic hugetlb page size via boot parameter
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/log2.h>
#include <asm/mman.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

unsigned int hpage_shift = HPAGE_SHIFT_DEFAULT;
EXPORT_SYMBOL(hpage_shift);

pte_t *
huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
	       unsigned long addr, unsigned long sz)
{
	unsigned long taddr = htlbpage_to_page(addr);
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, taddr);
	p4d = p4d_offset(pgd, taddr);
	pud = pud_alloc(mm, p4d, taddr);
	if (pud) {
		pmd = pmd_alloc(mm, pud, taddr);
		if (pmd)
			pte = pte_alloc_map(mm, pmd, taddr);
	}
	return pte;
}

pte_t *
huge_pte_offset (struct mm_struct *mm, unsigned long addr, unsigned long sz)
{
	unsigned long taddr = htlbpage_to_page(addr);
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, taddr);
	if (pgd_present(*pgd)) {
		p4d = p4d_offset(pgd, taddr);
		if (p4d_present(*p4d)) {
			pud = pud_offset(p4d, taddr);
			if (pud_present(*pud)) {
				pmd = pmd_offset(pud, taddr);
				if (pmd_present(*pmd))
					pte = pte_offset_map(pmd, taddr);
			}
		}
	}

	return pte;
}

#define mk_pte_huge(entry) { pte_val(entry) |= _PAGE_P; }

/*
 * Don't actually need to do any preparation, but need to make sure
 * the address is in the right region.
 */
int prepare_hugepage_range(struct file *file,
			unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	if (REGION_NUMBER(addr) != RGN_HPAGE)
		return -EINVAL;

	return 0;
}

struct page *follow_huge_addr(struct mm_struct *mm, unsigned long addr, int write)
{
	struct page *page;
	pte_t *ptep;

	if (REGION_NUMBER(addr) != RGN_HPAGE)
		return ERR_PTR(-EINVAL);

	ptep = huge_pte_offset(mm, addr, HPAGE_SIZE);
	if (!ptep || pte_none(*ptep))
		return NULL;
	page = pte_page(*ptep);
	page += ((addr & ~HPAGE_MASK) >> PAGE_SHIFT);
	return page;
}
int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}

void hugetlb_free_pgd_range(struct mmu_gather *tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
	/*
	 * This is called to free hugetlb page tables.
	 *
	 * The offset of these addresses from the base of the hugetlb
	 * region must be scaled down by HPAGE_SIZE/PAGE_SIZE so that
	 * the standard free_pgd_range will free the right page tables.
	 *
	 * If floor and ceiling are also in the hugetlb region, they
	 * must likewise be scaled down; but if outside, left unchanged.
	 */

	addr = htlbpage_to_page(addr);
	end  = htlbpage_to_page(end);
	if (REGION_NUMBER(floor) == RGN_HPAGE)
		floor = htlbpage_to_page(floor);
	if (REGION_NUMBER(ceiling) == RGN_HPAGE)
		ceiling = htlbpage_to_page(ceiling);

	free_pgd_range(tlb, addr, end, floor, ceiling);
}

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct vm_unmapped_area_info info;

	if (len > RGN_MAP_LIMIT)
		return -ENOMEM;
	if (len & ~HPAGE_MASK)
		return -EINVAL;

	/* Handle MAP_FIXED */
	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	/* This code assumes that RGN_HPAGE != 0. */
	if ((REGION_NUMBER(addr) != RGN_HPAGE) || (addr & (HPAGE_SIZE - 1)))
		addr = HPAGE_REGION_BASE;

	info.flags = 0;
	info.length = len;
	info.low_limit = addr;
	info.high_limit = HPAGE_REGION_BASE + RGN_MAP_LIMIT;
	info.align_mask = PAGE_MASK & (HPAGE_SIZE - 1);
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

static int __init hugetlb_setup_sz(char *str)
{
	u64 tr_pages;
	unsigned long long size;

	if (ia64_pal_vm_page_size(&tr_pages, NULL) != 0)
		/*
		 * shouldn't happen, but just in case.
		 */
		tr_pages = 0x15557000UL;

	size = memparse(str, &str);
	if (*str || !is_power_of_2(size) || !(tr_pages & size) ||
		size <= PAGE_SIZE ||
		size >= (1UL << PAGE_SHIFT << MAX_ORDER)) {
		printk(KERN_WARNING "Invalid huge page size specified\n");
		return 1;
	}

	hpage_shift = __ffs(size);
	/*
	 * boot cpu already executed ia64_mmu_init, and has HPAGE_SHIFT_DEFAULT
	 * override here with new page shift.
	 */
	ia64_set_rr(HPAGE_REGION_BASE, hpage_shift << 2);
	return 0;
}
early_param("hugepagesz", hugetlb_setup_sz);

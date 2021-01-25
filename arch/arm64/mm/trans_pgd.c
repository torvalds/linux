// SPDX-License-Identifier: GPL-2.0

/*
 * Transitional page tables for kexec and hibernate
 *
 * This file derived from: arch/arm64/kernel/hibernate.c
 *
 * Copyright (c) 2020, Microsoft Corporation.
 * Pavel Tatashin <pasha.tatashin@soleen.com>
 *
 */

/*
 * Transitional tables are used during system transferring from one world to
 * another: such as during hibernate restore, and kexec reboots. During these
 * phases one cannot rely on page table not being overwritten. This is because
 * hibernate and kexec can overwrite the current page tables during transition.
 */

#include <asm/trans_pgd.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <linux/suspend.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/mmzone.h>

static void *trans_alloc(struct trans_pgd_info *info)
{
	return info->trans_alloc_page(info->trans_alloc_arg);
}

static void _copy_pte(pte_t *dst_ptep, pte_t *src_ptep, unsigned long addr)
{
	pte_t pte = READ_ONCE(*src_ptep);

	if (pte_valid(pte)) {
		/*
		 * Resume will overwrite areas that may be marked
		 * read only (code, rodata). Clear the RDONLY bit from
		 * the temporary mappings we use during restore.
		 */
		set_pte(dst_ptep, pte_mkwrite(pte));
	} else if (debug_pagealloc_enabled() && !pte_none(pte)) {
		/*
		 * debug_pagealloc will removed the PTE_VALID bit if
		 * the page isn't in use by the resume kernel. It may have
		 * been in use by the original kernel, in which case we need
		 * to put it back in our copy to do the restore.
		 *
		 * Before marking this entry valid, check the pfn should
		 * be mapped.
		 */
		BUG_ON(!pfn_valid(pte_pfn(pte)));

		set_pte(dst_ptep, pte_mkpresent(pte_mkwrite(pte)));
	}
}

static int copy_pte(struct trans_pgd_info *info, pmd_t *dst_pmdp,
		    pmd_t *src_pmdp, unsigned long start, unsigned long end)
{
	pte_t *src_ptep;
	pte_t *dst_ptep;
	unsigned long addr = start;

	dst_ptep = trans_alloc(info);
	if (!dst_ptep)
		return -ENOMEM;
	pmd_populate_kernel(NULL, dst_pmdp, dst_ptep);
	dst_ptep = pte_offset_kernel(dst_pmdp, start);

	src_ptep = pte_offset_kernel(src_pmdp, start);
	do {
		_copy_pte(dst_ptep, src_ptep, addr);
	} while (dst_ptep++, src_ptep++, addr += PAGE_SIZE, addr != end);

	return 0;
}

static int copy_pmd(struct trans_pgd_info *info, pud_t *dst_pudp,
		    pud_t *src_pudp, unsigned long start, unsigned long end)
{
	pmd_t *src_pmdp;
	pmd_t *dst_pmdp;
	unsigned long next;
	unsigned long addr = start;

	if (pud_none(READ_ONCE(*dst_pudp))) {
		dst_pmdp = trans_alloc(info);
		if (!dst_pmdp)
			return -ENOMEM;
		pud_populate(NULL, dst_pudp, dst_pmdp);
	}
	dst_pmdp = pmd_offset(dst_pudp, start);

	src_pmdp = pmd_offset(src_pudp, start);
	do {
		pmd_t pmd = READ_ONCE(*src_pmdp);

		next = pmd_addr_end(addr, end);
		if (pmd_none(pmd))
			continue;
		if (pmd_table(pmd)) {
			if (copy_pte(info, dst_pmdp, src_pmdp, addr, next))
				return -ENOMEM;
		} else {
			set_pmd(dst_pmdp,
				__pmd(pmd_val(pmd) & ~PMD_SECT_RDONLY));
		}
	} while (dst_pmdp++, src_pmdp++, addr = next, addr != end);

	return 0;
}

static int copy_pud(struct trans_pgd_info *info, p4d_t *dst_p4dp,
		    p4d_t *src_p4dp, unsigned long start,
		    unsigned long end)
{
	pud_t *dst_pudp;
	pud_t *src_pudp;
	unsigned long next;
	unsigned long addr = start;

	if (p4d_none(READ_ONCE(*dst_p4dp))) {
		dst_pudp = trans_alloc(info);
		if (!dst_pudp)
			return -ENOMEM;
		p4d_populate(NULL, dst_p4dp, dst_pudp);
	}
	dst_pudp = pud_offset(dst_p4dp, start);

	src_pudp = pud_offset(src_p4dp, start);
	do {
		pud_t pud = READ_ONCE(*src_pudp);

		next = pud_addr_end(addr, end);
		if (pud_none(pud))
			continue;
		if (pud_table(pud)) {
			if (copy_pmd(info, dst_pudp, src_pudp, addr, next))
				return -ENOMEM;
		} else {
			set_pud(dst_pudp,
				__pud(pud_val(pud) & ~PUD_SECT_RDONLY));
		}
	} while (dst_pudp++, src_pudp++, addr = next, addr != end);

	return 0;
}

static int copy_p4d(struct trans_pgd_info *info, pgd_t *dst_pgdp,
		    pgd_t *src_pgdp, unsigned long start,
		    unsigned long end)
{
	p4d_t *dst_p4dp;
	p4d_t *src_p4dp;
	unsigned long next;
	unsigned long addr = start;

	dst_p4dp = p4d_offset(dst_pgdp, start);
	src_p4dp = p4d_offset(src_pgdp, start);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none(READ_ONCE(*src_p4dp)))
			continue;
		if (copy_pud(info, dst_p4dp, src_p4dp, addr, next))
			return -ENOMEM;
	} while (dst_p4dp++, src_p4dp++, addr = next, addr != end);

	return 0;
}

static int copy_page_tables(struct trans_pgd_info *info, pgd_t *dst_pgdp,
			    unsigned long start, unsigned long end)
{
	unsigned long next;
	unsigned long addr = start;
	pgd_t *src_pgdp = pgd_offset_k(start);

	dst_pgdp = pgd_offset_pgd(dst_pgdp, start);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none(READ_ONCE(*src_pgdp)))
			continue;
		if (copy_p4d(info, dst_pgdp, src_pgdp, addr, next))
			return -ENOMEM;
	} while (dst_pgdp++, src_pgdp++, addr = next, addr != end);

	return 0;
}

/*
 * Create trans_pgd and copy linear map.
 * info:	contains allocator and its argument
 * dst_pgdp:	new page table that is created, and to which map is copied.
 * start:	Start of the interval (inclusive).
 * end:		End of the interval (exclusive).
 *
 * Returns 0 on success, and -ENOMEM on failure.
 */
int trans_pgd_create_copy(struct trans_pgd_info *info, pgd_t **dst_pgdp,
			  unsigned long start, unsigned long end)
{
	int rc;
	pgd_t *trans_pgd = trans_alloc(info);

	if (!trans_pgd) {
		pr_err("Failed to allocate memory for temporary page tables.\n");
		return -ENOMEM;
	}

	rc = copy_page_tables(info, trans_pgd, start, end);
	if (!rc)
		*dst_pgdp = trans_pgd;

	return rc;
}

/*
 * Add map entry to trans_pgd for a base-size page at PTE level.
 * info:	contains allocator and its argument
 * trans_pgd:	page table in which new map is added.
 * page:	page to be mapped.
 * dst_addr:	new VA address for the page
 * pgprot:	protection for the page.
 *
 * Returns 0 on success, and -ENOMEM on failure.
 */
int trans_pgd_map_page(struct trans_pgd_info *info, pgd_t *trans_pgd,
		       void *page, unsigned long dst_addr, pgprot_t pgprot)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = pgd_offset_pgd(trans_pgd, dst_addr);
	if (pgd_none(READ_ONCE(*pgdp))) {
		p4dp = trans_alloc(info);
		if (!pgdp)
			return -ENOMEM;
		pgd_populate(NULL, pgdp, p4dp);
	}

	p4dp = p4d_offset(pgdp, dst_addr);
	if (p4d_none(READ_ONCE(*p4dp))) {
		pudp = trans_alloc(info);
		if (!pudp)
			return -ENOMEM;
		p4d_populate(NULL, p4dp, pudp);
	}

	pudp = pud_offset(p4dp, dst_addr);
	if (pud_none(READ_ONCE(*pudp))) {
		pmdp = trans_alloc(info);
		if (!pmdp)
			return -ENOMEM;
		pud_populate(NULL, pudp, pmdp);
	}

	pmdp = pmd_offset(pudp, dst_addr);
	if (pmd_none(READ_ONCE(*pmdp))) {
		ptep = trans_alloc(info);
		if (!ptep)
			return -ENOMEM;
		pmd_populate_kernel(NULL, pmdp, ptep);
	}

	ptep = pte_offset_kernel(pmdp, dst_addr);
	set_pte(ptep, pfn_pte(virt_to_pfn(page), pgprot));

	return 0;
}

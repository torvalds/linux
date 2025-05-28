// SPDX-License-Identifier: GPL-2.0

/*
 * Transitional page tables for kexec and hibernate
 *
 * This file derived from: arch/arm64/kernel/hibernate.c
 *
 * Copyright (c) 2021, Microsoft Corporation.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
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
#include <linux/kfence.h>

static void *trans_alloc(struct trans_pgd_info *info)
{
	return info->trans_alloc_page(info->trans_alloc_arg);
}

static void _copy_pte(pte_t *dst_ptep, pte_t *src_ptep, unsigned long addr)
{
	pte_t pte = __ptep_get(src_ptep);

	if (pte_valid(pte)) {
		/*
		 * Resume will overwrite areas that may be marked
		 * read only (code, rodata). Clear the RDONLY bit from
		 * the temporary mappings we use during restore.
		 */
		__set_pte(dst_ptep, pte_mkwrite_novma(pte));
	} else if (!pte_none(pte)) {
		/*
		 * debug_pagealloc will removed the PTE_VALID bit if
		 * the page isn't in use by the resume kernel. It may have
		 * been in use by the original kernel, in which case we need
		 * to put it back in our copy to do the restore.
		 *
		 * Other cases include kfence / vmalloc / memfd_secret which
		 * may call `set_direct_map_invalid_noflush()`.
		 *
		 * Before marking this entry valid, check the pfn should
		 * be mapped.
		 */
		BUG_ON(!pfn_valid(pte_pfn(pte)));

		__set_pte(dst_ptep, pte_mkvalid(pte_mkwrite_novma(pte)));
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

	if (pgd_none(READ_ONCE(*dst_pgdp))) {
		dst_p4dp = trans_alloc(info);
		if (!dst_p4dp)
			return -ENOMEM;
		pgd_populate(NULL, dst_pgdp, dst_p4dp);
	}

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
 * The page we want to idmap may be outside the range covered by VA_BITS that
 * can be built using the kernel's p?d_populate() helpers. As a one off, for a
 * single page, we build these page tables bottom up and just assume that will
 * need the maximum T0SZ.
 *
 * Returns 0 on success, and -ENOMEM on failure.
 * On success trans_ttbr0 contains page table with idmapped page, t0sz is set to
 * maximum T0SZ for this page.
 */
int trans_pgd_idmap_page(struct trans_pgd_info *info, phys_addr_t *trans_ttbr0,
			 unsigned long *t0sz, void *page)
{
	phys_addr_t dst_addr = virt_to_phys(page);
	unsigned long pfn = __phys_to_pfn(dst_addr);
	int max_msb = (dst_addr & GENMASK(52, 48)) ? 51 : 47;
	int bits_mapped = PAGE_SHIFT - 4;
	unsigned long level_mask, prev_level_entry, *levels[4];
	int this_level, index, level_lsb, level_msb;

	dst_addr &= PAGE_MASK;
	prev_level_entry = pte_val(pfn_pte(pfn, PAGE_KERNEL_ROX));

	for (this_level = 3; this_level >= 0; this_level--) {
		levels[this_level] = trans_alloc(info);
		if (!levels[this_level])
			return -ENOMEM;

		level_lsb = ARM64_HW_PGTABLE_LEVEL_SHIFT(this_level);
		level_msb = min(level_lsb + bits_mapped, max_msb);
		level_mask = GENMASK_ULL(level_msb, level_lsb);

		index = (dst_addr & level_mask) >> level_lsb;
		*(levels[this_level] + index) = prev_level_entry;

		pfn = virt_to_pfn(levels[this_level]);
		prev_level_entry = pte_val(pfn_pte(pfn,
						   __pgprot(PMD_TYPE_TABLE)));

		if (level_msb == max_msb)
			break;
	}

	*trans_ttbr0 = phys_to_ttbr(__pfn_to_phys(pfn));
	*t0sz = TCR_T0SZ(max_msb + 1);

	return 0;
}

/*
 * Create a copy of the vector table so we can call HVC_SET_VECTORS or
 * HVC_SOFT_RESTART from contexts where the table may be overwritten.
 */
int trans_pgd_copy_el2_vectors(struct trans_pgd_info *info,
			       phys_addr_t *el2_vectors)
{
	void *hyp_stub = trans_alloc(info);

	if (!hyp_stub)
		return -ENOMEM;
	*el2_vectors = virt_to_phys(hyp_stub);
	memcpy(hyp_stub, &trans_pgd_stub_vectors, ARM64_VECTOR_TABLE_LEN);
	caches_clean_inval_pou((unsigned long)hyp_stub,
			       (unsigned long)hyp_stub +
			       ARM64_VECTOR_TABLE_LEN);
	dcache_clean_inval_poc((unsigned long)hyp_stub,
			       (unsigned long)hyp_stub +
			       ARM64_VECTOR_TABLE_LEN);

	return 0;
}

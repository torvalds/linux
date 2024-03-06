// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 SiFive
 */

#include <linux/pagewalk.h>
#include <linux/pgtable.h>
#include <linux/vmalloc.h>
#include <asm/tlbflush.h>
#include <asm/bitops.h>
#include <asm/set_memory.h>

struct pageattr_masks {
	pgprot_t set_mask;
	pgprot_t clear_mask;
};

static unsigned long set_pageattr_masks(unsigned long val, struct mm_walk *walk)
{
	struct pageattr_masks *masks = walk->private;
	unsigned long new_val = val;

	new_val &= ~(pgprot_val(masks->clear_mask));
	new_val |= (pgprot_val(masks->set_mask));

	return new_val;
}

static int pageattr_p4d_entry(p4d_t *p4d, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	p4d_t val = p4dp_get(p4d);

	if (p4d_leaf(val)) {
		val = __p4d(set_pageattr_masks(p4d_val(val), walk));
		set_p4d(p4d, val);
	}

	return 0;
}

static int pageattr_pud_entry(pud_t *pud, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	pud_t val = pudp_get(pud);

	if (pud_leaf(val)) {
		val = __pud(set_pageattr_masks(pud_val(val), walk));
		set_pud(pud, val);
	}

	return 0;
}

static int pageattr_pmd_entry(pmd_t *pmd, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	pmd_t val = pmdp_get(pmd);

	if (pmd_leaf(val)) {
		val = __pmd(set_pageattr_masks(pmd_val(val), walk));
		set_pmd(pmd, val);
	}

	return 0;
}

static int pageattr_pte_entry(pte_t *pte, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	pte_t val = ptep_get(pte);

	val = __pte(set_pageattr_masks(pte_val(val), walk));
	set_pte(pte, val);

	return 0;
}

static int pageattr_pte_hole(unsigned long addr, unsigned long next,
			     int depth, struct mm_walk *walk)
{
	/* Nothing to do here */
	return 0;
}

static const struct mm_walk_ops pageattr_ops = {
	.p4d_entry = pageattr_p4d_entry,
	.pud_entry = pageattr_pud_entry,
	.pmd_entry = pageattr_pmd_entry,
	.pte_entry = pageattr_pte_entry,
	.pte_hole = pageattr_pte_hole,
	.walk_lock = PGWALK_RDLOCK,
};

#ifdef CONFIG_64BIT
static int __split_linear_mapping_pmd(pud_t *pudp,
				      unsigned long vaddr, unsigned long end)
{
	pmd_t *pmdp;
	unsigned long next;

	pmdp = pmd_offset(pudp, vaddr);

	do {
		next = pmd_addr_end(vaddr, end);

		if (next - vaddr >= PMD_SIZE &&
		    vaddr <= (vaddr & PMD_MASK) && end >= next)
			continue;

		if (pmd_leaf(pmdp_get(pmdp))) {
			struct page *pte_page;
			unsigned long pfn = _pmd_pfn(pmdp_get(pmdp));
			pgprot_t prot = __pgprot(pmd_val(pmdp_get(pmdp)) & ~_PAGE_PFN_MASK);
			pte_t *ptep_new;
			int i;

			pte_page = alloc_page(GFP_KERNEL);
			if (!pte_page)
				return -ENOMEM;

			ptep_new = (pte_t *)page_address(pte_page);
			for (i = 0; i < PTRS_PER_PTE; ++i, ++ptep_new)
				set_pte(ptep_new, pfn_pte(pfn + i, prot));

			smp_wmb();

			set_pmd(pmdp, pfn_pmd(page_to_pfn(pte_page), PAGE_TABLE));
		}
	} while (pmdp++, vaddr = next, vaddr != end);

	return 0;
}

static int __split_linear_mapping_pud(p4d_t *p4dp,
				      unsigned long vaddr, unsigned long end)
{
	pud_t *pudp;
	unsigned long next;
	int ret;

	pudp = pud_offset(p4dp, vaddr);

	do {
		next = pud_addr_end(vaddr, end);

		if (next - vaddr >= PUD_SIZE &&
		    vaddr <= (vaddr & PUD_MASK) && end >= next)
			continue;

		if (pud_leaf(pudp_get(pudp))) {
			struct page *pmd_page;
			unsigned long pfn = _pud_pfn(pudp_get(pudp));
			pgprot_t prot = __pgprot(pud_val(pudp_get(pudp)) & ~_PAGE_PFN_MASK);
			pmd_t *pmdp_new;
			int i;

			pmd_page = alloc_page(GFP_KERNEL);
			if (!pmd_page)
				return -ENOMEM;

			pmdp_new = (pmd_t *)page_address(pmd_page);
			for (i = 0; i < PTRS_PER_PMD; ++i, ++pmdp_new)
				set_pmd(pmdp_new,
					pfn_pmd(pfn + ((i * PMD_SIZE) >> PAGE_SHIFT), prot));

			smp_wmb();

			set_pud(pudp, pfn_pud(page_to_pfn(pmd_page), PAGE_TABLE));
		}

		ret = __split_linear_mapping_pmd(pudp, vaddr, next);
		if (ret)
			return ret;
	} while (pudp++, vaddr = next, vaddr != end);

	return 0;
}

static int __split_linear_mapping_p4d(pgd_t *pgdp,
				      unsigned long vaddr, unsigned long end)
{
	p4d_t *p4dp;
	unsigned long next;
	int ret;

	p4dp = p4d_offset(pgdp, vaddr);

	do {
		next = p4d_addr_end(vaddr, end);

		/*
		 * If [vaddr; end] contains [vaddr & P4D_MASK; next], we don't
		 * need to split, we'll change the protections on the whole P4D.
		 */
		if (next - vaddr >= P4D_SIZE &&
		    vaddr <= (vaddr & P4D_MASK) && end >= next)
			continue;

		if (p4d_leaf(p4dp_get(p4dp))) {
			struct page *pud_page;
			unsigned long pfn = _p4d_pfn(p4dp_get(p4dp));
			pgprot_t prot = __pgprot(p4d_val(p4dp_get(p4dp)) & ~_PAGE_PFN_MASK);
			pud_t *pudp_new;
			int i;

			pud_page = alloc_page(GFP_KERNEL);
			if (!pud_page)
				return -ENOMEM;

			/*
			 * Fill the pud level with leaf puds that have the same
			 * protections as the leaf p4d.
			 */
			pudp_new = (pud_t *)page_address(pud_page);
			for (i = 0; i < PTRS_PER_PUD; ++i, ++pudp_new)
				set_pud(pudp_new,
					pfn_pud(pfn + ((i * PUD_SIZE) >> PAGE_SHIFT), prot));

			/*
			 * Make sure the pud filling is not reordered with the
			 * p4d store which could result in seeing a partially
			 * filled pud level.
			 */
			smp_wmb();

			set_p4d(p4dp, pfn_p4d(page_to_pfn(pud_page), PAGE_TABLE));
		}

		ret = __split_linear_mapping_pud(p4dp, vaddr, next);
		if (ret)
			return ret;
	} while (p4dp++, vaddr = next, vaddr != end);

	return 0;
}

static int __split_linear_mapping_pgd(pgd_t *pgdp,
				      unsigned long vaddr,
				      unsigned long end)
{
	unsigned long next;
	int ret;

	do {
		next = pgd_addr_end(vaddr, end);
		/* We never use PGD mappings for the linear mapping */
		ret = __split_linear_mapping_p4d(pgdp, vaddr, next);
		if (ret)
			return ret;
	} while (pgdp++, vaddr = next, vaddr != end);

	return 0;
}

static int split_linear_mapping(unsigned long start, unsigned long end)
{
	return __split_linear_mapping_pgd(pgd_offset_k(start), start, end);
}
#endif	/* CONFIG_64BIT */

static int __set_memory(unsigned long addr, int numpages, pgprot_t set_mask,
			pgprot_t clear_mask)
{
	int ret;
	unsigned long start = addr;
	unsigned long end = start + PAGE_SIZE * numpages;
	unsigned long __maybe_unused lm_start;
	unsigned long __maybe_unused lm_end;
	struct pageattr_masks masks = {
		.set_mask = set_mask,
		.clear_mask = clear_mask
	};

	if (!numpages)
		return 0;

	mmap_write_lock(&init_mm);

#ifdef CONFIG_64BIT
	/*
	 * We are about to change the permissions of a kernel mapping, we must
	 * apply the same changes to its linear mapping alias, which may imply
	 * splitting a huge mapping.
	 */

	if (is_vmalloc_or_module_addr((void *)start)) {
		struct vm_struct *area = NULL;
		int i, page_start;

		area = find_vm_area((void *)start);
		page_start = (start - (unsigned long)area->addr) >> PAGE_SHIFT;

		for (i = page_start; i < page_start + numpages; ++i) {
			lm_start = (unsigned long)page_address(area->pages[i]);
			lm_end = lm_start + PAGE_SIZE;

			ret = split_linear_mapping(lm_start, lm_end);
			if (ret)
				goto unlock;

			ret = walk_page_range_novma(&init_mm, lm_start, lm_end,
						    &pageattr_ops, NULL, &masks);
			if (ret)
				goto unlock;
		}
	} else if (is_kernel_mapping(start) || is_linear_mapping(start)) {
		if (is_kernel_mapping(start)) {
			lm_start = (unsigned long)lm_alias(start);
			lm_end = (unsigned long)lm_alias(end);
		} else {
			lm_start = start;
			lm_end = end;
		}

		ret = split_linear_mapping(lm_start, lm_end);
		if (ret)
			goto unlock;

		ret = walk_page_range_novma(&init_mm, lm_start, lm_end,
					    &pageattr_ops, NULL, &masks);
		if (ret)
			goto unlock;
	}

	ret =  walk_page_range_novma(&init_mm, start, end, &pageattr_ops, NULL,
				     &masks);

unlock:
	mmap_write_unlock(&init_mm);

	/*
	 * We can't use flush_tlb_kernel_range() here as we may have split a
	 * hugepage that is larger than that, so let's flush everything.
	 */
	flush_tlb_all();
#else
	ret =  walk_page_range_novma(&init_mm, start, end, &pageattr_ops, NULL,
				     &masks);

	mmap_write_unlock(&init_mm);

	flush_tlb_kernel_range(start, end);
#endif

	return ret;
}

int set_memory_rw_nx(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, __pgprot(_PAGE_READ | _PAGE_WRITE),
			    __pgprot(_PAGE_EXEC));
}

int set_memory_ro(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, __pgprot(_PAGE_READ),
			    __pgprot(_PAGE_WRITE));
}

int set_memory_rw(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, __pgprot(_PAGE_READ | _PAGE_WRITE),
			    __pgprot(0));
}

int set_memory_x(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, __pgprot(_PAGE_EXEC), __pgprot(0));
}

int set_memory_nx(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, __pgprot(0), __pgprot(_PAGE_EXEC));
}

int set_direct_map_invalid_noflush(struct page *page)
{
	return __set_memory((unsigned long)page_address(page), 1,
			    __pgprot(0), __pgprot(_PAGE_PRESENT));
}

int set_direct_map_default_noflush(struct page *page)
{
	return __set_memory((unsigned long)page_address(page), 1,
			    PAGE_KERNEL, __pgprot(_PAGE_EXEC));
}

#ifdef CONFIG_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (!debug_pagealloc_enabled())
		return;

	if (enable)
		__set_memory((unsigned long)page_address(page), numpages,
			     __pgprot(_PAGE_PRESENT), __pgprot(0));
	else
		__set_memory((unsigned long)page_address(page), numpages,
			     __pgprot(0), __pgprot(_PAGE_PRESENT));
}
#endif

bool kernel_page_present(struct page *page)
{
	unsigned long addr = (unsigned long)page_address(page);
	pgd_t *pgd;
	pud_t *pud;
	p4d_t *p4d;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset_k(addr);
	if (!pgd_present(pgdp_get(pgd)))
		return false;
	if (pgd_leaf(pgdp_get(pgd)))
		return true;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(p4dp_get(p4d)))
		return false;
	if (p4d_leaf(p4dp_get(p4d)))
		return true;

	pud = pud_offset(p4d, addr);
	if (!pud_present(pudp_get(pud)))
		return false;
	if (pud_leaf(pudp_get(pud)))
		return true;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(pmdp_get(pmd)))
		return false;
	if (pmd_leaf(pmdp_get(pmd)))
		return true;

	pte = pte_offset_kernel(pmd, addr);
	return pte_present(ptep_get(pte));
}

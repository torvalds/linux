/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILE Huge TLB Page Support for Kernel.
 * Taken from i386 hugetlb implementation:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <linux/mman.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>

#ifdef CONFIG_HUGETLB_SUPER_PAGES

/*
 * Provide an additional huge page size (in addition to the regular default
 * huge page size) if no "hugepagesz" arguments are specified.
 * Note that it must be smaller than the default huge page size so
 * that it's possible to allocate them on demand from the buddy allocator.
 * You can change this to 64K (on a 16K build), 256K, 1M, or 4M,
 * or not define it at all.
 */
#define ADDITIONAL_HUGE_SIZE (1024 * 1024UL)

/* "Extra" page-size multipliers, one per level of the page table. */
int huge_shift[HUGE_SHIFT_ENTRIES] = {
#ifdef ADDITIONAL_HUGE_SIZE
#define ADDITIONAL_HUGE_SHIFT __builtin_ctzl(ADDITIONAL_HUGE_SIZE / PAGE_SIZE)
	[HUGE_SHIFT_PAGE] = ADDITIONAL_HUGE_SHIFT
#endif
};

#endif

pte_t *huge_pte_alloc(struct mm_struct *mm,
		      unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;

	addr &= -sz;   /* Mask off any low bits in the address. */

	pgd = pgd_offset(mm, addr);
	pud = pud_alloc(mm, pgd, addr);

#ifdef CONFIG_HUGETLB_SUPER_PAGES
	if (sz >= PGDIR_SIZE) {
		BUG_ON(sz != PGDIR_SIZE &&
		       sz != PGDIR_SIZE << huge_shift[HUGE_SHIFT_PGDIR]);
		return (pte_t *)pud;
	} else {
		pmd_t *pmd = pmd_alloc(mm, pud, addr);
		if (sz >= PMD_SIZE) {
			BUG_ON(sz != PMD_SIZE &&
			       sz != (PMD_SIZE << huge_shift[HUGE_SHIFT_PMD]));
			return (pte_t *)pmd;
		}
		else {
			if (sz != PAGE_SIZE << huge_shift[HUGE_SHIFT_PAGE])
				panic("Unexpected page size %#lx\n", sz);
			return pte_alloc_map(mm, NULL, pmd, addr);
		}
	}
#else
	BUG_ON(sz != PMD_SIZE);
	return (pte_t *) pmd_alloc(mm, pud, addr);
#endif
}

static pte_t *get_pte(pte_t *base, int index, int level)
{
	pte_t *ptep = base + index;
#ifdef CONFIG_HUGETLB_SUPER_PAGES
	if (!pte_present(*ptep) && huge_shift[level] != 0) {
		unsigned long mask = -1UL << huge_shift[level];
		pte_t *super_ptep = base + (index & mask);
		pte_t pte = *super_ptep;
		if (pte_present(pte) && pte_super(pte))
			ptep = super_ptep;
	}
#endif
	return ptep;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
#ifdef CONFIG_HUGETLB_SUPER_PAGES
	pte_t *pte;
#endif

	/* Get the top-level page table entry. */
	pgd = (pgd_t *)get_pte((pte_t *)mm->pgd, pgd_index(addr), 0);

	/* We don't have four levels. */
	pud = pud_offset(pgd, addr);
#ifndef __PAGETABLE_PUD_FOLDED
# error support fourth page table level
#endif
	if (!pud_present(*pud))
		return NULL;

	/* Check for an L0 huge PTE, if we have three levels. */
#ifndef __PAGETABLE_PMD_FOLDED
	if (pud_huge(*pud))
		return (pte_t *)pud;

	pmd = (pmd_t *)get_pte((pte_t *)pud_page_vaddr(*pud),
			       pmd_index(addr), 1);
	if (!pmd_present(*pmd))
		return NULL;
#else
	pmd = pmd_offset(pud, addr);
#endif

	/* Check for an L1 huge PTE. */
	if (pmd_huge(*pmd))
		return (pte_t *)pmd;

#ifdef CONFIG_HUGETLB_SUPER_PAGES
	/* Check for an L2 huge PTE. */
	pte = get_pte((pte_t *)pmd_page_vaddr(*pmd), pte_index(addr), 2);
	if (!pte_present(*pte))
		return NULL;
	if (pte_super(*pte))
		return pte;
#endif

	return NULL;
}

int pmd_huge(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_HUGE_PAGE);
}

int pud_huge(pud_t pud)
{
	return !!(pud_val(pud) & _PAGE_HUGE_PAGE);
}

#ifdef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
static unsigned long hugetlb_get_unmapped_area_bottomup(struct file *file,
		unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info;

	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = TASK_SIZE;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

static unsigned long hugetlb_get_unmapped_area_topdown(struct file *file,
		unsigned long addr0, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info;
	unsigned long addr;

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = current->mm->mmap_base;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	info.align_offset = 0;
	addr = vm_unmapped_area(&info);

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	if (addr & ~PAGE_MASK) {
		VM_BUG_ON(addr != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = TASK_SIZE;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, huge_page_size(h));
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	if (current->mm->get_unmapped_area == arch_get_unmapped_area)
		return hugetlb_get_unmapped_area_bottomup(file, addr, len,
				pgoff, flags);
	else
		return hugetlb_get_unmapped_area_topdown(file, addr, len,
				pgoff, flags);
}
#endif /* HAVE_ARCH_HUGETLB_UNMAPPED_AREA */

#ifdef CONFIG_HUGETLB_SUPER_PAGES
static __init int __setup_hugepagesz(unsigned long ps)
{
	int log_ps = __builtin_ctzl(ps);
	int level, base_shift;

	if ((1UL << log_ps) != ps || (log_ps & 1) != 0) {
		pr_warn("Not enabling %ld byte huge pages; must be a power of four\n",
			ps);
		return -EINVAL;
	}

	if (ps > 64*1024*1024*1024UL) {
		pr_warn("Not enabling %ld MB huge pages; largest legal value is 64 GB\n",
			ps >> 20);
		return -EINVAL;
	} else if (ps >= PUD_SIZE) {
		static long hv_jpage_size;
		if (hv_jpage_size == 0)
			hv_jpage_size = hv_sysconf(HV_SYSCONF_PAGE_SIZE_JUMBO);
		if (hv_jpage_size != PUD_SIZE) {
			pr_warn("Not enabling >= %ld MB huge pages: hypervisor reports size %ld\n",
				PUD_SIZE >> 20, hv_jpage_size);
			return -EINVAL;
		}
		level = 0;
		base_shift = PUD_SHIFT;
	} else if (ps >= PMD_SIZE) {
		level = 1;
		base_shift = PMD_SHIFT;
	} else if (ps > PAGE_SIZE) {
		level = 2;
		base_shift = PAGE_SHIFT;
	} else {
		pr_err("hugepagesz: huge page size %ld too small\n", ps);
		return -EINVAL;
	}

	if (log_ps != base_shift) {
		int shift_val = log_ps - base_shift;
		if (huge_shift[level] != 0) {
			int old_shift = base_shift + huge_shift[level];
			pr_warn("Not enabling %ld MB huge pages; already have size %ld MB\n",
				ps >> 20, (1UL << old_shift) >> 20);
			return -EINVAL;
		}
		if (hv_set_pte_super_shift(level, shift_val) != 0) {
			pr_warn("Not enabling %ld MB huge pages; no hypervisor support\n",
				ps >> 20);
			return -EINVAL;
		}
		printk(KERN_DEBUG "Enabled %ld MB huge pages\n", ps >> 20);
		huge_shift[level] = shift_val;
	}

	hugetlb_add_hstate(log_ps - PAGE_SHIFT);

	return 0;
}

static bool saw_hugepagesz;

static __init int setup_hugepagesz(char *opt)
{
	if (!saw_hugepagesz) {
		saw_hugepagesz = true;
		memset(huge_shift, 0, sizeof(huge_shift));
	}
	return __setup_hugepagesz(memparse(opt, NULL));
}
__setup("hugepagesz=", setup_hugepagesz);

#ifdef ADDITIONAL_HUGE_SIZE
/*
 * Provide an additional huge page size if no "hugepagesz" args are given.
 * In that case, all the cores have properly set up their hv super_shift
 * already, but we need to notify the hugetlb code to enable the
 * new huge page size from the Linux point of view.
 */
static __init int add_default_hugepagesz(void)
{
	if (!saw_hugepagesz) {
		BUILD_BUG_ON(ADDITIONAL_HUGE_SIZE >= PMD_SIZE ||
			     ADDITIONAL_HUGE_SIZE <= PAGE_SIZE);
		BUILD_BUG_ON((PAGE_SIZE << ADDITIONAL_HUGE_SHIFT) !=
			     ADDITIONAL_HUGE_SIZE);
		BUILD_BUG_ON(ADDITIONAL_HUGE_SHIFT & 1);
		hugetlb_add_hstate(ADDITIONAL_HUGE_SHIFT);
	}
	return 0;
}
arch_initcall(add_default_hugepagesz);
#endif

#endif /* CONFIG_HUGETLB_SUPER_PAGES */

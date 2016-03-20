/*
 * SPARC64 Huge TLB page support.
 *
 * Copyright (C) 2002, 2003, 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>

/* Slightly simplified from the non-hugepage variant because by
 * definition we don't have to worry about any page coloring stuff
 */

static unsigned long hugetlb_get_unmapped_area_bottomup(struct file *filp,
							unsigned long addr,
							unsigned long len,
							unsigned long pgoff,
							unsigned long flags)
{
	unsigned long task_size = TASK_SIZE;
	struct vm_unmapped_area_info info;

	if (test_thread_flag(TIF_32BIT))
		task_size = STACK_TOP32;

	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = min(task_size, VA_EXCLUDE_START);
	info.align_mask = PAGE_MASK & ~HPAGE_MASK;
	info.align_offset = 0;
	addr = vm_unmapped_area(&info);

	if ((addr & ~PAGE_MASK) && task_size > VA_EXCLUDE_END) {
		VM_BUG_ON(addr != -ENOMEM);
		info.low_limit = VA_EXCLUDE_END;
		info.high_limit = task_size;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

static unsigned long
hugetlb_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
				  const unsigned long len,
				  const unsigned long pgoff,
				  const unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	struct vm_unmapped_area_info info;

	/* This should only ever run for 32-bit processes.  */
	BUG_ON(!test_thread_flag(TIF_32BIT));

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = mm->mmap_base;
	info.align_mask = PAGE_MASK & ~HPAGE_MASK;
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
		info.high_limit = STACK_TOP32;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long task_size = TASK_SIZE;

	if (test_thread_flag(TIF_32BIT))
		task_size = STACK_TOP32;

	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (len > task_size)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, HPAGE_SIZE);
		vma = find_vma(mm, addr);
		if (task_size - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	if (mm->get_unmapped_area == arch_get_unmapped_area)
		return hugetlb_get_unmapped_area_bottomup(file, addr, len,
				pgoff, flags);
	else
		return hugetlb_get_unmapped_area_topdown(file, addr, len,
				pgoff, flags);
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	/* We must align the address, because our caller will run
	 * set_huge_pte_at() on whatever we return, which writes out
	 * all of the sub-ptes for the hugepage range.  So we have
	 * to give it the first such sub-pte.
	 */
	addr &= HPAGE_MASK;

	pgd = pgd_offset(mm, addr);
	pud = pud_alloc(mm, pgd, addr);
	if (pud) {
		pmd = pmd_alloc(mm, pud, addr);
		if (pmd)
			pte = pte_alloc_map(mm, pmd, addr);
	}
	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	addr &= HPAGE_MASK;

	pgd = pgd_offset(mm, addr);
	if (!pgd_none(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (!pud_none(*pud)) {
			pmd = pmd_offset(pud, addr);
			if (!pmd_none(*pmd))
				pte = pte_offset_map(pmd, addr);
		}
	}
	return pte;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry)
{
	int i;

	if (!pte_present(*ptep) && pte_present(entry))
		mm->context.huge_pte_count++;

	addr &= HPAGE_MASK;
	for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
		set_pte_at(mm, addr, ptep, entry);
		ptep++;
		addr += PAGE_SIZE;
		pte_val(entry) += PAGE_SIZE;
	}
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep)
{
	pte_t entry;
	int i;

	entry = *ptep;
	if (pte_present(entry))
		mm->context.huge_pte_count--;

	addr &= HPAGE_MASK;

	for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
		pte_clear(mm, addr, ptep);
		addr += PAGE_SIZE;
		ptep++;
	}

	return entry;
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}

/*
 * SPARC64 Huge TLB page support.
 *
 * Copyright (C) 2002, 2003, 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
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
	struct hstate *h = hstate_file(filp);
	unsigned long task_size = TASK_SIZE;
	struct vm_unmapped_area_info info;

	if (test_thread_flag(TIF_32BIT))
		task_size = STACK_TOP32;

	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = min(task_size, VA_EXCLUDE_START);
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
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
	struct hstate *h = hstate_file(filp);
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	struct vm_unmapped_area_info info;

	/* This should only ever run for 32-bit processes.  */
	BUG_ON(!test_thread_flag(TIF_32BIT));

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = mm->mmap_base;
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
		info.high_limit = STACK_TOP32;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long task_size = TASK_SIZE;

	if (test_thread_flag(TIF_32BIT))
		task_size = STACK_TOP32;

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (len > task_size)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, huge_page_size(h));
		vma = find_vma(mm, addr);
		if (task_size - len >= addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}
	if (mm->get_unmapped_area == arch_get_unmapped_area)
		return hugetlb_get_unmapped_area_bottomup(file, addr, len,
				pgoff, flags);
	else
		return hugetlb_get_unmapped_area_topdown(file, addr, len,
				pgoff, flags);
}

static pte_t sun4u_hugepage_shift_to_tte(pte_t entry, unsigned int shift)
{
	return entry;
}

static pte_t sun4v_hugepage_shift_to_tte(pte_t entry, unsigned int shift)
{
	unsigned long hugepage_size = _PAGE_SZ4MB_4V;

	pte_val(entry) = pte_val(entry) & ~_PAGE_SZALL_4V;

	switch (shift) {
	case HPAGE_2GB_SHIFT:
		hugepage_size = _PAGE_SZ2GB_4V;
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_256MB_SHIFT:
		hugepage_size = _PAGE_SZ256MB_4V;
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_SHIFT:
		pte_val(entry) |= _PAGE_PMD_HUGE;
		break;
	case HPAGE_64K_SHIFT:
		hugepage_size = _PAGE_SZ64K_4V;
		break;
	default:
		WARN_ONCE(1, "unsupported hugepage shift=%u\n", shift);
	}

	pte_val(entry) = pte_val(entry) | hugepage_size;
	return entry;
}

static pte_t hugepage_shift_to_tte(pte_t entry, unsigned int shift)
{
	if (tlb_type == hypervisor)
		return sun4v_hugepage_shift_to_tte(entry, shift);
	else
		return sun4u_hugepage_shift_to_tte(entry, shift);
}

pte_t arch_make_huge_pte(pte_t entry, struct vm_area_struct *vma,
			 struct page *page, int writeable)
{
	unsigned int shift = huge_page_shift(hstate_vma(vma));

	return hugepage_shift_to_tte(entry, shift);
}

static unsigned int sun4v_huge_tte_to_shift(pte_t entry)
{
	unsigned long tte_szbits = pte_val(entry) & _PAGE_SZALL_4V;
	unsigned int shift;

	switch (tte_szbits) {
	case _PAGE_SZ2GB_4V:
		shift = HPAGE_2GB_SHIFT;
		break;
	case _PAGE_SZ256MB_4V:
		shift = HPAGE_256MB_SHIFT;
		break;
	case _PAGE_SZ4MB_4V:
		shift = REAL_HPAGE_SHIFT;
		break;
	case _PAGE_SZ64K_4V:
		shift = HPAGE_64K_SHIFT;
		break;
	default:
		shift = PAGE_SHIFT;
		break;
	}
	return shift;
}

static unsigned int sun4u_huge_tte_to_shift(pte_t entry)
{
	unsigned long tte_szbits = pte_val(entry) & _PAGE_SZALL_4U;
	unsigned int shift;

	switch (tte_szbits) {
	case _PAGE_SZ256MB_4U:
		shift = HPAGE_256MB_SHIFT;
		break;
	case _PAGE_SZ4MB_4U:
		shift = REAL_HPAGE_SHIFT;
		break;
	case _PAGE_SZ64K_4U:
		shift = HPAGE_64K_SHIFT;
		break;
	default:
		shift = PAGE_SHIFT;
		break;
	}
	return shift;
}

static unsigned int huge_tte_to_shift(pte_t entry)
{
	unsigned long shift;

	if (tlb_type == hypervisor)
		shift = sun4v_huge_tte_to_shift(entry);
	else
		shift = sun4u_huge_tte_to_shift(entry);

	if (shift == PAGE_SHIFT)
		WARN_ONCE(1, "tto_to_shift: invalid hugepage tte=0x%lx\n",
			  pte_val(entry));

	return shift;
}

static unsigned long huge_tte_to_size(pte_t pte)
{
	unsigned long size = 1UL << huge_tte_to_shift(pte);

	if (size == REAL_HPAGE_SIZE)
		size = HPAGE_SIZE;
	return size;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	pud = pud_alloc(mm, pgd, addr);
	if (pud) {
		pmd = pmd_alloc(mm, pud, addr);
		if (!pmd)
			return NULL;

		if (sz >= PMD_SIZE)
			pte = (pte_t *)pmd;
		else
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

	pgd = pgd_offset(mm, addr);
	if (!pgd_none(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (!pud_none(*pud)) {
			pmd = pmd_offset(pud, addr);
			if (!pmd_none(*pmd)) {
				if (is_hugetlb_pmd(*pmd))
					pte = (pte_t *)pmd;
				else
					pte = pte_offset_map(pmd, addr);
			}
		}
	}

	return pte;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry)
{
	unsigned int i, nptes, orig_shift, shift;
	unsigned long size;
	pte_t orig;

	size = huge_tte_to_size(entry);
	shift = size >= HPAGE_SIZE ? PMD_SHIFT : PAGE_SHIFT;
	nptes = size >> shift;

	if (!pte_present(*ptep) && pte_present(entry))
		mm->context.hugetlb_pte_count += nptes;

	addr &= ~(size - 1);
	orig = *ptep;
	orig_shift = pte_none(orig) ? PAGE_SHIFT : huge_tte_to_shift(orig);

	for (i = 0; i < nptes; i++)
		ptep[i] = __pte(pte_val(entry) + (i << shift));

	maybe_tlb_batch_add(mm, addr, ptep, orig, 0, orig_shift);
	/* An HPAGE_SIZE'ed page is composed of two REAL_HPAGE_SIZE'ed pages */
	if (size == HPAGE_SIZE)
		maybe_tlb_batch_add(mm, addr + REAL_HPAGE_SIZE, ptep, orig, 0,
				    orig_shift);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep)
{
	unsigned int i, nptes, hugepage_shift;
	unsigned long size;
	pte_t entry;

	entry = *ptep;
	size = huge_tte_to_size(entry);
	if (size >= HPAGE_SIZE)
		nptes = size >> PMD_SHIFT;
	else
		nptes = size >> PAGE_SHIFT;

	hugepage_shift = pte_none(entry) ? PAGE_SHIFT :
		huge_tte_to_shift(entry);

	if (pte_present(entry))
		mm->context.hugetlb_pte_count -= nptes;

	addr &= ~(size - 1);
	for (i = 0; i < nptes; i++)
		ptep[i] = __pte(0UL);

	maybe_tlb_batch_add(mm, addr, ptep, entry, 0, hugepage_shift);
	/* An HPAGE_SIZE'ed page is composed of two REAL_HPAGE_SIZE'ed pages */
	if (size == HPAGE_SIZE)
		maybe_tlb_batch_add(mm, addr + REAL_HPAGE_SIZE, ptep, entry, 0,
				    hugepage_shift);

	return entry;
}

int pmd_huge(pmd_t pmd)
{
	return !pmd_none(pmd) &&
		(pmd_val(pmd) & (_PAGE_VALID|_PAGE_PMD_HUGE)) != _PAGE_VALID;
}

int pud_huge(pud_t pud)
{
	return 0;
}

static void hugetlb_free_pte_range(struct mmu_gather *tlb, pmd_t *pmd,
			   unsigned long addr)
{
	pgtable_t token = pmd_pgtable(*pmd);

	pmd_clear(pmd);
	pte_free_tlb(tlb, token, addr);
	atomic_long_dec(&tlb->mm->nr_ptes);
}

static void hugetlb_free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd))
			continue;
		if (is_hugetlb_pmd(*pmd))
			pmd_clear(pmd);
		else
			hugetlb_free_pte_range(tlb, pmd, addr);
	} while (pmd++, addr = next, addr != end);

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = pmd_offset(pud, start);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd, start);
	mm_dec_nr_pmds(tlb->mm);
}

static void hugetlb_free_pud_range(struct mmu_gather *tlb, pgd_t *pgd,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		hugetlb_free_pmd_range(tlb, pud, addr, next, floor,
				       ceiling);
	} while (pud++, addr = next, addr != end);

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = pud_offset(pgd, start);
	pgd_clear(pgd);
	pud_free_tlb(tlb, pud, start);
}

void hugetlb_free_pgd_range(struct mmu_gather *tlb,
			    unsigned long addr, unsigned long end,
			    unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;

	addr &= PMD_MASK;
	if (addr < floor) {
		addr += PMD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= PMD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= PMD_SIZE;
	if (addr > end - 1)
		return;

	pgd = pgd_offset(tlb->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		hugetlb_free_pud_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

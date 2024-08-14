// SPDX-License-Identifier: GPL-2.0
/*
 *  IBM System z Huge TLB Page Support for Kernel.
 *
 *    Copyright IBM Corp. 2007,2020
 *    Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#define KMSG_COMPONENT "hugetlb"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <asm/pgalloc.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>
#include <linux/security.h>

/*
 * If the bit selected by single-bit bitmask "a" is set within "x", move
 * it to the position indicated by single-bit bitmask "b".
 */
#define move_set_bit(x, a, b)	(((x) & (a)) >> ilog2(a) << ilog2(b))

static inline unsigned long __pte_to_rste(pte_t pte)
{
	unsigned long rste;

	/*
	 * Convert encoding		  pte bits	pmd / pud bits
	 *				lIR.uswrdy.p	dy..R...I...wr
	 * empty			010.000000.0 -> 00..0...1...00
	 * prot-none, clean, old	111.000000.1 -> 00..1...1...00
	 * prot-none, clean, young	111.000001.1 -> 01..1...1...00
	 * prot-none, dirty, old	111.000010.1 -> 10..1...1...00
	 * prot-none, dirty, young	111.000011.1 -> 11..1...1...00
	 * read-only, clean, old	111.000100.1 -> 00..1...1...01
	 * read-only, clean, young	101.000101.1 -> 01..1...0...01
	 * read-only, dirty, old	111.000110.1 -> 10..1...1...01
	 * read-only, dirty, young	101.000111.1 -> 11..1...0...01
	 * read-write, clean, old	111.001100.1 -> 00..1...1...11
	 * read-write, clean, young	101.001101.1 -> 01..1...0...11
	 * read-write, dirty, old	110.001110.1 -> 10..0...1...11
	 * read-write, dirty, young	100.001111.1 -> 11..0...0...11
	 * HW-bits: R read-only, I invalid
	 * SW-bits: p present, y young, d dirty, r read, w write, s special,
	 *	    u unused, l large
	 */
	if (pte_present(pte)) {
		rste = pte_val(pte) & PAGE_MASK;
		rste |= move_set_bit(pte_val(pte), _PAGE_READ,
				     _SEGMENT_ENTRY_READ);
		rste |= move_set_bit(pte_val(pte), _PAGE_WRITE,
				     _SEGMENT_ENTRY_WRITE);
		rste |= move_set_bit(pte_val(pte), _PAGE_INVALID,
				     _SEGMENT_ENTRY_INVALID);
		rste |= move_set_bit(pte_val(pte), _PAGE_PROTECT,
				     _SEGMENT_ENTRY_PROTECT);
		rste |= move_set_bit(pte_val(pte), _PAGE_DIRTY,
				     _SEGMENT_ENTRY_DIRTY);
		rste |= move_set_bit(pte_val(pte), _PAGE_YOUNG,
				     _SEGMENT_ENTRY_YOUNG);
#ifdef CONFIG_MEM_SOFT_DIRTY
		rste |= move_set_bit(pte_val(pte), _PAGE_SOFT_DIRTY,
				     _SEGMENT_ENTRY_SOFT_DIRTY);
#endif
		rste |= move_set_bit(pte_val(pte), _PAGE_NOEXEC,
				     _SEGMENT_ENTRY_NOEXEC);
	} else
		rste = _SEGMENT_ENTRY_EMPTY;
	return rste;
}

static inline pte_t __rste_to_pte(unsigned long rste)
{
	unsigned long pteval;
	int present;

	if ((rste & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		present = pud_present(__pud(rste));
	else
		present = pmd_present(__pmd(rste));

	/*
	 * Convert encoding		pmd / pud bits	    pte bits
	 *				dy..R...I...wr	  lIR.uswrdy.p
	 * empty			00..0...1...00 -> 010.000000.0
	 * prot-none, clean, old	00..1...1...00 -> 111.000000.1
	 * prot-none, clean, young	01..1...1...00 -> 111.000001.1
	 * prot-none, dirty, old	10..1...1...00 -> 111.000010.1
	 * prot-none, dirty, young	11..1...1...00 -> 111.000011.1
	 * read-only, clean, old	00..1...1...01 -> 111.000100.1
	 * read-only, clean, young	01..1...0...01 -> 101.000101.1
	 * read-only, dirty, old	10..1...1...01 -> 111.000110.1
	 * read-only, dirty, young	11..1...0...01 -> 101.000111.1
	 * read-write, clean, old	00..1...1...11 -> 111.001100.1
	 * read-write, clean, young	01..1...0...11 -> 101.001101.1
	 * read-write, dirty, old	10..0...1...11 -> 110.001110.1
	 * read-write, dirty, young	11..0...0...11 -> 100.001111.1
	 * HW-bits: R read-only, I invalid
	 * SW-bits: p present, y young, d dirty, r read, w write, s special,
	 *	    u unused, l large
	 */
	if (present) {
		pteval = rste & _SEGMENT_ENTRY_ORIGIN_LARGE;
		pteval |= _PAGE_LARGE | _PAGE_PRESENT;
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_READ, _PAGE_READ);
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_WRITE, _PAGE_WRITE);
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_INVALID, _PAGE_INVALID);
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_PROTECT, _PAGE_PROTECT);
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_DIRTY, _PAGE_DIRTY);
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_YOUNG, _PAGE_YOUNG);
#ifdef CONFIG_MEM_SOFT_DIRTY
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_SOFT_DIRTY, _PAGE_SOFT_DIRTY);
#endif
		pteval |= move_set_bit(rste, _SEGMENT_ENTRY_NOEXEC, _PAGE_NOEXEC);
	} else
		pteval = _PAGE_INVALID;
	return __pte(pteval);
}

static void clear_huge_pte_skeys(struct mm_struct *mm, unsigned long rste)
{
	struct folio *folio;
	unsigned long size, paddr;

	if (!mm_uses_skeys(mm) ||
	    rste & _SEGMENT_ENTRY_INVALID)
		return;

	if ((rste & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3) {
		folio = page_folio(pud_page(__pud(rste)));
		size = PUD_SIZE;
		paddr = rste & PUD_MASK;
	} else {
		folio = page_folio(pmd_page(__pmd(rste)));
		size = PMD_SIZE;
		paddr = rste & PMD_MASK;
	}

	if (!test_and_set_bit(PG_arch_1, &folio->flags))
		__storage_key_init_range(paddr, paddr + size);
}

void __set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte)
{
	unsigned long rste;

	rste = __pte_to_rste(pte);
	if (!MACHINE_HAS_NX)
		rste &= ~_SEGMENT_ENTRY_NOEXEC;

	/* Set correct table type for 2G hugepages */
	if ((pte_val(*ptep) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3) {
		if (likely(pte_present(pte)))
			rste |= _REGION3_ENTRY_LARGE;
		rste |= _REGION_ENTRY_TYPE_R3;
	} else if (likely(pte_present(pte)))
		rste |= _SEGMENT_ENTRY_LARGE;

	clear_huge_pte_skeys(mm, rste);
	set_pte(ptep, __pte(rste));
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte, unsigned long sz)
{
	__set_huge_pte_at(mm, addr, ptep, pte);
}

pte_t huge_ptep_get(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	return __rste_to_pte(pte_val(*ptep));
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr, pte_t *ptep)
{
	pte_t pte = huge_ptep_get(mm, addr, ptep);
	pmd_t *pmdp = (pmd_t *) ptep;
	pud_t *pudp = (pud_t *) ptep;

	if ((pte_val(*ptep) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		pudp_xchg_direct(mm, addr, pudp, __pud(_REGION3_ENTRY_EMPTY));
	else
		pmdp_xchg_direct(mm, addr, pmdp, __pmd(_SEGMENT_ENTRY_EMPTY));
	return pte;
}

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp = NULL;

	pgdp = pgd_offset(mm, addr);
	p4dp = p4d_alloc(mm, pgdp, addr);
	if (p4dp) {
		pudp = pud_alloc(mm, p4dp, addr);
		if (pudp) {
			if (sz == PUD_SIZE)
				return (pte_t *) pudp;
			else if (sz == PMD_SIZE)
				pmdp = pmd_alloc(mm, pudp, addr);
		}
	}
	return (pte_t *) pmdp;
}

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp = NULL;

	pgdp = pgd_offset(mm, addr);
	if (pgd_present(*pgdp)) {
		p4dp = p4d_offset(pgdp, addr);
		if (p4d_present(*p4dp)) {
			pudp = pud_offset(p4dp, addr);
			if (pud_present(*pudp)) {
				if (pud_leaf(*pudp))
					return (pte_t *) pudp;
				pmdp = pmd_offset(pudp, addr);
			}
		}
	}
	return (pte_t *) pmdp;
}

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	if (MACHINE_HAS_EDAT1 && size == PMD_SIZE)
		return true;
	else if (MACHINE_HAS_EDAT2 && size == PUD_SIZE)
		return true;
	else
		return false;
}

static unsigned long hugetlb_get_unmapped_area_bottomup(struct file *file,
		unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info = {};

	info.length = len;
	info.low_limit = current->mm->mmap_base;
	info.high_limit = TASK_SIZE;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	return vm_unmapped_area(&info);
}

static unsigned long hugetlb_get_unmapped_area_topdown(struct file *file,
		unsigned long addr0, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info = {};
	unsigned long addr;

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = current->mm->mmap_base;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
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
	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		goto check_asce_limit;
	}

	if (addr) {
		addr = ALIGN(addr, huge_page_size(h));
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			goto check_asce_limit;
	}

	if (!test_bit(MMF_TOPDOWN, &mm->flags))
		addr = hugetlb_get_unmapped_area_bottomup(file, addr, len,
				pgoff, flags);
	else
		addr = hugetlb_get_unmapped_area_topdown(file, addr, len,
				pgoff, flags);
	if (offset_in_page(addr))
		return addr;

check_asce_limit:
	return check_asce_limit(mm, addr, len);
}

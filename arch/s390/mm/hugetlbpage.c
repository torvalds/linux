// SPDX-License-Identifier: GPL-2.0
/*
 *  IBM System z Huge TLB Page Support for Kernel.
 *
 *    Copyright IBM Corp. 2007,2016
 *    Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#define KMSG_COMPONENT "hugetlb"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/mm.h>
#include <linux/hugetlb.h>

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
	int present;
	pte_t pte;

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
		pte_val(pte) = rste & _SEGMENT_ENTRY_ORIGIN_LARGE;
		pte_val(pte) |= _PAGE_LARGE | _PAGE_PRESENT;
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_READ,
					     _PAGE_READ);
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_WRITE,
					     _PAGE_WRITE);
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_INVALID,
					     _PAGE_INVALID);
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_PROTECT,
					     _PAGE_PROTECT);
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_DIRTY,
					     _PAGE_DIRTY);
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_YOUNG,
					     _PAGE_YOUNG);
#ifdef CONFIG_MEM_SOFT_DIRTY
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_SOFT_DIRTY,
					     _PAGE_DIRTY);
#endif
		pte_val(pte) |= move_set_bit(rste, _SEGMENT_ENTRY_NOEXEC,
					     _PAGE_NOEXEC);
	} else
		pte_val(pte) = _PAGE_INVALID;
	return pte;
}

static void clear_huge_pte_skeys(struct mm_struct *mm, unsigned long rste)
{
	struct page *page;
	unsigned long size, paddr;

	if (!mm_uses_skeys(mm) ||
	    rste & _SEGMENT_ENTRY_INVALID)
		return;

	if ((rste & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3) {
		page = pud_page(__pud(rste));
		size = PUD_SIZE;
		paddr = rste & PUD_MASK;
	} else {
		page = pmd_page(__pmd(rste));
		size = PMD_SIZE;
		paddr = rste & PMD_MASK;
	}

	if (!test_and_set_bit(PG_arch_1, &page->flags))
		__storage_key_init_range(paddr, paddr + size - 1);
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte)
{
	unsigned long rste;

	rste = __pte_to_rste(pte);
	if (!MACHINE_HAS_NX)
		rste &= ~_SEGMENT_ENTRY_NOEXEC;

	/* Set correct table type for 2G hugepages */
	if ((pte_val(*ptep) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		rste |= _REGION_ENTRY_TYPE_R3 | _REGION3_ENTRY_LARGE;
	else
		rste |= _SEGMENT_ENTRY_LARGE;
	clear_huge_pte_skeys(mm, rste);
	pte_val(*ptep) = rste;
}

pte_t huge_ptep_get(pte_t *ptep)
{
	return __rste_to_pte(pte_val(*ptep));
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr, pte_t *ptep)
{
	pte_t pte = huge_ptep_get(ptep);
	pmd_t *pmdp = (pmd_t *) ptep;
	pud_t *pudp = (pud_t *) ptep;

	if ((pte_val(*ptep) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		pudp_xchg_direct(mm, addr, pudp, __pud(_REGION3_ENTRY_EMPTY));
	else
		pmdp_xchg_direct(mm, addr, pmdp, __pmd(_SEGMENT_ENTRY_EMPTY));
	return pte;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
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
				if (pud_large(*pudp))
					return (pte_t *) pudp;
				pmdp = pmd_offset(pudp, addr);
			}
		}
	}
	return (pte_t *) pmdp;
}

int pmd_huge(pmd_t pmd)
{
	return pmd_large(pmd);
}

int pud_huge(pud_t pud)
{
	return pud_large(pud);
}

struct page *
follow_huge_pud(struct mm_struct *mm, unsigned long address,
		pud_t *pud, int flags)
{
	if (flags & FOLL_GET)
		return NULL;

	return pud_page(*pud) + ((address & ~PUD_MASK) >> PAGE_SHIFT);
}

static __init int setup_hugepagesz(char *opt)
{
	unsigned long size;
	char *string = opt;

	size = memparse(opt, &opt);
	if (MACHINE_HAS_EDAT1 && size == PMD_SIZE) {
		hugetlb_add_hstate(PMD_SHIFT - PAGE_SHIFT);
	} else if (MACHINE_HAS_EDAT2 && size == PUD_SIZE) {
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	} else {
		hugetlb_bad_size();
		pr_err("hugepagesz= specifies an unsupported page size %s\n",
			string);
		return 0;
	}
	return 1;
}
__setup("hugepagesz=", setup_hugepagesz);

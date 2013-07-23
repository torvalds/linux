/*
 *  IBM System z Huge TLB Page Support for Kernel.
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>

static inline pmd_t __pte_to_pmd(pte_t pte)
{
	int none, prot;
	pmd_t pmd;

	/*
	 * Convert encoding	  pte bits	  pmd bits
	 *			.IR.....wdtp	..R...I.....
	 * empty		.10.....0000 -> ..0...1.....
	 * prot-none, clean	.11.....0001 -> ..1...1.....
	 * prot-none, dirty	.10.....0101 -> ..1...1.....
	 * read-only, clean	.01.....0001 -> ..1...0.....
	 * read-only, dirty	.01.....0101 -> ..1...0.....
	 * read-write, clean	.01.....1001 -> ..0...0.....
	 * read-write, dirty	.00.....1101 -> ..0...0.....
	 * Huge ptes are dirty by definition, a clean pte is made dirty
	 * by the conversion.
	 */
	if (pte_present(pte)) {
		pmd_val(pmd) = pte_val(pte) & PAGE_MASK;
		if (pte_val(pte) & _PAGE_INVALID)
			pmd_val(pmd) |= _SEGMENT_ENTRY_INVALID;
		none = (pte_val(pte) & _PAGE_PRESENT) &&
			(pte_val(pte) & _PAGE_INVALID);
		prot = (pte_val(pte) & _PAGE_PROTECT);
		if (prot || none)
			pmd_val(pmd) |= _SEGMENT_ENTRY_PROTECT;
	} else
		pmd_val(pmd) = _SEGMENT_ENTRY_INVALID;
	return pmd;
}

static inline pte_t __pmd_to_pte(pmd_t pmd)
{
	pte_t pte;

	/*
	 * Convert encoding	  pmd bits	  pte bits
	 *			..R...I.....	.IR.....wdtp
	 * empty		..0...1..... -> .10.....0000
	 * prot-none, young	..1...1..... -> .10.....0101
	 * read-only, young	..1...0..... -> .01.....0101
	 * read-write, young	..0...0..... -> .00.....1101
	 * Huge ptes are dirty by definition
	 */
	if (pmd_present(pmd)) {
		pte_val(pte) = _PAGE_PRESENT | _PAGE_LARGE | _PAGE_DIRTY |
			(pmd_val(pmd) & PAGE_MASK);
		if (pmd_val(pmd) & _SEGMENT_ENTRY_INVALID)
			pte_val(pte) |= _PAGE_INVALID;
		else {
			if (pmd_val(pmd) & _SEGMENT_ENTRY_PROTECT)
				pte_val(pte) |= _PAGE_PROTECT;
			else
				pte_val(pte) |= _PAGE_WRITE;
		}
	} else
		pte_val(pte) = _PAGE_INVALID;
	return pte;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte)
{
	pmd_t pmd;

	pmd = __pte_to_pmd(pte);
	if (!MACHINE_HAS_HPAGE) {
		pmd_val(pmd) &= ~_SEGMENT_ENTRY_ORIGIN;
		pmd_val(pmd) |= pte_page(pte)[1].index;
	} else
		pmd_val(pmd) |= _SEGMENT_ENTRY_LARGE | _SEGMENT_ENTRY_CO;
	*(pmd_t *) ptep = pmd;
}

pte_t huge_ptep_get(pte_t *ptep)
{
	unsigned long origin;
	pmd_t pmd;

	pmd = *(pmd_t *) ptep;
	if (!MACHINE_HAS_HPAGE && pmd_present(pmd)) {
		origin = pmd_val(pmd) & _SEGMENT_ENTRY_ORIGIN;
		pmd_val(pmd) &= ~_SEGMENT_ENTRY_ORIGIN;
		pmd_val(pmd) |= *(unsigned long *) origin;
	}
	return __pmd_to_pte(pmd);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr, pte_t *ptep)
{
	pmd_t *pmdp = (pmd_t *) ptep;
	pte_t pte = huge_ptep_get(ptep);

	if (MACHINE_HAS_IDTE)
		__pmd_idte(addr, pmdp);
	else
		__pmd_csp(pmdp);
	pmd_val(*pmdp) = _SEGMENT_ENTRY_EMPTY;
	return pte;
}

int arch_prepare_hugepage(struct page *page)
{
	unsigned long addr = page_to_phys(page);
	pte_t pte;
	pte_t *ptep;
	int i;

	if (MACHINE_HAS_HPAGE)
		return 0;

	ptep = (pte_t *) pte_alloc_one(&init_mm, addr);
	if (!ptep)
		return -ENOMEM;

	pte_val(pte) = addr;
	for (i = 0; i < PTRS_PER_PTE; i++) {
		set_pte_at(&init_mm, addr + i * PAGE_SIZE, ptep + i, pte);
		pte_val(pte) += PAGE_SIZE;
	}
	page[1].index = (unsigned long) ptep;
	return 0;
}

void arch_release_hugepage(struct page *page)
{
	pte_t *ptep;

	if (MACHINE_HAS_HPAGE)
		return;

	ptep = (pte_t *) page[1].index;
	if (!ptep)
		return;
	clear_table((unsigned long *) ptep, _PAGE_INVALID,
		    PTRS_PER_PTE * sizeof(pte_t));
	page_table_free(&init_mm, (unsigned long *) ptep);
	page[1].index = 0;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp = NULL;

	pgdp = pgd_offset(mm, addr);
	pudp = pud_alloc(mm, pgdp, addr);
	if (pudp)
		pmdp = pmd_alloc(mm, pudp, addr);
	return (pte_t *) pmdp;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp = NULL;

	pgdp = pgd_offset(mm, addr);
	if (pgd_present(*pgdp)) {
		pudp = pud_offset(pgdp, addr);
		if (pud_present(*pudp))
			pmdp = pmd_offset(pudp, addr);
	}
	return (pte_t *) pmdp;
}

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}

struct page *follow_huge_addr(struct mm_struct *mm, unsigned long address,
			      int write)
{
	return ERR_PTR(-EINVAL);
}

int pmd_huge(pmd_t pmd)
{
	if (!MACHINE_HAS_HPAGE)
		return 0;

	return !!(pmd_val(pmd) & _SEGMENT_ENTRY_LARGE);
}

int pud_huge(pud_t pud)
{
	return 0;
}

struct page *follow_huge_pmd(struct mm_struct *mm, unsigned long address,
			     pmd_t *pmdp, int write)
{
	struct page *page;

	if (!MACHINE_HAS_HPAGE)
		return NULL;

	page = pmd_page(*pmdp);
	if (page)
		page += ((address & ~HPAGE_MASK) >> PAGE_SHIFT);
	return page;
}

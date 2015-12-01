#ifndef _ASM_POWERPC_BOOK3S_64_HASH_64K_H
#define _ASM_POWERPC_BOOK3S_64_HASH_64K_H

#include <asm-generic/pgtable-nopud.h>

#define PTE_INDEX_SIZE  8
#define PMD_INDEX_SIZE  10
#define PUD_INDEX_SIZE	0
#define PGD_INDEX_SIZE  12

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* With 4k base page size, hugepage PTEs go at the PMD level */
#define MIN_HUGEPTE_SHIFT	PAGE_SHIFT

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Bits to mask out from a PMD to get to the PTE page */
/* PMDs point to PTE table fragments which are 4K aligned.  */
#define PMD_MASKED_BITS		0xfff
/* Bits to mask out from a PGD/PUD to get to the PMD page */
#define PUD_MASKED_BITS		0x1ff

#define _PAGE_COMBO	0x00020000 /* this is a combo 4k page */
#define _PAGE_4K_PFN	0x00040000 /* PFN is for a single 4k page */
/*
 * Used to track subpage group valid if _PAGE_COMBO is set
 * This overloads _PAGE_F_GIX and _PAGE_F_SECOND
 */
#define _PAGE_COMBO_VALID	(_PAGE_F_GIX | _PAGE_F_SECOND)

/* PTE flags to conserve for HPTE identification */
#define _PAGE_HPTEFLAGS (_PAGE_BUSY | _PAGE_F_SECOND | \
			 _PAGE_F_GIX | _PAGE_HASHPTE | _PAGE_COMBO)

/* Shift to put page number into pte.
 *
 * That gives us a max RPN of 34 bits, which means a max of 50 bits
 * of addressable physical space, or 46 bits for the special 4k PFNs.
 */
#define PTE_RPN_SHIFT	(30)

#ifndef __ASSEMBLY__

/*
 * With 64K pages on hash table, we have a special PTE format that
 * uses a second "half" of the page table to encode sub-page information
 * in order to deal with 64K made of 4K HW pages. Thus we override the
 * generic accessors and iterators here
 */
#define __real_pte __real_pte
static inline real_pte_t __real_pte(pte_t pte, pte_t *ptep)
{
	real_pte_t rpte;
	unsigned long *hidxp;

	rpte.pte = pte;
	rpte.hidx = 0;
	if (pte_val(pte) & _PAGE_COMBO) {
		/*
		 * Make sure we order the hidx load against the _PAGE_COMBO
		 * check. The store side ordering is done in __hash_page_4K
		 */
		smp_rmb();
		hidxp = (unsigned long *)(ptep + PTRS_PER_PTE);
		rpte.hidx = *hidxp;
	}
	return rpte;
}

static inline unsigned long __rpte_to_hidx(real_pte_t rpte, unsigned long index)
{
	if ((pte_val(rpte.pte) & _PAGE_COMBO))
		return (rpte.hidx >> (index<<2)) & 0xf;
	return (pte_val(rpte.pte) >> 12) & 0xf;
}

#define __rpte_to_pte(r)	((r).pte)
extern bool __rpte_sub_valid(real_pte_t rpte, unsigned long index);
/*
 * Trick: we set __end to va + 64k, which happens works for
 * a 16M page as well as we want only one iteration
 */
#define pte_iterate_hashed_subpages(rpte, psize, vpn, index, shift)	\
	do {								\
		unsigned long __end = vpn + (1UL << (PAGE_SHIFT - VPN_SHIFT));	\
		unsigned __split = (psize == MMU_PAGE_4K ||		\
				    psize == MMU_PAGE_64K_AP);		\
		shift = mmu_psize_defs[psize].shift;			\
		for (index = 0; vpn < __end; index++,			\
			     vpn += (1L << (shift - VPN_SHIFT))) {	\
			if (!__split || __rpte_sub_valid(rpte, index))	\
				do {

#define pte_iterate_hashed_end() } while(0); } } while(0)

#define pte_pagesize_index(mm, addr, pte)	\
	(((pte) & _PAGE_COMBO)? MMU_PAGE_4K: MMU_PAGE_64K)

#define remap_4k_pfn(vma, addr, pfn, prot)				\
	(WARN_ON(((pfn) >= (1UL << (64 - PTE_RPN_SHIFT)))) ? -EINVAL :	\
		remap_pfn_range((vma), (addr), (pfn), PAGE_SIZE,	\
			__pgprot(pgprot_val((prot)) | _PAGE_4K_PFN)))

#define PTE_TABLE_SIZE	(sizeof(real_pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#define pgd_pte(pgd)	(pud_pte(((pud_t){ pgd })))
#define pte_pgd(pte)	((pgd_t)pte_pud(pte))

#endif	/* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_64_HASH_64K_H */

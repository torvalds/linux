#ifndef _ASM_POWERPC_PGTABLE_64K_H
#define _ASM_POWERPC_PGTABLE_64K_H

#include <asm-generic/pgtable-nopud.h>


#define PTE_INDEX_SIZE  12
#define PMD_INDEX_SIZE  12
#define PUD_INDEX_SIZE	0
#define PGD_INDEX_SIZE  4

#ifndef __ASSEMBLY__
#define PTE_TABLE_SIZE	(sizeof(real_pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

#ifdef CONFIG_PPC_SUBPAGE_PROT
/*
 * For the sub-page protection option, we extend the PGD with one of
 * these.  Basically we have a 3-level tree, with the top level being
 * the protptrs array.  To optimize speed and memory consumption when
 * only addresses < 4GB are being protected, pointers to the first
 * four pages of sub-page protection words are stored in the low_prot
 * array.
 * Each page of sub-page protection words protects 1GB (4 bytes
 * protects 64k).  For the 3-level tree, each page of pointers then
 * protects 8TB.
 */
struct subpage_prot_table {
	unsigned long maxaddr;	/* only addresses < this are protected */
	unsigned int **protptrs[2];
	unsigned int *low_prot[4];
};

#undef PGD_TABLE_SIZE
#define PGD_TABLE_SIZE		((sizeof(pgd_t) << PGD_INDEX_SIZE) + \
				 sizeof(struct subpage_prot_table))

#define SBP_L1_BITS		(PAGE_SHIFT - 2)
#define SBP_L2_BITS		(PAGE_SHIFT - 3)
#define SBP_L1_COUNT		(1 << SBP_L1_BITS)
#define SBP_L2_COUNT		(1 << SBP_L2_BITS)
#define SBP_L2_SHIFT		(PAGE_SHIFT + SBP_L1_BITS)
#define SBP_L3_SHIFT		(SBP_L2_SHIFT + SBP_L2_BITS)

extern void subpage_prot_free(pgd_t *pgd);

static inline struct subpage_prot_table *pgd_subpage_prot(pgd_t *pgd)
{
	return (struct subpage_prot_table *)(pgd + PTRS_PER_PGD);
}
#endif /* CONFIG_PPC_SUBPAGE_PROT */
#endif	/* __ASSEMBLY__ */

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

/* Additional PTE bits (don't change without checking asm in hash_low.S) */
#define __HAVE_ARCH_PTE_SPECIAL
#define _PAGE_SPECIAL	0x00000400 /* software: special page */
#define _PAGE_HPTE_SUB	0x0ffff000 /* combo only: sub pages HPTE bits */
#define _PAGE_HPTE_SUB0	0x08000000 /* combo only: first sub page */
#define _PAGE_COMBO	0x10000000 /* this is a combo 4k page */
#define _PAGE_4K_PFN	0x20000000 /* PFN is for a single 4k page */

/* For 64K page, we don't have a separate _PAGE_HASHPTE bit. Instead,
 * we set that to be the whole sub-bits mask. The C code will only
 * test this, so a multi-bit mask will work. For combo pages, this
 * is equivalent as effectively, the old _PAGE_HASHPTE was an OR of
 * all the sub bits. For real 64k pages, we now have the assembly set
 * _PAGE_HPTE_SUB0 in addition to setting the HIDX bits which overlap
 * that mask. This is fine as long as the HIDX bits are never set on
 * a PTE that isn't hashed, which is the case today.
 *
 * A little nit is for the huge page C code, which does the hashing
 * in C, we need to provide which bit to use.
 */
#define _PAGE_HASHPTE	_PAGE_HPTE_SUB

/* Note the full page bits must be in the same location as for normal
 * 4k pages as the same asssembly will be used to insert 64K pages
 * wether the kernel has CONFIG_PPC_64K_PAGES or not
 */
#define _PAGE_F_SECOND  0x00008000 /* full page: hidx bits */
#define _PAGE_F_GIX     0x00007000 /* full page: hidx bits */

/* PTE flags to conserve for HPTE identification */
#define _PAGE_HPTEFLAGS (_PAGE_BUSY | _PAGE_HASHPTE | _PAGE_COMBO)

/* Shift to put page number into pte.
 *
 * That gives us a max RPN of 34 bits, which means a max of 50 bits
 * of addressable physical space, or 46 bits for the special 4k PFNs.
 */
#define PTE_RPN_SHIFT	(30)
#define PTE_RPN_MAX	(1UL << (64 - PTE_RPN_SHIFT))
#define PTE_RPN_MASK	(~((1UL<<PTE_RPN_SHIFT)-1))

/* _PAGE_CHG_MASK masks of bits that are to be preserved accross
 * pgprot changes
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
                         _PAGE_ACCESSED | _PAGE_SPECIAL)

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0x1ff
/* Bits to mask out from a PGD/PUD to get to the PMD page */
#define PUD_MASKED_BITS		0x1ff

/* Manipulate "rpte" values */
#define __real_pte(e,p) 	((real_pte_t) { \
	(e), pte_val(*((p) + PTRS_PER_PTE)) })
#define __rpte_to_hidx(r,index)	((pte_val((r).pte) & _PAGE_COMBO) ? \
        (((r).hidx >> ((index)<<2)) & 0xf) : ((pte_val((r).pte) >> 12) & 0xf))
#define __rpte_to_pte(r)	((r).pte)
#define __rpte_sub_valid(rpte, index) \
	(pte_val(rpte.pte) & (_PAGE_HPTE_SUB0 >> (index)))


/* Trick: we set __end to va + 64k, which happens works for
 * a 16M page as well as we want only one iteration
 */
#define pte_iterate_hashed_subpages(rpte, psize, va, index, shift)	    \
        do {                                                                \
                unsigned long __end = va + PAGE_SIZE;                       \
                unsigned __split = (psize == MMU_PAGE_4K ||                 \
				    psize == MMU_PAGE_64K_AP);              \
                shift = mmu_psize_defs[psize].shift;                        \
		for (index = 0; va < __end; index++, va += (1L << shift)) { \
		        if (!__split || __rpte_sub_valid(rpte, index)) do { \

#define pte_iterate_hashed_end() } while(0); } } while(0)

#define pte_pagesize_index(mm, addr, pte)	\
	(((pte) & _PAGE_COMBO)? MMU_PAGE_4K: MMU_PAGE_64K)

#define remap_4k_pfn(vma, addr, pfn, prot)				\
	remap_pfn_range((vma), (addr), (pfn), PAGE_SIZE,		\
			__pgprot(pgprot_val((prot)) | _PAGE_4K_PFN))

#endif /* _ASM_POWERPC_PGTABLE_64K_H */

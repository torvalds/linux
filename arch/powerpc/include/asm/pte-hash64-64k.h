/* To be include by pgtable-hash64.h only */

/* Additional PTE bits (don't change without checking asm in hash_low.S) */
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

#ifndef __ASSEMBLY__

/*
 * With 64K pages on hash table, we have a special PTE format that
 * uses a second "half" of the page table to encode sub-page information
 * in order to deal with 64K made of 4K HW pages. Thus we override the
 * generic accessors and iterators here
 */
#define __real_pte(e,p) 	((real_pte_t) { \
			(e), ((e) & _PAGE_COMBO) ? \
				(pte_val(*((p) + PTRS_PER_PTE))) : 0 })
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

#endif	/* __ASSEMBLY__ */

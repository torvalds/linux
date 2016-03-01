#ifndef _ASM_POWERPC_BOOK3S_64_HASH_64K_H
#define _ASM_POWERPC_BOOK3S_64_HASH_64K_H

#define PTE_INDEX_SIZE  8
#define PMD_INDEX_SIZE  5
#define PUD_INDEX_SIZE	5
#define PGD_INDEX_SIZE  12

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PUD	(1 << PUD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* With 4k base page size, hugepage PTEs go at the PMD level */
#define MIN_HUGEPTE_SHIFT	PAGE_SHIFT

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PUD_SHIFT determines what a third-level page table entry can map */
#define PUD_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */
#define PGDIR_SHIFT	(PUD_SHIFT + PUD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define _PAGE_COMBO	0x00001000 /* this is a combo 4k page */
#define _PAGE_4K_PFN	0x00002000 /* PFN is for a single 4k page */
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
 * That gives us a max RPN of 41 bits, which means a max of 57 bits
 * of addressable physical space, or 53 bits for the special 4k PFNs.
 */
#define PTE_RPN_SHIFT	(16)
#define PTE_RPN_SIZE	(41)

/*
 * we support 16 fragments per PTE page of 64K size.
 */
#define PTE_FRAG_NR	16
/*
 * We use a 2K PTE page fragment and another 2K for storing
 * real_pte_t hash index
 */
#define PTE_FRAG_SIZE_SHIFT  12
#define PTE_FRAG_SIZE (1UL << PTE_FRAG_SIZE_SHIFT)

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0xc0000000000000ffUL
/* Bits to mask out from a PUD to get to the PMD page */
#define PUD_MASKED_BITS		0xc0000000000000ffUL
/* Bits to mask out from a PGD to get to the PUD page */
#define PGD_MASKED_BITS		0xc0000000000000ffUL

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
	return (pte_val(rpte.pte) >> _PAGE_F_GIX_SHIFT) & 0xf;
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
	(WARN_ON(((pfn) >= (1UL << PTE_RPN_SIZE))) ? -EINVAL :	\
		remap_pfn_range((vma), (addr), (pfn), PAGE_SIZE,	\
			__pgprot(pgprot_val((prot)) | _PAGE_4K_PFN)))

#define PTE_TABLE_SIZE	PTE_FRAG_SIZE
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define PMD_TABLE_SIZE	((sizeof(pmd_t) << PMD_INDEX_SIZE) + (sizeof(unsigned long) << PMD_INDEX_SIZE))
#else
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#endif
#define PUD_TABLE_SIZE	(sizeof(pud_t) << PUD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#ifdef CONFIG_HUGETLB_PAGE
/*
 * We have PGD_INDEX_SIZ = 12 and PTE_INDEX_SIZE = 8, so that we can have
 * 16GB hugepage pte in PGD and 16MB hugepage pte at PMD;
 *
 * Defined in such a way that we can optimize away code block at build time
 * if CONFIG_HUGETLB_PAGE=n.
 */
static inline int pmd_huge(pmd_t pmd)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pmd_val(pmd) & _PAGE_PTE);
}

static inline int pud_huge(pud_t pud)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pud_val(pud) & _PAGE_PTE);
}

static inline int pgd_huge(pgd_t pgd)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pgd_val(pgd) & _PAGE_PTE);
}
#define pgd_huge pgd_huge

#ifdef CONFIG_DEBUG_VM
extern int hugepd_ok(hugepd_t hpd);
#define is_hugepd(hpd)               (hugepd_ok(hpd))
#else
/*
 * With 64k page size, we have hugepage ptes in the pgd and pmd entries. We don't
 * need to setup hugepage directory for them. Our pte and page directory format
 * enable us to have this enabled.
 */
static inline int hugepd_ok(hugepd_t hpd)
{
	return 0;
}
#define is_hugepd(pdep)			0
#endif /* CONFIG_DEBUG_VM */

#endif /* CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern unsigned long pmd_hugepage_update(struct mm_struct *mm,
					 unsigned long addr,
					 pmd_t *pmdp,
					 unsigned long clr,
					 unsigned long set);
static inline char *get_hpte_slot_array(pmd_t *pmdp)
{
	/*
	 * The hpte hindex is stored in the pgtable whose address is in the
	 * second half of the PMD
	 *
	 * Order this load with the test for pmd_trans_huge in the caller
	 */
	smp_rmb();
	return *(char **)(pmdp + PTRS_PER_PMD);


}
/*
 * The linux hugepage PMD now include the pmd entries followed by the address
 * to the stashed pgtable_t. The stashed pgtable_t contains the hpte bits.
 * [ 000 | 1 bit secondary | 3 bit hidx | 1 bit valid]. We use one byte per
 * each HPTE entry. With 16MB hugepage and 64K HPTE we need 256 entries and
 * with 4K HPTE we need 4096 entries. Both will fit in a 4K pgtable_t.
 *
 * The top three bits are intentionally left as zero. This memory location
 * are also used as normal page PTE pointers. So if we have any pointers
 * left around while we collapse a hugepage, we need to make sure
 * _PAGE_PRESENT bit of that is zero when we look at them
 */
static inline unsigned int hpte_valid(unsigned char *hpte_slot_array, int index)
{
	return hpte_slot_array[index] & 0x1;
}

static inline unsigned int hpte_hash_index(unsigned char *hpte_slot_array,
					   int index)
{
	return hpte_slot_array[index] >> 1;
}

static inline void mark_hpte_slot_valid(unsigned char *hpte_slot_array,
					unsigned int index, unsigned int hidx)
{
	hpte_slot_array[index] = (hidx << 1) | 0x1;
}

/*
 *
 * For core kernel code by design pmd_trans_huge is never run on any hugetlbfs
 * page. The hugetlbfs page table walking and mangling paths are totally
 * separated form the core VM paths and they're differentiated by
 *  VM_HUGETLB being set on vm_flags well before any pmd_trans_huge could run.
 *
 * pmd_trans_huge() is defined as false at build time if
 * CONFIG_TRANSPARENT_HUGEPAGE=n to optimize away code blocks at build
 * time in such case.
 *
 * For ppc64 we need to differntiate from explicit hugepages from THP, because
 * for THP we also track the subpage details at the pmd level. We don't do
 * that for explicit huge pages.
 *
 */
static inline int pmd_trans_huge(pmd_t pmd)
{
	return !!((pmd_val(pmd) & (_PAGE_PTE | _PAGE_THP_HUGE)) ==
		  (_PAGE_PTE | _PAGE_THP_HUGE));
}

static inline int pmd_large(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_PTE);
}

static inline pmd_t pmd_mknotpresent(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) & ~_PAGE_PRESENT);
}

#define __HAVE_ARCH_PMD_SAME
static inline int pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	return (((pmd_val(pmd_a) ^ pmd_val(pmd_b)) & ~_PAGE_HPTEFLAGS) == 0);
}

static inline int __pmdp_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pmd_t *pmdp)
{
	unsigned long old;

	if ((pmd_val(*pmdp) & (_PAGE_ACCESSED | _PAGE_HASHPTE)) == 0)
		return 0;
	old = pmd_hugepage_update(mm, addr, pmdp, _PAGE_ACCESSED, 0);
	return ((old & _PAGE_ACCESSED) != 0);
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pmd_t *pmdp)
{

	if ((pmd_val(*pmdp) & _PAGE_RW) == 0)
		return;

	pmd_hugepage_update(mm, addr, pmdp, _PAGE_RW, 0);
}

#endif /*  CONFIG_TRANSPARENT_HUGEPAGE */
#endif	/* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_64_HASH_64K_H */

#ifndef _ASM_POWERPC_BOOK3S_64_HASH_64K_H
#define _ASM_POWERPC_BOOK3S_64_HASH_64K_H

#define H_PTE_INDEX_SIZE  8
#define H_PMD_INDEX_SIZE  5
#define H_PUD_INDEX_SIZE  5
#define H_PGD_INDEX_SIZE  12

#define H_PAGE_COMBO	0x00001000 /* this is a combo 4k page */
#define H_PAGE_4K_PFN	0x00002000 /* PFN is for a single 4k page */
/*
 * We need to differentiate between explicit huge page and THP huge
 * page, since THP huge page also need to track real subpage details
 */
#define H_PAGE_THP_HUGE  H_PAGE_4K_PFN

/*
 * Used to track subpage group valid if H_PAGE_COMBO is set
 * This overloads H_PAGE_F_GIX and H_PAGE_F_SECOND
 */
#define H_PAGE_COMBO_VALID	(H_PAGE_F_GIX | H_PAGE_F_SECOND)

/* PTE flags to conserve for HPTE identification */
#define _PAGE_HPTEFLAGS (H_PAGE_BUSY | H_PAGE_F_SECOND | \
			 H_PAGE_F_GIX | H_PAGE_HASHPTE | H_PAGE_COMBO)
/*
 * we support 16 fragments per PTE page of 64K size.
 */
#define H_PTE_FRAG_NR	16
/*
 * We use a 2K PTE page fragment and another 2K for storing
 * real_pte_t hash index
 */
#define H_PTE_FRAG_SIZE_SHIFT  12
#define PTE_FRAG_SIZE (1UL << PTE_FRAG_SIZE_SHIFT)

#ifndef __ASSEMBLY__
#include <asm/errno.h>

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
	if (pte_val(pte) & H_PAGE_COMBO) {
		/*
		 * Make sure we order the hidx load against the H_PAGE_COMBO
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
	if ((pte_val(rpte.pte) & H_PAGE_COMBO))
		return (rpte.hidx >> (index<<2)) & 0xf;
	return (pte_val(rpte.pte) >> H_PAGE_F_GIX_SHIFT) & 0xf;
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
	(((pte) & H_PAGE_COMBO)? MMU_PAGE_4K: MMU_PAGE_64K)

extern int remap_pfn_range(struct vm_area_struct *, unsigned long addr,
			   unsigned long pfn, unsigned long size, pgprot_t);
static inline int hash__remap_4k_pfn(struct vm_area_struct *vma, unsigned long addr,
				 unsigned long pfn, pgprot_t prot)
{
	if (pfn > (PTE_RPN_MASK >> PAGE_SHIFT)) {
		WARN(1, "remap_4k_pfn called with wrong pfn value\n");
		return -EINVAL;
	}
	return remap_pfn_range(vma, addr, pfn, PAGE_SIZE,
			       __pgprot(pgprot_val(prot) | H_PAGE_4K_PFN));
}

#define H_PTE_TABLE_SIZE	PTE_FRAG_SIZE
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define H_PMD_TABLE_SIZE	((sizeof(pmd_t) << PMD_INDEX_SIZE) + \
				 (sizeof(unsigned long) << PMD_INDEX_SIZE))
#else
#define H_PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#endif
#define H_PUD_TABLE_SIZE	(sizeof(pud_t) << PUD_INDEX_SIZE)
#define H_PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
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
static inline int hash__pmd_trans_huge(pmd_t pmd)
{
	return !!((pmd_val(pmd) & (_PAGE_PTE | H_PAGE_THP_HUGE)) ==
		  (_PAGE_PTE | H_PAGE_THP_HUGE));
}

static inline int hash__pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	return (((pmd_raw(pmd_a) ^ pmd_raw(pmd_b)) & ~cpu_to_be64(_PAGE_HPTEFLAGS)) == 0);
}

static inline pmd_t hash__pmd_mkhuge(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | (_PAGE_PTE | H_PAGE_THP_HUGE));
}

extern unsigned long hash__pmd_hugepage_update(struct mm_struct *mm,
					   unsigned long addr, pmd_t *pmdp,
					   unsigned long clr, unsigned long set);
extern pmd_t hash__pmdp_collapse_flush(struct vm_area_struct *vma,
				   unsigned long address, pmd_t *pmdp);
extern void hash__pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
					 pgtable_t pgtable);
extern pgtable_t hash__pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp);
extern void hash__pmdp_huge_split_prepare(struct vm_area_struct *vma,
				      unsigned long address, pmd_t *pmdp);
extern pmd_t hash__pmdp_huge_get_and_clear(struct mm_struct *mm,
				       unsigned long addr, pmd_t *pmdp);
extern int hash__has_transparent_hugepage(void);
#endif /*  CONFIG_TRANSPARENT_HUGEPAGE */
#endif	/* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_64_HASH_64K_H */

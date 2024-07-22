/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_32_PTE_8xx_H
#define _ASM_POWERPC_NOHASH_32_PTE_8xx_H
#ifdef __KERNEL__

/*
 * The PowerPC MPC8xx uses a TLB with hardware assisted, software tablewalk.
 * We also use the two level tables, but we can put the real bits in them
 * needed for the TLB and tablewalk.  These definitions require Mx_CTR.PPM = 0,
 * Mx_CTR.PPCS = 0, and MD_CTR.TWAM = 1.  The level 2 descriptor has
 * additional page protection (when Mx_CTR.PPCS = 1) that allows TLB hit
 * based upon user/super access.  The TLB does not have accessed nor write
 * protect.  We assume that if the TLB get loaded with an entry it is
 * accessed, and overload the changed bit for write protect.  We use
 * two bits in the software pte that are supposed to be set to zero in
 * the TLB entry (24 and 25) for these indicators.  Although the level 1
 * descriptor contains the guarded and writethrough/copyback bits, we can
 * set these at the page level since they get copied from the Mx_TWC
 * register when the TLB entry is loaded.  We will use bit 27 for guard, since
 * that is where it exists in the MD_TWC, and bit 26 for writethrough.
 * These will get masked from the level 2 descriptor at TLB load time, and
 * copied to the MD_TWC before it gets loaded.
 * Large page sizes added.  We currently support two sizes, 4K and 8M.
 * This also allows a TLB hander optimization because we can directly
 * load the PMD into MD_TWC.  The 8M pages are only used for kernel
 * mapping of well known areas.  The PMD (PGD) entries contain control
 * flags in addition to the address, so care must be taken that the
 * software no longer assumes these are only pointers.
 */

/* Definitions for 8xx embedded chips. */
#define _PAGE_PRESENT	0x0001	/* V: Page is valid */
#define _PAGE_NO_CACHE	0x0002	/* CI: cache inhibit */
#define _PAGE_SH	0x0004	/* SH: No ASID (context) compare */
#define _PAGE_SPS	0x0008	/* SPS: Small Page Size (1 if 16k, 512k or 8M)*/
#define _PAGE_DIRTY	0x0100	/* C: page changed */

/* These 4 software bits must be masked out when the L2 entry is loaded
 * into the TLB.
 */
#define _PAGE_GUARDED	0x0010	/* Copied to L1 G entry in DTLB */
#define _PAGE_ACCESSED	0x0020	/* Copied to L1 APG 1 entry in I/DTLB */
#define _PAGE_EXEC	0x0040	/* Copied to PP (bit 21) in ITLB */
#define _PAGE_SPECIAL	0x0080	/* SW entry */

#define _PAGE_NA	0x0200	/* Supervisor NA, User no access */
#define _PAGE_RO	0x0600	/* Supervisor RO, User no access */

#define _PAGE_HUGE	0x0800	/* Copied to L1 PS bit 29 */

#define _PAGE_NAX	(_PAGE_NA | _PAGE_EXEC)
#define _PAGE_ROX	(_PAGE_RO | _PAGE_EXEC)
#define _PAGE_RW	0
#define _PAGE_RWX	_PAGE_EXEC

/* cache related flags non existing on 8xx */
#define _PAGE_COHERENT	0
#define _PAGE_WRITETHRU	0

#define _PAGE_KERNEL_RO		(_PAGE_SH | _PAGE_RO)
#define _PAGE_KERNEL_ROX	(_PAGE_SH | _PAGE_RO | _PAGE_EXEC)
#define _PAGE_KERNEL_RW		(_PAGE_SH | _PAGE_DIRTY)
#define _PAGE_KERNEL_RWX	(_PAGE_SH | _PAGE_DIRTY | _PAGE_EXEC)

#define _PMD_PRESENT	0x0001
#define _PMD_PRESENT_MASK	_PMD_PRESENT
#define _PMD_BAD	0x0f90
#define _PMD_PAGE_MASK	0x000c
#define _PMD_PAGE_8M	0x000c
#define _PMD_PAGE_512K	0x0004
#define _PMD_ACCESSED	0x0020	/* APG 1 */
#define _PMD_USER	0x0040	/* APG 2 */

#define _PTE_NONE_MASK	0

#ifdef CONFIG_PPC_16K_PAGES
#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_SPS)
#else
#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED)
#endif

#define _PAGE_BASE	(_PAGE_BASE_NC)

#include <asm/pgtable-masks.h>

#ifndef __ASSEMBLY__
static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_RO);
}

#define pte_wrprotect pte_wrprotect

static inline int pte_read(pte_t pte)
{
	return (pte_val(pte) & _PAGE_RO) != _PAGE_NA;
}

#define pte_read pte_read

static inline int pte_write(pte_t pte)
{
	return !(pte_val(pte) & _PAGE_RO);
}

#define pte_write pte_write

static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_RO);
}

#define pte_mkwrite_novma pte_mkwrite_novma

static inline pte_t pte_mkhuge(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPS | _PAGE_HUGE);
}

#define pte_mkhuge pte_mkhuge

static inline pte_basic_t pte_update(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
				     unsigned long clr, unsigned long set, int huge);

static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_update(mm, addr, ptep, 0, _PAGE_RO, 0);
}
#define ptep_set_wrprotect ptep_set_wrprotect

static inline void __ptep_set_access_flags(struct vm_area_struct *vma, pte_t *ptep,
					   pte_t entry, unsigned long address, int psize)
{
	unsigned long set = pte_val(entry) & (_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_EXEC);
	unsigned long clr = ~pte_val(entry) & _PAGE_RO;
	int huge = psize > mmu_virtual_psize ? 1 : 0;

	pte_update(vma->vm_mm, address, ptep, clr, set, huge);

	flush_tlb_page(vma, address);
}
#define __ptep_set_access_flags __ptep_set_access_flags

static inline unsigned long __pte_leaf_size(pmd_t pmd, pte_t pte)
{
	pte_basic_t val = pte_val(pte);

	if (pmd_val(pmd) & _PMD_PAGE_8M)
		return SZ_8M;
	if (val & _PAGE_HUGE)
		return SZ_512K;
	if (val & _PAGE_SPS)
		return SZ_16K;
	return SZ_4K;
}

#define __pte_leaf_size __pte_leaf_size

/*
 * On the 8xx, the page tables are a bit special. For 16k pages, we have
 * 4 identical entries. For 512k pages, we have 128 entries as if it was
 * 4k pages, but they are flagged as 512k pages for the hardware.
 * For 8M pages, we have 1024 entries as if it was 4M pages (PMD_SIZE)
 * but they are flagged as 8M pages for the hardware.
 * For 4k pages, we have a single entry in the table.
 */
static pmd_t *pmd_off(struct mm_struct *mm, unsigned long addr);
static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address);

static inline bool ptep_is_8m_pmdp(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	return (pmd_t *)ptep == pmd_off(mm, ALIGN_DOWN(addr, SZ_8M));
}

static inline int number_of_cells_per_pte(pmd_t *pmd, pte_basic_t val, int huge)
{
	if (!huge)
		return PAGE_SIZE / SZ_4K;
	else if ((pmd_val(*pmd) & _PMD_PAGE_MASK) == _PMD_PAGE_8M)
		return SZ_4M / SZ_4K;
	else if (IS_ENABLED(CONFIG_PPC_4K_PAGES) && !(val & _PAGE_HUGE))
		return SZ_16K / SZ_4K;
	else
		return SZ_512K / SZ_4K;
}

static inline pte_basic_t __pte_update(struct mm_struct *mm, unsigned long addr, pte_t *p,
				       unsigned long clr, unsigned long set, int huge)
{
	pte_basic_t *entry = (pte_basic_t *)p;
	pte_basic_t old = pte_val(*p);
	pte_basic_t new = (old & ~(pte_basic_t)clr) | set;
	int num, i;
	pmd_t *pmd = pmd_off(mm, addr);

	num = number_of_cells_per_pte(pmd, new, huge);

	for (i = 0; i < num; i += PAGE_SIZE / SZ_4K, new += PAGE_SIZE) {
		*entry++ = new;
		if (IS_ENABLED(CONFIG_PPC_16K_PAGES)) {
			*entry++ = new;
			*entry++ = new;
			*entry++ = new;
		}
	}

	return old;
}

static inline pte_basic_t pte_update(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
				     unsigned long clr, unsigned long set, int huge)
{
	pte_basic_t old;

	if (huge && ptep_is_8m_pmdp(mm, addr, ptep)) {
		pmd_t *pmdp = (pmd_t *)ptep;

		old = __pte_update(mm, addr, pte_offset_kernel(pmdp, 0), clr, set, huge);
		__pte_update(mm, addr, pte_offset_kernel(pmdp + 1, 0), clr, set, huge);
	} else {
		old = __pte_update(mm, addr, ptep, clr, set, huge);
	}
	return old;
}
#define pte_update pte_update

#ifdef CONFIG_PPC_16K_PAGES
#define ptep_get ptep_get
static inline pte_t ptep_get(pte_t *ptep)
{
	pte_basic_t val = READ_ONCE(ptep->pte);
	pte_t pte = {val, val, val, val};

	return pte;
}
#endif /* CONFIG_PPC_16K_PAGES */

#endif

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_NOHASH_32_PTE_8xx_H */

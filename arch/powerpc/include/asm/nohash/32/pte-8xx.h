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
#define _PAGE_PSIZE	_PAGE_SPS
#else
#define _PAGE_PSIZE		0
#endif

#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_PSIZE)
#define _PAGE_BASE	(_PAGE_BASE_NC)

/* Permission masks used to generate the __P and __S table */
#define PAGE_NONE	__pgprot(_PAGE_BASE | _PAGE_NA)
#define PAGE_SHARED	__pgprot(_PAGE_BASE)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_RO)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_RO | _PAGE_EXEC)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_RO)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_RO | _PAGE_EXEC)

#ifndef __ASSEMBLY__
static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_RO);
}

#define pte_wrprotect pte_wrprotect

static inline int pte_write(pte_t pte)
{
	return !(pte_val(pte) & _PAGE_RO);
}

#define pte_write pte_write

static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_RO);
}

#define pte_mkwrite pte_mkwrite

static inline bool pte_user(pte_t pte)
{
	return !(pte_val(pte) & _PAGE_SH);
}

#define pte_user pte_user

static inline pte_t pte_mkprivileged(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SH);
}

#define pte_mkprivileged pte_mkprivileged

static inline pte_t pte_mkuser(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_SH);
}

#define pte_mkuser pte_mkuser

static inline pte_t pte_mkhuge(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPS | _PAGE_HUGE);
}

#define pte_mkhuge pte_mkhuge

static inline unsigned long pgd_leaf_size(pgd_t pgd)
{
	if (pgd_val(pgd) & _PMD_PAGE_8M)
		return SZ_8M;
	return SZ_4M;
}

#define pgd_leaf_size pgd_leaf_size

static inline unsigned long pte_leaf_size(pte_t pte)
{
	pte_basic_t val = pte_val(pte);

	if (val & _PAGE_HUGE)
		return SZ_512K;
	if (val & _PAGE_SPS)
		return SZ_16K;
	return SZ_4K;
}

#define pte_leaf_size pte_leaf_size

#endif

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_NOHASH_32_PTE_8xx_H */

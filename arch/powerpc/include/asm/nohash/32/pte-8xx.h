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
#define _PAGE_PRESENT	0x0001	/* Page is valid */
#define _PAGE_NO_CACHE	0x0002	/* I: cache inhibit */
#define _PAGE_PRIVILEGED	0x0004	/* No ASID (context) compare */
#define _PAGE_HUGE	0x0008	/* SPS: Small Page Size (1 if 16k, 512k or 8M)*/
#define _PAGE_DIRTY	0x0100	/* C: page changed */

/* These 4 software bits must be masked out when the L2 entry is loaded
 * into the TLB.
 */
#define _PAGE_GUARDED	0x0010	/* Copied to L1 G entry in DTLB */
#define _PAGE_SPECIAL	0x0020	/* SW entry */
#define _PAGE_EXEC	0x0040	/* Copied to PP (bit 21) in ITLB */
#define _PAGE_ACCESSED	0x0080	/* software: page referenced */

#define _PAGE_NA	0x0200	/* Supervisor NA, User no access */
#define _PAGE_RO	0x0600	/* Supervisor RO, User no access */

#define _PMD_PRESENT	0x0001
#define _PMD_BAD	0x0fd0
#define _PMD_PAGE_MASK	0x000c
#define _PMD_PAGE_8M	0x000c
#define _PMD_PAGE_512K	0x0004
#define _PMD_USER	0x0020	/* APG 1 */

/* Until my rework is finished, 8xx still needs atomic PTE updates */
#define PTE_ATOMIC_UPDATES	1

#ifdef CONFIG_PPC_16K_PAGES
#define _PAGE_PSIZE	_PAGE_HUGE
#endif

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_NOHASH_32_PTE_8xx_H */

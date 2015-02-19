#ifndef _ASM_POWERPC_PTE_8xx_H
#define _ASM_POWERPC_PTE_8xx_H
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
#define _PAGE_SHARED	0x0004	/* No ASID (context) compare */
#define _PAGE_SPECIAL	0x0008	/* SW entry, forced to 0 by the TLB miss */
#define _PAGE_DIRTY	0x0100	/* C: page changed */

/* These 4 software bits must be masked out when the entry is loaded
 * into the TLB, 1 SW bit left(0x0080).
 */
#define _PAGE_GUARDED	0x0010	/* software: guarded access */
#define _PAGE_ACCESSED	0x0020	/* software: page referenced */
#define _PAGE_WRITETHRU	0x0040	/* software: caching is write through */

/* Setting any bits in the nibble with the follow two controls will
 * require a TLB exception handler change.  It is assumed unused bits
 * are always zero.
 */
#define _PAGE_RO	0x0400	/* lsb PP bits */
#define _PAGE_USER	0x0800	/* msb PP bits */
/* set when _PAGE_USER is unset and _PAGE_RO is set */
#define _PAGE_KNLRO	0x0200

#define _PMD_PRESENT	0x0001
#define _PMD_BAD	0x0ff0
#define _PMD_PAGE_MASK	0x000c
#define _PMD_PAGE_8M	0x000c

#define _PTE_NONE_MASK _PAGE_KNLRO

/* Until my rework is finished, 8xx still needs atomic PTE updates */
#define PTE_ATOMIC_UPDATES	1

/* We need to add _PAGE_SHARED to kernel pages */
#define _PAGE_KERNEL_RO	(_PAGE_SHARED | _PAGE_RO | _PAGE_KNLRO)
#define _PAGE_KERNEL_ROX	(_PAGE_EXEC | _PAGE_RO | _PAGE_KNLRO)

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_PTE_8xx_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_MMU_8XX_H_
#define _ASM_POWERPC_MMU_8XX_H_
/*
 * PPC8xx support
 */

/* Control/status registers for the MPC8xx.
 * A write operation to these registers causes serialized access.
 * During software tablewalk, the registers used perform mask/shift-add
 * operations when written/read.  A TLB entry is created when the Mx_RPN
 * is written, and the contents of several registers are used to
 * create the entry.
 */
#define SPRN_MI_CTR	784	/* Instruction TLB control register */
#define MI_GPM		0x80000000	/* Set domain manager mode */
#define MI_PPM		0x40000000	/* Set subpage protection */
#define MI_CIDEF	0x20000000	/* Set cache inhibit when MMU dis */
#define MI_RSV4I	0x08000000	/* Reserve 4 TLB entries */
#define MI_PPCS		0x02000000	/* Use MI_RPN prob/priv state */
#define MI_IDXMASK	0x00001f00	/* TLB index to be loaded */

/* These are the Ks and Kp from the PowerPC books.  For proper operation,
 * Ks = 0, Kp = 1.
 */
#define SPRN_MI_AP	786
#define MI_Ks		0x80000000	/* Should not be set */
#define MI_Kp		0x40000000	/* Should always be set */

/*
 * All pages' PP data bits are set to either 001 or 011 by copying _PAGE_EXEC
 * into bit 21 in the ITLBmiss handler (bit 21 is the middle bit), which means
 * respectively NA for All or X for Supervisor and no access for User.
 * Then we use the APG to say whether accesses are according to Page rules or
 * "all Supervisor" rules (Access to all)
 * _PAGE_ACCESSED is also managed via APG. When _PAGE_ACCESSED is not set, say
 * "all User" rules, that will lead to NA for all.
 * Therefore, we define 4 APG groups. lsb is _PAGE_ACCESSED
 * 0 => Kernel => 11 (all accesses performed according as user iaw page definition)
 * 1 => Kernel+Accessed => 01 (all accesses performed according to page definition)
 * 2 => User => 11 (all accesses performed according as user iaw page definition)
 * 3 => User+Accessed => 10 (all accesses performed according to swaped page definition) for KUEP
 * 4-15 => Not Used
 */
#define MI_APG_INIT	0xde000000

/* The effective page number register.  When read, contains the information
 * about the last instruction TLB miss.  When MI_RPN is written, bits in
 * this register are used to create the TLB entry.
 */
#define SPRN_MI_EPN	787
#define MI_EPNMASK	0xfffff000	/* Effective page number for entry */
#define MI_EVALID	0x00000200	/* Entry is valid */
#define MI_ASIDMASK	0x0000000f	/* ASID match value */
					/* Reset value is undefined */

/* A "level 1" or "segment" or whatever you want to call it register.
 * For the instruction TLB, it contains bits that get loaded into the
 * TLB entry when the MI_RPN is written.
 */
#define SPRN_MI_TWC	789
#define MI_APG		0x000001e0	/* Access protection group (0) */
#define MI_GUARDED	0x00000010	/* Guarded storage */
#define MI_PSMASK	0x0000000c	/* Mask of page size bits */
#define MI_PS8MEG	0x0000000c	/* 8M page size */
#define MI_PS512K	0x00000004	/* 512K page size */
#define MI_PS4K_16K	0x00000000	/* 4K or 16K page size */
#define MI_SVALID	0x00000001	/* Segment entry is valid */
					/* Reset value is undefined */

/* Real page number.  Defined by the pte.  Writing this register
 * causes a TLB entry to be created for the instruction TLB, using
 * additional information from the MI_EPN, and MI_TWC registers.
 */
#define SPRN_MI_RPN	790
#define MI_SPS16K	0x00000008	/* Small page size (0 = 4k, 1 = 16k) */

/* Define an RPN value for mapping kernel memory to large virtual
 * pages for boot initialization.  This has real page number of 0,
 * large page size, shared page, cache enabled, and valid.
 * Also mark all subpages valid and write access.
 */
#define MI_BOOTINIT	0x000001fd

#define SPRN_MD_CTR	792	/* Data TLB control register */
#define MD_GPM		0x80000000	/* Set domain manager mode */
#define MD_PPM		0x40000000	/* Set subpage protection */
#define MD_CIDEF	0x20000000	/* Set cache inhibit when MMU dis */
#define MD_WTDEF	0x10000000	/* Set writethrough when MMU dis */
#define MD_RSV4I	0x08000000	/* Reserve 4 TLB entries */
#define MD_TWAM		0x04000000	/* Use 4K page hardware assist */
#define MD_PPCS		0x02000000	/* Use MI_RPN prob/priv state */
#define MD_IDXMASK	0x00001f00	/* TLB index to be loaded */

#define SPRN_M_CASID	793	/* Address space ID (context) to match */
#define MC_ASIDMASK	0x0000000f	/* Bits used for ASID value */


/* These are the Ks and Kp from the PowerPC books.  For proper operation,
 * Ks = 0, Kp = 1.
 */
#define SPRN_MD_AP	794
#define MD_Ks		0x80000000	/* Should not be set */
#define MD_Kp		0x40000000	/* Should always be set */

/* See explanation above at the definition of MI_APG_INIT */
#define MD_APG_INIT	0xdc000000
#define MD_APG_KUAP	0xde000000

/* The effective page number register.  When read, contains the information
 * about the last instruction TLB miss.  When MD_RPN is written, bits in
 * this register are used to create the TLB entry.
 */
#define SPRN_MD_EPN	795
#define MD_EPNMASK	0xfffff000	/* Effective page number for entry */
#define MD_EVALID	0x00000200	/* Entry is valid */
#define MD_ASIDMASK	0x0000000f	/* ASID match value */
					/* Reset value is undefined */

/* The pointer to the base address of the first level page table.
 * During a software tablewalk, reading this register provides the address
 * of the entry associated with MD_EPN.
 */
#define SPRN_M_TWB	796
#define	M_L1TB		0xfffff000	/* Level 1 table base address */
#define M_L1INDX	0x00000ffc	/* Level 1 index, when read */
					/* Reset value is undefined */

/* A "level 1" or "segment" or whatever you want to call it register.
 * For the data TLB, it contains bits that get loaded into the TLB entry
 * when the MD_RPN is written.  It is also provides the hardware assist
 * for finding the PTE address during software tablewalk.
 */
#define SPRN_MD_TWC	797
#define MD_L2TB		0xfffff000	/* Level 2 table base address */
#define MD_L2INDX	0xfffffe00	/* Level 2 index (*pte), when read */
#define MD_APG		0x000001e0	/* Access protection group (0) */
#define MD_GUARDED	0x00000010	/* Guarded storage */
#define MD_PSMASK	0x0000000c	/* Mask of page size bits */
#define MD_PS8MEG	0x0000000c	/* 8M page size */
#define MD_PS512K	0x00000004	/* 512K page size */
#define MD_PS4K_16K	0x00000000	/* 4K or 16K page size */
#define MD_WT		0x00000002	/* Use writethrough page attribute */
#define MD_SVALID	0x00000001	/* Segment entry is valid */
					/* Reset value is undefined */


/* Real page number.  Defined by the pte.  Writing this register
 * causes a TLB entry to be created for the data TLB, using
 * additional information from the MD_EPN, and MD_TWC registers.
 */
#define SPRN_MD_RPN	798
#define MD_SPS16K	0x00000008	/* Small page size (0 = 4k, 1 = 16k) */

/* This is a temporary storage register that could be used to save
 * a processor working register during a tablewalk.
 */
#define SPRN_M_TW	799

#if defined(CONFIG_PPC_4K_PAGES)
#define mmu_virtual_psize	MMU_PAGE_4K
#elif defined(CONFIG_PPC_16K_PAGES)
#define mmu_virtual_psize	MMU_PAGE_16K
#define PTE_FRAG_NR		4
#define PTE_FRAG_SIZE_SHIFT	12
#define PTE_FRAG_SIZE		(1UL << 12)
#else
#error "Unsupported PAGE_SIZE"
#endif

#define mmu_linear_psize	MMU_PAGE_8M

#define MODULES_VADDR	(PAGE_OFFSET - SZ_256M)
#define MODULES_END	PAGE_OFFSET

#ifndef __ASSEMBLY__

#include <linux/mmdebug.h>
#include <linux/sizes.h>

void mmu_pin_tlb(unsigned long top, bool readonly);

typedef struct {
	unsigned int id;
	unsigned int active;
	void __user *vdso;
	void *pte_frag;
} mm_context_t;

#define PHYS_IMMR_BASE (mfspr(SPRN_IMMR) & 0xfff80000)
#define VIRT_IMMR_BASE (__fix_to_virt(FIX_IMMR_BASE))

/* Page size definitions, common between 32 and 64-bit
 *
 *    shift : is the "PAGE_SHIFT" value for that page size
 *    penc  : is the pte encoding mask
 *
 */
struct mmu_psize_def {
	unsigned int	shift;	/* number of bits */
	unsigned int	enc;	/* PTE encoding */
	unsigned int    ind;    /* Corresponding indirect page size shift */
	unsigned int	flags;
#define MMU_PAGE_SIZE_DIRECT	0x1	/* Supported as a direct size */
#define MMU_PAGE_SIZE_INDIRECT	0x2	/* Supported as an indirect size */
};

extern struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];

static inline int shift_to_mmu_psize(unsigned int shift)
{
	int psize;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize)
		if (mmu_psize_defs[psize].shift == shift)
			return psize;
	return -1;
}

static inline unsigned int mmu_psize_to_shift(unsigned int mmu_psize)
{
	if (mmu_psize_defs[mmu_psize].shift)
		return mmu_psize_defs[mmu_psize].shift;
	BUG();
}

static inline bool arch_vmap_try_size(unsigned long addr, unsigned long end, u64 pfn,
				      unsigned int max_page_shift, unsigned long size)
{
	if (end - addr < size)
		return false;

	if ((1UL << max_page_shift) < size)
		return false;

	if (!IS_ALIGNED(addr, size))
		return false;

	if (!IS_ALIGNED(PFN_PHYS(pfn), size))
		return false;

	return true;
}

static inline unsigned long arch_vmap_pte_range_map_size(unsigned long addr, unsigned long end,
							 u64 pfn, unsigned int max_page_shift)
{
	if (arch_vmap_try_size(addr, end, pfn, max_page_shift, SZ_512K))
		return SZ_512K;
	if (PAGE_SIZE == SZ_16K)
		return SZ_16K;
	if (arch_vmap_try_size(addr, end, pfn, max_page_shift, SZ_16K))
		return SZ_16K;
	return PAGE_SIZE;
}
#define arch_vmap_pte_range_map_size arch_vmap_pte_range_map_size

static inline int arch_vmap_pte_supported_shift(unsigned long size)
{
	if (size >= SZ_512K)
		return 19;
	else if (size >= SZ_16K)
		return 14;
	else
		return PAGE_SHIFT;
}
#define arch_vmap_pte_supported_shift arch_vmap_pte_supported_shift

/* patch sites */
extern s32 patch__itlbmiss_exit_1, patch__dtlbmiss_exit_1;
extern s32 patch__itlbmiss_perf, patch__dtlbmiss_perf;

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_MMU_8XX_H_ */

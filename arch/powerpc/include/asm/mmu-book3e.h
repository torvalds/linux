#ifndef _ASM_POWERPC_MMU_BOOK3E_H_
#define _ASM_POWERPC_MMU_BOOK3E_H_
/*
 * Freescale Book-E/Book-3e (ISA 2.06+) MMU support
 */

/* Book-3e defined page sizes */
#define BOOK3E_PAGESZ_1K	0
#define BOOK3E_PAGESZ_2K	1
#define BOOK3E_PAGESZ_4K	2
#define BOOK3E_PAGESZ_8K	3
#define BOOK3E_PAGESZ_16K	4
#define BOOK3E_PAGESZ_32K	5
#define BOOK3E_PAGESZ_64K	6
#define BOOK3E_PAGESZ_128K	7
#define BOOK3E_PAGESZ_256K	8
#define BOOK3E_PAGESZ_512K	9
#define BOOK3E_PAGESZ_1M	10
#define BOOK3E_PAGESZ_2M	11
#define BOOK3E_PAGESZ_4M	12
#define BOOK3E_PAGESZ_8M	13
#define BOOK3E_PAGESZ_16M	14
#define BOOK3E_PAGESZ_32M	15
#define BOOK3E_PAGESZ_64M	16
#define BOOK3E_PAGESZ_128M	17
#define BOOK3E_PAGESZ_256M	18
#define BOOK3E_PAGESZ_512M	19
#define BOOK3E_PAGESZ_1GB	20
#define BOOK3E_PAGESZ_2GB	21
#define BOOK3E_PAGESZ_4GB	22
#define BOOK3E_PAGESZ_8GB	23
#define BOOK3E_PAGESZ_16GB	24
#define BOOK3E_PAGESZ_32GB	25
#define BOOK3E_PAGESZ_64GB	26
#define BOOK3E_PAGESZ_128GB	27
#define BOOK3E_PAGESZ_256GB	28
#define BOOK3E_PAGESZ_512GB	29
#define BOOK3E_PAGESZ_1TB	30
#define BOOK3E_PAGESZ_2TB	31

/* MAS registers bit definitions */

#define MAS0_TLBSEL(x)		((x << 28) & 0x30000000)
#define MAS0_ESEL(x)		((x << 16) & 0x0FFF0000)
#define MAS0_NV(x)		((x) & 0x00000FFF)
#define MAS0_HES		0x00004000
#define MAS0_WQ_ALLWAYS		0x00000000
#define MAS0_WQ_COND		0x00001000
#define MAS0_WQ_CLR_RSRV       	0x00002000

#define MAS1_VALID		0x80000000
#define MAS1_IPROT		0x40000000
#define MAS1_TID(x)		((x << 16) & 0x3FFF0000)
#define MAS1_IND		0x00002000
#define MAS1_TS			0x00001000
#define MAS1_TSIZE_MASK		0x00000f80
#define MAS1_TSIZE_SHIFT	7
#define MAS1_TSIZE(x)		((x << MAS1_TSIZE_SHIFT) & MAS1_TSIZE_MASK)

#define MAS2_EPN		0xFFFFF000
#define MAS2_X0			0x00000040
#define MAS2_X1			0x00000020
#define MAS2_W			0x00000010
#define MAS2_I			0x00000008
#define MAS2_M			0x00000004
#define MAS2_G			0x00000002
#define MAS2_E			0x00000001
#define MAS2_EPN_MASK(size)		(~0 << (size + 10))
#define MAS2_VAL(addr, size, flags)	((addr) & MAS2_EPN_MASK(size) | (flags))

#define MAS3_RPN		0xFFFFF000
#define MAS3_U0			0x00000200
#define MAS3_U1			0x00000100
#define MAS3_U2			0x00000080
#define MAS3_U3			0x00000040
#define MAS3_UX			0x00000020
#define MAS3_SX			0x00000010
#define MAS3_UW			0x00000008
#define MAS3_SW			0x00000004
#define MAS3_UR			0x00000002
#define MAS3_SR			0x00000001
#define MAS3_SPSIZE		0x0000003e
#define MAS3_SPSIZE_SHIFT	1

#define MAS4_TLBSELD(x) 	MAS0_TLBSEL(x)
#define MAS4_INDD		0x00008000	/* Default IND */
#define MAS4_TSIZED(x)		MAS1_TSIZE(x)
#define MAS4_X0D		0x00000040
#define MAS4_X1D		0x00000020
#define MAS4_WD			0x00000010
#define MAS4_ID			0x00000008
#define MAS4_MD			0x00000004
#define MAS4_GD			0x00000002
#define MAS4_ED			0x00000001
#define MAS4_WIMGED_MASK	0x0000001f	/* Default WIMGE */
#define MAS4_WIMGED_SHIFT	0
#define MAS4_VLED		MAS4_X1D	/* Default VLE */
#define MAS4_ACMD		0x000000c0	/* Default ACM */
#define MAS4_ACMD_SHIFT		6
#define MAS4_TSIZED_MASK	0x00000f80	/* Default TSIZE */
#define MAS4_TSIZED_SHIFT	7

#define MAS6_SPID0		0x3FFF0000
#define MAS6_SPID1		0x00007FFE
#define MAS6_ISIZE(x)		MAS1_TSIZE(x)
#define MAS6_SAS		0x00000001
#define MAS6_SPID		MAS6_SPID0
#define MAS6_SIND 		0x00000002	/* Indirect page */
#define MAS6_SIND_SHIFT		1
#define MAS6_SPID_MASK		0x3fff0000
#define MAS6_SPID_SHIFT		16
#define MAS6_ISIZE_MASK		0x00000f80
#define MAS6_ISIZE_SHIFT	7

#define MAS7_RPN		0xFFFFFFFF

/* Bit definitions for MMUCSR0 */
#define MMUCSR0_TLB1FI	0x00000002	/* TLB1 Flash invalidate */
#define MMUCSR0_TLB0FI	0x00000004	/* TLB0 Flash invalidate */
#define MMUCSR0_TLB2FI	0x00000040	/* TLB2 Flash invalidate */
#define MMUCSR0_TLB3FI	0x00000020	/* TLB3 Flash invalidate */
#define MMUCSR0_TLBFI	(MMUCSR0_TLB0FI | MMUCSR0_TLB1FI | \
			 MMUCSR0_TLB2FI | MMUCSR0_TLB3FI)
#define MMUCSR0_TLB0PS	0x00000780	/* TLB0 Page Size */
#define MMUCSR0_TLB1PS	0x00007800	/* TLB1 Page Size */
#define MMUCSR0_TLB2PS	0x00078000	/* TLB2 Page Size */
#define MMUCSR0_TLB3PS	0x00780000	/* TLB3 Page Size */

/* TLBnCFG encoding */
#define TLBnCFG_N_ENTRY		0x00000fff	/* number of entries */
#define TLBnCFG_HES		0x00002000	/* HW select supported */
#define TLBnCFG_IPROT		0x00008000	/* IPROT supported */
#define TLBnCFG_GTWE		0x00010000	/* Guest can write */
#define TLBnCFG_IND		0x00020000	/* IND entries supported */
#define TLBnCFG_PT		0x00040000	/* Can load from page table */
#define TLBnCFG_ASSOC		0xff000000	/* Associativity */

/* TLBnPS encoding */
#define TLBnPS_4K		0x00000004
#define TLBnPS_8K		0x00000008
#define TLBnPS_16K		0x00000010
#define TLBnPS_32K		0x00000020
#define TLBnPS_64K		0x00000040
#define TLBnPS_128K		0x00000080
#define TLBnPS_256K		0x00000100
#define TLBnPS_512K		0x00000200
#define TLBnPS_1M 		0x00000400
#define TLBnPS_2M 		0x00000800
#define TLBnPS_4M 		0x00001000
#define TLBnPS_8M 		0x00002000
#define TLBnPS_16M		0x00004000
#define TLBnPS_32M		0x00008000
#define TLBnPS_64M		0x00010000
#define TLBnPS_128M		0x00020000
#define TLBnPS_256M		0x00040000
#define TLBnPS_512M		0x00080000
#define TLBnPS_1G		0x00100000
#define TLBnPS_2G		0x00200000
#define TLBnPS_4G		0x00400000
#define TLBnPS_8G		0x00800000
#define TLBnPS_16G		0x01000000
#define TLBnPS_32G		0x02000000
#define TLBnPS_64G		0x04000000
#define TLBnPS_128G		0x08000000
#define TLBnPS_256G		0x10000000

/* tlbilx action encoding */
#define TLBILX_T_ALL			0
#define TLBILX_T_TID			1
#define TLBILX_T_FULLMATCH		3
#define TLBILX_T_CLASS0			4
#define TLBILX_T_CLASS1			5
#define TLBILX_T_CLASS2			6
#define TLBILX_T_CLASS3			7

#ifndef __ASSEMBLY__

extern unsigned int tlbcam_index;

typedef struct {
	unsigned int	id;
	unsigned int	active;
	unsigned long	vdso_base;
} mm_context_t;

/* Page size definitions, common between 32 and 64-bit
 *
 *    shift : is the "PAGE_SHIFT" value for that page size
 *    penc  : is the pte encoding mask
 *
 */
struct mmu_psize_def
{
	unsigned int	shift;	/* number of bits */
	unsigned int	enc;	/* PTE encoding */
	unsigned int    ind;    /* Corresponding indirect page size shift */
	unsigned int	flags;
#define MMU_PAGE_SIZE_DIRECT	0x1	/* Supported as a direct size */
#define MMU_PAGE_SIZE_INDIRECT	0x2	/* Supported as an indirect size */
};
extern struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];

/* The page sizes use the same names as 64-bit hash but are
 * constants
 */
#if defined(CONFIG_PPC_4K_PAGES)
#define mmu_virtual_psize	MMU_PAGE_4K
#elif defined(CONFIG_PPC_64K_PAGES)
#define mmu_virtual_psize	MMU_PAGE_64K
#else
#error Unsupported page size
#endif

extern int mmu_linear_psize;
extern int mmu_vmemmap_psize;

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_MMU_BOOK3E_H_ */

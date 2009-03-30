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

#define MAS0_TLBSEL(x)	((x << 28) & 0x30000000)
#define MAS0_ESEL(x)	((x << 16) & 0x0FFF0000)
#define MAS0_NV(x)	((x) & 0x00000FFF)

#define MAS1_VALID 	0x80000000
#define MAS1_IPROT	0x40000000
#define MAS1_TID(x)	((x << 16) & 0x3FFF0000)
#define MAS1_IND	0x00002000
#define MAS1_TS		0x00001000
#define MAS1_TSIZE(x)	((x << 7) & 0x00000F80)

#define MAS2_EPN	0xFFFFF000
#define MAS2_X0		0x00000040
#define MAS2_X1		0x00000020
#define MAS2_W		0x00000010
#define MAS2_I		0x00000008
#define MAS2_M		0x00000004
#define MAS2_G		0x00000002
#define MAS2_E		0x00000001
#define MAS2_EPN_MASK(size)		(~0 << (size + 10))
#define MAS2_VAL(addr, size, flags)	((addr) & MAS2_EPN_MASK(size) | (flags))

#define MAS3_RPN	0xFFFFF000
#define MAS3_U0		0x00000200
#define MAS3_U1		0x00000100
#define MAS3_U2		0x00000080
#define MAS3_U3		0x00000040
#define MAS3_UX		0x00000020
#define MAS3_SX		0x00000010
#define MAS3_UW		0x00000008
#define MAS3_SW		0x00000004
#define MAS3_UR		0x00000002
#define MAS3_SR		0x00000001

#define MAS4_TLBSELD(x) MAS0_TLBSEL(x)
#define MAS4_INDD	0x00008000
#define MAS4_TSIZED(x)	MAS1_TSIZE(x)
#define MAS4_X0D	0x00000040
#define MAS4_X1D	0x00000020
#define MAS4_WD		0x00000010
#define MAS4_ID		0x00000008
#define MAS4_MD		0x00000004
#define MAS4_GD		0x00000002
#define MAS4_ED		0x00000001

#define MAS6_SPID0	0x3FFF0000
#define MAS6_SPID1	0x00007FFE
#define MAS6_ISIZE(x)	MAS1_TSIZE(x)
#define MAS6_SAS	0x00000001
#define MAS6_SPID	MAS6_SPID0

#define MAS7_RPN	0xFFFFFFFF

#ifndef __ASSEMBLY__

extern unsigned int tlbcam_index;

typedef struct {
	unsigned int	id;
	unsigned int	active;
	unsigned long	vdso_base;
} mm_context_t;
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_MMU_BOOK3E_H_ */

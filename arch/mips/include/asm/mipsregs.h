/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1997, 2000, 2001 by Ralf Baechle
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Modified for further R[236]000 support by Paul M. Antoine, 1996.
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 07 MIPS Technologies, Inc.
 * Copyright (C) 2003, 2004  Maciej W. Rozycki
 */
#ifndef _ASM_MIPSREGS_H
#define _ASM_MIPSREGS_H

#include <linux/linkage.h>
#include <asm/hazards.h>
#include <asm/war.h>

/*
 * The following macros are especially useful for __asm__
 * inline assembler.
 */
#ifndef __STR
#define __STR(x) #x
#endif
#ifndef STR
#define STR(x) __STR(x)
#endif

/*
 *  Configure language
 */
#ifdef __ASSEMBLY__
#define _ULCAST_
#else
#define _ULCAST_ (unsigned long)
#endif

/*
 * Coprocessor 0 register names
 */
#define CP0_INDEX $0
#define CP0_RANDOM $1
#define CP0_ENTRYLO0 $2
#define CP0_ENTRYLO1 $3
#define CP0_CONF $3
#define CP0_CONTEXT $4
#define CP0_PAGEMASK $5
#define CP0_WIRED $6
#define CP0_INFO $7
#define CP0_BADVADDR $8
#define CP0_COUNT $9
#define CP0_ENTRYHI $10
#define CP0_COMPARE $11
#define CP0_STATUS $12
#define CP0_CAUSE $13
#define CP0_EPC $14
#define CP0_PRID $15
#define CP0_CONFIG $16
#define CP0_LLADDR $17
#define CP0_WATCHLO $18
#define CP0_WATCHHI $19
#define CP0_XCONTEXT $20
#define CP0_FRAMEMASK $21
#define CP0_DIAGNOSTIC $22
#define CP0_DEBUG $23
#define CP0_DEPC $24
#define CP0_PERFORMANCE $25
#define CP0_ECC $26
#define CP0_CACHEERR $27
#define CP0_TAGLO $28
#define CP0_TAGHI $29
#define CP0_ERROREPC $30
#define CP0_DESAVE $31

/*
 * R4640/R4650 cp0 register names.  These registers are listed
 * here only for completeness; without MMU these CPUs are not useable
 * by Linux.  A future ELKS port might take make Linux run on them
 * though ...
 */
#define CP0_IBASE $0
#define CP0_IBOUND $1
#define CP0_DBASE $2
#define CP0_DBOUND $3
#define CP0_CALG $17
#define CP0_IWATCH $18
#define CP0_DWATCH $19

/*
 * Coprocessor 0 Set 1 register names
 */
#define CP0_S1_DERRADDR0  $26
#define CP0_S1_DERRADDR1  $27
#define CP0_S1_INTCONTROL $20

/*
 * Coprocessor 0 Set 2 register names
 */
#define CP0_S2_SRSCTL	  $12	/* MIPSR2 */

/*
 * Coprocessor 0 Set 3 register names
 */
#define CP0_S3_SRSMAP	  $12	/* MIPSR2 */

/*
 *  TX39 Series
 */
#define CP0_TX39_CACHE	$7

/*
 * Coprocessor 1 (FPU) register names
 */
#define CP1_REVISION   $0
#define CP1_STATUS     $31

/*
 * FPU Status Register Values
 */
/*
 * Status Register Values
 */

#define FPU_CSR_FLUSH	0x01000000	/* flush denormalised results to 0 */
#define FPU_CSR_COND	0x00800000	/* $fcc0 */
#define FPU_CSR_COND0	0x00800000	/* $fcc0 */
#define FPU_CSR_COND1	0x02000000	/* $fcc1 */
#define FPU_CSR_COND2	0x04000000	/* $fcc2 */
#define FPU_CSR_COND3	0x08000000	/* $fcc3 */
#define FPU_CSR_COND4	0x10000000	/* $fcc4 */
#define FPU_CSR_COND5	0x20000000	/* $fcc5 */
#define FPU_CSR_COND6	0x40000000	/* $fcc6 */
#define FPU_CSR_COND7	0x80000000	/* $fcc7 */

/*
 * Bits 18 - 20 of the FPU Status Register will be read as 0,
 * and should be written as zero.
 */
#define FPU_CSR_RSVD	0x001c0000

/*
 * X the exception cause indicator
 * E the exception enable
 * S the sticky/flag bit
*/
#define FPU_CSR_ALL_X	0x0003f000
#define FPU_CSR_UNI_X	0x00020000
#define FPU_CSR_INV_X	0x00010000
#define FPU_CSR_DIV_X	0x00008000
#define FPU_CSR_OVF_X	0x00004000
#define FPU_CSR_UDF_X	0x00002000
#define FPU_CSR_INE_X	0x00001000

#define FPU_CSR_ALL_E	0x00000f80
#define FPU_CSR_INV_E	0x00000800
#define FPU_CSR_DIV_E	0x00000400
#define FPU_CSR_OVF_E	0x00000200
#define FPU_CSR_UDF_E	0x00000100
#define FPU_CSR_INE_E	0x00000080

#define FPU_CSR_ALL_S	0x0000007c
#define FPU_CSR_INV_S	0x00000040
#define FPU_CSR_DIV_S	0x00000020
#define FPU_CSR_OVF_S	0x00000010
#define FPU_CSR_UDF_S	0x00000008
#define FPU_CSR_INE_S	0x00000004

/* Bits 0 and 1 of FPU Status Register specify the rounding mode */
#define FPU_CSR_RM	0x00000003
#define FPU_CSR_RN	0x0	/* nearest */
#define FPU_CSR_RZ	0x1	/* towards zero */
#define FPU_CSR_RU	0x2	/* towards +Infinity */
#define FPU_CSR_RD	0x3	/* towards -Infinity */


/*
 * Values for PageMask register
 */
#ifdef CONFIG_CPU_VR41XX

/* Why doesn't stupidity hurt ... */

#define PM_1K		0x00000000
#define PM_4K		0x00001800
#define PM_16K		0x00007800
#define PM_64K		0x0001f800
#define PM_256K		0x0007f800

#else

#define PM_4K		0x00000000
#define PM_8K		0x00002000
#define PM_16K		0x00006000
#define PM_32K		0x0000e000
#define PM_64K		0x0001e000
#define PM_128K		0x0003e000
#define PM_256K		0x0007e000
#define PM_512K		0x000fe000
#define PM_1M		0x001fe000
#define PM_2M		0x003fe000
#define PM_4M		0x007fe000
#define PM_8M		0x00ffe000
#define PM_16M		0x01ffe000
#define PM_32M		0x03ffe000
#define PM_64M		0x07ffe000
#define PM_256M		0x1fffe000
#define PM_1G		0x7fffe000

#endif

/*
 * Default page size for a given kernel configuration
 */
#ifdef CONFIG_PAGE_SIZE_4KB
#define PM_DEFAULT_MASK PM_4K
#elif defined(CONFIG_PAGE_SIZE_8KB)
#define PM_DEFAULT_MASK PM_8K
#elif defined(CONFIG_PAGE_SIZE_16KB)
#define PM_DEFAULT_MASK PM_16K
#elif defined(CONFIG_PAGE_SIZE_32KB)
#define PM_DEFAULT_MASK PM_32K
#elif defined(CONFIG_PAGE_SIZE_64KB)
#define PM_DEFAULT_MASK PM_64K
#else
#error Bad page size configuration!
#endif

/*
 * Default huge tlb size for a given kernel configuration
 */
#ifdef CONFIG_PAGE_SIZE_4KB
#define PM_HUGE_MASK	PM_1M
#elif defined(CONFIG_PAGE_SIZE_8KB)
#define PM_HUGE_MASK	PM_4M
#elif defined(CONFIG_PAGE_SIZE_16KB)
#define PM_HUGE_MASK	PM_16M
#elif defined(CONFIG_PAGE_SIZE_32KB)
#define PM_HUGE_MASK	PM_64M
#elif defined(CONFIG_PAGE_SIZE_64KB)
#define PM_HUGE_MASK	PM_256M
#elif defined(CONFIG_MIPS_HUGE_TLB_SUPPORT)
#error Bad page size configuration for hugetlbfs!
#endif

/*
 * Values used for computation of new tlb entries
 */
#define PL_4K		12
#define PL_16K		14
#define PL_64K		16
#define PL_256K		18
#define PL_1M		20
#define PL_4M		22
#define PL_16M		24
#define PL_64M		26
#define PL_256M		28

/*
 * PageGrain bits
 */
#define PG_RIE		(_ULCAST_(1) <<	 31)
#define PG_XIE		(_ULCAST_(1) <<	 30)
#define PG_ELPA		(_ULCAST_(1) <<	 29)
#define PG_ESP		(_ULCAST_(1) <<	 28)

/*
 * R4x00 interrupt enable / cause bits
 */
#define IE_SW0		(_ULCAST_(1) <<	 8)
#define IE_SW1		(_ULCAST_(1) <<	 9)
#define IE_IRQ0		(_ULCAST_(1) << 10)
#define IE_IRQ1		(_ULCAST_(1) << 11)
#define IE_IRQ2		(_ULCAST_(1) << 12)
#define IE_IRQ3		(_ULCAST_(1) << 13)
#define IE_IRQ4		(_ULCAST_(1) << 14)
#define IE_IRQ5		(_ULCAST_(1) << 15)

/*
 * R4x00 interrupt cause bits
 */
#define C_SW0		(_ULCAST_(1) <<	 8)
#define C_SW1		(_ULCAST_(1) <<	 9)
#define C_IRQ0		(_ULCAST_(1) << 10)
#define C_IRQ1		(_ULCAST_(1) << 11)
#define C_IRQ2		(_ULCAST_(1) << 12)
#define C_IRQ3		(_ULCAST_(1) << 13)
#define C_IRQ4		(_ULCAST_(1) << 14)
#define C_IRQ5		(_ULCAST_(1) << 15)

/*
 * Bitfields in the R4xx0 cp0 status register
 */
#define ST0_IE			0x00000001
#define ST0_EXL			0x00000002
#define ST0_ERL			0x00000004
#define ST0_KSU			0x00000018
#  define KSU_USER		0x00000010
#  define KSU_SUPERVISOR	0x00000008
#  define KSU_KERNEL		0x00000000
#define ST0_UX			0x00000020
#define ST0_SX			0x00000040
#define ST0_KX			0x00000080
#define ST0_DE			0x00010000
#define ST0_CE			0x00020000

/*
 * Setting c0_status.co enables Hit_Writeback and Hit_Writeback_Invalidate
 * cacheops in userspace.  This bit exists only on RM7000 and RM9000
 * processors.
 */
#define ST0_CO			0x08000000

/*
 * Bitfields in the R[23]000 cp0 status register.
 */
#define ST0_IEC			0x00000001
#define ST0_KUC			0x00000002
#define ST0_IEP			0x00000004
#define ST0_KUP			0x00000008
#define ST0_IEO			0x00000010
#define ST0_KUO			0x00000020
/* bits 6 & 7 are reserved on R[23]000 */
#define ST0_ISC			0x00010000
#define ST0_SWC			0x00020000
#define ST0_CM			0x00080000

/*
 * Bits specific to the R4640/R4650
 */
#define ST0_UM			(_ULCAST_(1) <<	 4)
#define ST0_IL			(_ULCAST_(1) << 23)
#define ST0_DL			(_ULCAST_(1) << 24)

/*
 * Enable the MIPS MDMX and DSP ASEs
 */
#define ST0_MX			0x01000000

/*
 * Bitfields in the TX39 family CP0 Configuration Register 3
 */
#define TX39_CONF_ICS_SHIFT	19
#define TX39_CONF_ICS_MASK	0x00380000
#define TX39_CONF_ICS_1KB	0x00000000
#define TX39_CONF_ICS_2KB	0x00080000
#define TX39_CONF_ICS_4KB	0x00100000
#define TX39_CONF_ICS_8KB	0x00180000
#define TX39_CONF_ICS_16KB	0x00200000

#define TX39_CONF_DCS_SHIFT	16
#define TX39_CONF_DCS_MASK	0x00070000
#define TX39_CONF_DCS_1KB	0x00000000
#define TX39_CONF_DCS_2KB	0x00010000
#define TX39_CONF_DCS_4KB	0x00020000
#define TX39_CONF_DCS_8KB	0x00030000
#define TX39_CONF_DCS_16KB	0x00040000

#define TX39_CONF_CWFON		0x00004000
#define TX39_CONF_WBON		0x00002000
#define TX39_CONF_RF_SHIFT	10
#define TX39_CONF_RF_MASK	0x00000c00
#define TX39_CONF_DOZE		0x00000200
#define TX39_CONF_HALT		0x00000100
#define TX39_CONF_LOCK		0x00000080
#define TX39_CONF_ICE		0x00000020
#define TX39_CONF_DCE		0x00000010
#define TX39_CONF_IRSIZE_SHIFT	2
#define TX39_CONF_IRSIZE_MASK	0x0000000c
#define TX39_CONF_DRSIZE_SHIFT	0
#define TX39_CONF_DRSIZE_MASK	0x00000003

/*
 * Status register bits available in all MIPS CPUs.
 */
#define ST0_IM			0x0000ff00
#define	 STATUSB_IP0		8
#define	 STATUSF_IP0		(_ULCAST_(1) <<	 8)
#define	 STATUSB_IP1		9
#define	 STATUSF_IP1		(_ULCAST_(1) <<	 9)
#define	 STATUSB_IP2		10
#define	 STATUSF_IP2		(_ULCAST_(1) << 10)
#define	 STATUSB_IP3		11
#define	 STATUSF_IP3		(_ULCAST_(1) << 11)
#define	 STATUSB_IP4		12
#define	 STATUSF_IP4		(_ULCAST_(1) << 12)
#define	 STATUSB_IP5		13
#define	 STATUSF_IP5		(_ULCAST_(1) << 13)
#define	 STATUSB_IP6		14
#define	 STATUSF_IP6		(_ULCAST_(1) << 14)
#define	 STATUSB_IP7		15
#define	 STATUSF_IP7		(_ULCAST_(1) << 15)
#define	 STATUSB_IP8		0
#define	 STATUSF_IP8		(_ULCAST_(1) <<	 0)
#define	 STATUSB_IP9		1
#define	 STATUSF_IP9		(_ULCAST_(1) <<	 1)
#define	 STATUSB_IP10		2
#define	 STATUSF_IP10		(_ULCAST_(1) <<	 2)
#define	 STATUSB_IP11		3
#define	 STATUSF_IP11		(_ULCAST_(1) <<	 3)
#define	 STATUSB_IP12		4
#define	 STATUSF_IP12		(_ULCAST_(1) <<	 4)
#define	 STATUSB_IP13		5
#define	 STATUSF_IP13		(_ULCAST_(1) <<	 5)
#define	 STATUSB_IP14		6
#define	 STATUSF_IP14		(_ULCAST_(1) <<	 6)
#define	 STATUSB_IP15		7
#define	 STATUSF_IP15		(_ULCAST_(1) <<	 7)
#define ST0_CH			0x00040000
#define ST0_NMI			0x00080000
#define ST0_SR			0x00100000
#define ST0_TS			0x00200000
#define ST0_BEV			0x00400000
#define ST0_RE			0x02000000
#define ST0_FR			0x04000000
#define ST0_CU			0xf0000000
#define ST0_CU0			0x10000000
#define ST0_CU1			0x20000000
#define ST0_CU2			0x40000000
#define ST0_CU3			0x80000000
#define ST0_XX			0x80000000	/* MIPS IV naming */

/*
 * Bitfields and bit numbers in the coprocessor 0 IntCtl register. (MIPSR2)
 *
 * Refer to your MIPS R4xx0 manual, chapter 5 for explanation.
 */
#define INTCTLB_IPPCI		26
#define INTCTLF_IPPCI		(_ULCAST_(7) << INTCTLB_IPPCI)
#define INTCTLB_IPTI		29
#define INTCTLF_IPTI		(_ULCAST_(7) << INTCTLB_IPTI)

/*
 * Bitfields and bit numbers in the coprocessor 0 cause register.
 *
 * Refer to your MIPS R4xx0 manual, chapter 5 for explanation.
 */
#define	 CAUSEB_EXCCODE		2
#define	 CAUSEF_EXCCODE		(_ULCAST_(31)  <<  2)
#define	 CAUSEB_IP		8
#define	 CAUSEF_IP		(_ULCAST_(255) <<  8)
#define	 CAUSEB_IP0		8
#define	 CAUSEF_IP0		(_ULCAST_(1)   <<  8)
#define	 CAUSEB_IP1		9
#define	 CAUSEF_IP1		(_ULCAST_(1)   <<  9)
#define	 CAUSEB_IP2		10
#define	 CAUSEF_IP2		(_ULCAST_(1)   << 10)
#define	 CAUSEB_IP3		11
#define	 CAUSEF_IP3		(_ULCAST_(1)   << 11)
#define	 CAUSEB_IP4		12
#define	 CAUSEF_IP4		(_ULCAST_(1)   << 12)
#define	 CAUSEB_IP5		13
#define	 CAUSEF_IP5		(_ULCAST_(1)   << 13)
#define	 CAUSEB_IP6		14
#define	 CAUSEF_IP6		(_ULCAST_(1)   << 14)
#define	 CAUSEB_IP7		15
#define	 CAUSEF_IP7		(_ULCAST_(1)   << 15)
#define	 CAUSEB_IV		23
#define	 CAUSEF_IV		(_ULCAST_(1)   << 23)
#define	 CAUSEB_PCI		26
#define	 CAUSEF_PCI		(_ULCAST_(1)   << 26)
#define	 CAUSEB_CE		28
#define	 CAUSEF_CE		(_ULCAST_(3)   << 28)
#define	 CAUSEB_TI		30
#define	 CAUSEF_TI		(_ULCAST_(1)   << 30)
#define	 CAUSEB_BD		31
#define	 CAUSEF_BD		(_ULCAST_(1)   << 31)

/*
 * Bits in the coprocessor 0 config register.
 */
/* Generic bits.  */
#define CONF_CM_CACHABLE_NO_WA		0
#define CONF_CM_CACHABLE_WA		1
#define CONF_CM_UNCACHED		2
#define CONF_CM_CACHABLE_NONCOHERENT	3
#define CONF_CM_CACHABLE_CE		4
#define CONF_CM_CACHABLE_COW		5
#define CONF_CM_CACHABLE_CUW		6
#define CONF_CM_CACHABLE_ACCELERATED	7
#define CONF_CM_CMASK			7
#define CONF_BE			(_ULCAST_(1) << 15)

/* Bits common to various processors.  */
#define CONF_CU			(_ULCAST_(1) <<	 3)
#define CONF_DB			(_ULCAST_(1) <<	 4)
#define CONF_IB			(_ULCAST_(1) <<	 5)
#define CONF_DC			(_ULCAST_(7) <<	 6)
#define CONF_IC			(_ULCAST_(7) <<	 9)
#define CONF_EB			(_ULCAST_(1) << 13)
#define CONF_EM			(_ULCAST_(1) << 14)
#define CONF_SM			(_ULCAST_(1) << 16)
#define CONF_SC			(_ULCAST_(1) << 17)
#define CONF_EW			(_ULCAST_(3) << 18)
#define CONF_EP			(_ULCAST_(15)<< 24)
#define CONF_EC			(_ULCAST_(7) << 28)
#define CONF_CM			(_ULCAST_(1) << 31)

/* Bits specific to the R4xx0.	*/
#define R4K_CONF_SW		(_ULCAST_(1) << 20)
#define R4K_CONF_SS		(_ULCAST_(1) << 21)
#define R4K_CONF_SB		(_ULCAST_(3) << 22)

/* Bits specific to the R5000.	*/
#define R5K_CONF_SE		(_ULCAST_(1) << 12)
#define R5K_CONF_SS		(_ULCAST_(3) << 20)

/* Bits specific to the RM7000.	 */
#define RM7K_CONF_SE		(_ULCAST_(1) <<	 3)
#define RM7K_CONF_TE		(_ULCAST_(1) << 12)
#define RM7K_CONF_CLK		(_ULCAST_(1) << 16)
#define RM7K_CONF_TC		(_ULCAST_(1) << 17)
#define RM7K_CONF_SI		(_ULCAST_(3) << 20)
#define RM7K_CONF_SC		(_ULCAST_(1) << 31)

/* Bits specific to the R10000.	 */
#define R10K_CONF_DN		(_ULCAST_(3) <<	 3)
#define R10K_CONF_CT		(_ULCAST_(1) <<	 5)
#define R10K_CONF_PE		(_ULCAST_(1) <<	 6)
#define R10K_CONF_PM		(_ULCAST_(3) <<	 7)
#define R10K_CONF_EC		(_ULCAST_(15)<<	 9)
#define R10K_CONF_SB		(_ULCAST_(1) << 13)
#define R10K_CONF_SK		(_ULCAST_(1) << 14)
#define R10K_CONF_SS		(_ULCAST_(7) << 16)
#define R10K_CONF_SC		(_ULCAST_(7) << 19)
#define R10K_CONF_DC		(_ULCAST_(7) << 26)
#define R10K_CONF_IC		(_ULCAST_(7) << 29)

/* Bits specific to the VR41xx.	 */
#define VR41_CONF_CS		(_ULCAST_(1) << 12)
#define VR41_CONF_P4K		(_ULCAST_(1) << 13)
#define VR41_CONF_BP		(_ULCAST_(1) << 16)
#define VR41_CONF_M16		(_ULCAST_(1) << 20)
#define VR41_CONF_AD		(_ULCAST_(1) << 23)

/* Bits specific to the R30xx.	*/
#define R30XX_CONF_FDM		(_ULCAST_(1) << 19)
#define R30XX_CONF_REV		(_ULCAST_(1) << 22)
#define R30XX_CONF_AC		(_ULCAST_(1) << 23)
#define R30XX_CONF_RF		(_ULCAST_(1) << 24)
#define R30XX_CONF_HALT		(_ULCAST_(1) << 25)
#define R30XX_CONF_FPINT	(_ULCAST_(7) << 26)
#define R30XX_CONF_DBR		(_ULCAST_(1) << 29)
#define R30XX_CONF_SB		(_ULCAST_(1) << 30)
#define R30XX_CONF_LOCK		(_ULCAST_(1) << 31)

/* Bits specific to the TX49.  */
#define TX49_CONF_DC		(_ULCAST_(1) << 16)
#define TX49_CONF_IC		(_ULCAST_(1) << 17)  /* conflict with CONF_SC */
#define TX49_CONF_HALT		(_ULCAST_(1) << 18)
#define TX49_CONF_CWFON		(_ULCAST_(1) << 27)

/* Bits specific to the MIPS32/64 PRA.	*/
#define MIPS_CONF_MT		(_ULCAST_(7) <<	 7)
#define MIPS_CONF_AR		(_ULCAST_(7) << 10)
#define MIPS_CONF_AT		(_ULCAST_(3) << 13)
#define MIPS_CONF_M		(_ULCAST_(1) << 31)

/*
 * Bits in the MIPS32/64 PRA coprocessor 0 config registers 1 and above.
 */
#define MIPS_CONF1_FP		(_ULCAST_(1) <<	 0)
#define MIPS_CONF1_EP		(_ULCAST_(1) <<	 1)
#define MIPS_CONF1_CA		(_ULCAST_(1) <<	 2)
#define MIPS_CONF1_WR		(_ULCAST_(1) <<	 3)
#define MIPS_CONF1_PC		(_ULCAST_(1) <<	 4)
#define MIPS_CONF1_MD		(_ULCAST_(1) <<	 5)
#define MIPS_CONF1_C2		(_ULCAST_(1) <<	 6)
#define MIPS_CONF1_DA		(_ULCAST_(7) <<	 7)
#define MIPS_CONF1_DL		(_ULCAST_(7) << 10)
#define MIPS_CONF1_DS		(_ULCAST_(7) << 13)
#define MIPS_CONF1_IA		(_ULCAST_(7) << 16)
#define MIPS_CONF1_IL		(_ULCAST_(7) << 19)
#define MIPS_CONF1_IS		(_ULCAST_(7) << 22)
#define MIPS_CONF1_TLBS		(_ULCAST_(63)<< 25)

#define MIPS_CONF2_SA		(_ULCAST_(15)<<	 0)
#define MIPS_CONF2_SL		(_ULCAST_(15)<<	 4)
#define MIPS_CONF2_SS		(_ULCAST_(15)<<	 8)
#define MIPS_CONF2_SU		(_ULCAST_(15)<< 12)
#define MIPS_CONF2_TA		(_ULCAST_(15)<< 16)
#define MIPS_CONF2_TL		(_ULCAST_(15)<< 20)
#define MIPS_CONF2_TS		(_ULCAST_(15)<< 24)
#define MIPS_CONF2_TU		(_ULCAST_(7) << 28)

#define MIPS_CONF3_TL		(_ULCAST_(1) <<	 0)
#define MIPS_CONF3_SM		(_ULCAST_(1) <<	 1)
#define MIPS_CONF3_MT		(_ULCAST_(1) <<	 2)
#define MIPS_CONF3_SP		(_ULCAST_(1) <<	 4)
#define MIPS_CONF3_VINT		(_ULCAST_(1) <<	 5)
#define MIPS_CONF3_VEIC		(_ULCAST_(1) <<	 6)
#define MIPS_CONF3_LPA		(_ULCAST_(1) <<	 7)
#define MIPS_CONF3_DSP		(_ULCAST_(1) << 10)
#define MIPS_CONF3_DSP2P	(_ULCAST_(1) << 11)
#define MIPS_CONF3_RXI		(_ULCAST_(1) << 12)
#define MIPS_CONF3_ULRI		(_ULCAST_(1) << 13)
#define MIPS_CONF3_ISA		(_ULCAST_(3) << 14)
#define MIPS_CONF3_VZ		(_ULCAST_(1) << 23)

#define MIPS_CONF4_MMUSIZEEXT	(_ULCAST_(255) << 0)
#define MIPS_CONF4_MMUEXTDEF	(_ULCAST_(3) << 14)
#define MIPS_CONF4_MMUEXTDEF_MMUSIZEEXT (_ULCAST_(1) << 14)

#define MIPS_CONF6_SYND		(_ULCAST_(1) << 13)

#define MIPS_CONF7_WII		(_ULCAST_(1) << 31)

#define MIPS_CONF7_RPS		(_ULCAST_(1) << 2)


/*
 * Bits in the MIPS32/64 coprocessor 1 (FPU) revision register.
 */
#define MIPS_FPIR_S		(_ULCAST_(1) << 16)
#define MIPS_FPIR_D		(_ULCAST_(1) << 17)
#define MIPS_FPIR_PS		(_ULCAST_(1) << 18)
#define MIPS_FPIR_3D		(_ULCAST_(1) << 19)
#define MIPS_FPIR_W		(_ULCAST_(1) << 20)
#define MIPS_FPIR_L		(_ULCAST_(1) << 21)
#define MIPS_FPIR_F64		(_ULCAST_(1) << 22)

#ifndef __ASSEMBLY__

/*
 * Functions to access the R10000 performance counters.	 These are basically
 * mfc0 and mtc0 instructions from and to coprocessor register with a 5-bit
 * performance counter number encoded into bits 1 ... 5 of the instruction.
 * Only performance counters 0 to 1 actually exist, so for a non-R10000 aware
 * disassembler these will look like an access to sel 0 or 1.
 */
#define read_r10k_perf_cntr(counter)				\
({								\
	unsigned int __res;					\
	__asm__ __volatile__(					\
	"mfpc\t%0, %1"						\
	: "=r" (__res)						\
	: "i" (counter));					\
								\
	__res;							\
})

#define write_r10k_perf_cntr(counter,val)			\
do {								\
	__asm__ __volatile__(					\
	"mtpc\t%0, %1"						\
	:							\
	: "r" (val), "i" (counter));				\
} while (0)

#define read_r10k_perf_event(counter)				\
({								\
	unsigned int __res;					\
	__asm__ __volatile__(					\
	"mfps\t%0, %1"						\
	: "=r" (__res)						\
	: "i" (counter));					\
								\
	__res;							\
})

#define write_r10k_perf_cntl(counter,val)			\
do {								\
	__asm__ __volatile__(					\
	"mtps\t%0, %1"						\
	:							\
	: "r" (val), "i" (counter));				\
} while (0)


/*
 * Macros to access the system control coprocessor
 */

#define __read_32bit_c0_register(source, sel)				\
({ int __res;								\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			"mfc0\t%0, " #source "\n\t"			\
			: "=r" (__res));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips32\n\t"				\
			"mfc0\t%0, " #source ", " #sel "\n\t"		\
			".set\tmips0\n\t"				\
			: "=r" (__res));				\
	__res;								\
})

#define __read_64bit_c0_register(source, sel)				\
({ unsigned long long __res;						\
	if (sizeof(unsigned long) == 4)					\
		__res = __read_64bit_c0_split(source, sel);		\
	else if (sel == 0)						\
		__asm__ __volatile__(					\
			".set\tmips3\n\t"				\
			"dmfc0\t%0, " #source "\n\t"			\
			".set\tmips0"					\
			: "=r" (__res));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmfc0\t%0, " #source ", " #sel "\n\t"		\
			".set\tmips0"					\
			: "=r" (__res));				\
	__res;								\
})

#define __write_32bit_c0_register(register, sel, value)			\
do {									\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			"mtc0\t%z0, " #register "\n\t"			\
			: : "Jr" ((unsigned int)(value)));		\
	else								\
		__asm__ __volatile__(					\
			".set\tmips32\n\t"				\
			"mtc0\t%z0, " #register ", " #sel "\n\t"	\
			".set\tmips0"					\
			: : "Jr" ((unsigned int)(value)));		\
} while (0)

#define __write_64bit_c0_register(register, sel, value)			\
do {									\
	if (sizeof(unsigned long) == 4)					\
		__write_64bit_c0_split(register, sel, value);		\
	else if (sel == 0)						\
		__asm__ __volatile__(					\
			".set\tmips3\n\t"				\
			"dmtc0\t%z0, " #register "\n\t"			\
			".set\tmips0"					\
			: : "Jr" (value));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmtc0\t%z0, " #register ", " #sel "\n\t"	\
			".set\tmips0"					\
			: : "Jr" (value));				\
} while (0)

#define __read_ulong_c0_register(reg, sel)				\
	((sizeof(unsigned long) == 4) ?					\
	(unsigned long) __read_32bit_c0_register(reg, sel) :		\
	(unsigned long) __read_64bit_c0_register(reg, sel))

#define __write_ulong_c0_register(reg, sel, val)			\
do {									\
	if (sizeof(unsigned long) == 4)					\
		__write_32bit_c0_register(reg, sel, val);		\
	else								\
		__write_64bit_c0_register(reg, sel, val);		\
} while (0)

/*
 * On RM7000/RM9000 these are uses to access cop0 set 1 registers
 */
#define __read_32bit_c0_ctrl_register(source)				\
({ int __res;								\
	__asm__ __volatile__(						\
		"cfc0\t%0, " #source "\n\t"				\
		: "=r" (__res));					\
	__res;								\
})

#define __write_32bit_c0_ctrl_register(register, value)			\
do {									\
	__asm__ __volatile__(						\
		"ctc0\t%z0, " #register "\n\t"				\
		: : "Jr" ((unsigned int)(value)));			\
} while (0)

/*
 * These versions are only needed for systems with more than 38 bits of
 * physical address space running the 32-bit kernel.  That's none atm :-)
 */
#define __read_64bit_c0_split(source, sel)				\
({									\
	unsigned long long __val;					\
	unsigned long __flags;						\
									\
	local_irq_save(__flags);					\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmfc0\t%M0, " #source "\n\t"			\
			"dsll\t%L0, %M0, 32\n\t"			\
			"dsra\t%M0, %M0, 32\n\t"			\
			"dsra\t%L0, %L0, 32\n\t"			\
			".set\tmips0"					\
			: "=r" (__val));				\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dmfc0\t%M0, " #source ", " #sel "\n\t"		\
			"dsll\t%L0, %M0, 32\n\t"			\
			"dsra\t%M0, %M0, 32\n\t"			\
			"dsra\t%L0, %L0, 32\n\t"			\
			".set\tmips0"					\
			: "=r" (__val));				\
	local_irq_restore(__flags);					\
									\
	__val;								\
})

#define __write_64bit_c0_split(source, sel, val)			\
do {									\
	unsigned long __flags;						\
									\
	local_irq_save(__flags);					\
	if (sel == 0)							\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dsll\t%L0, %L0, 32\n\t"			\
			"dsrl\t%L0, %L0, 32\n\t"			\
			"dsll\t%M0, %M0, 32\n\t"			\
			"or\t%L0, %L0, %M0\n\t"				\
			"dmtc0\t%L0, " #source "\n\t"			\
			".set\tmips0"					\
			: : "r" (val));					\
	else								\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dsll\t%L0, %L0, 32\n\t"			\
			"dsrl\t%L0, %L0, 32\n\t"			\
			"dsll\t%M0, %M0, 32\n\t"			\
			"or\t%L0, %L0, %M0\n\t"				\
			"dmtc0\t%L0, " #source ", " #sel "\n\t"		\
			".set\tmips0"					\
			: : "r" (val));					\
	local_irq_restore(__flags);					\
} while (0)

#define read_c0_index()		__read_32bit_c0_register($0, 0)
#define write_c0_index(val)	__write_32bit_c0_register($0, 0, val)

#define read_c0_random()	__read_32bit_c0_register($1, 0)
#define write_c0_random(val)	__write_32bit_c0_register($1, 0, val)

#define read_c0_entrylo0()	__read_ulong_c0_register($2, 0)
#define write_c0_entrylo0(val)	__write_ulong_c0_register($2, 0, val)

#define read_c0_entrylo1()	__read_ulong_c0_register($3, 0)
#define write_c0_entrylo1(val)	__write_ulong_c0_register($3, 0, val)

#define read_c0_conf()		__read_32bit_c0_register($3, 0)
#define write_c0_conf(val)	__write_32bit_c0_register($3, 0, val)

#define read_c0_context()	__read_ulong_c0_register($4, 0)
#define write_c0_context(val)	__write_ulong_c0_register($4, 0, val)

#define read_c0_userlocal()	__read_ulong_c0_register($4, 2)
#define write_c0_userlocal(val) __write_ulong_c0_register($4, 2, val)

#define read_c0_pagemask()	__read_32bit_c0_register($5, 0)
#define write_c0_pagemask(val)	__write_32bit_c0_register($5, 0, val)

#define read_c0_pagegrain()	__read_32bit_c0_register($5, 1)
#define write_c0_pagegrain(val) __write_32bit_c0_register($5, 1, val)

#define read_c0_wired()		__read_32bit_c0_register($6, 0)
#define write_c0_wired(val)	__write_32bit_c0_register($6, 0, val)

#define read_c0_info()		__read_32bit_c0_register($7, 0)

#define read_c0_cache()		__read_32bit_c0_register($7, 0) /* TX39xx */
#define write_c0_cache(val)	__write_32bit_c0_register($7, 0, val)

#define read_c0_badvaddr()	__read_ulong_c0_register($8, 0)
#define write_c0_badvaddr(val)	__write_ulong_c0_register($8, 0, val)

#define read_c0_count()		__read_32bit_c0_register($9, 0)
#define write_c0_count(val)	__write_32bit_c0_register($9, 0, val)

#define read_c0_count2()	__read_32bit_c0_register($9, 6) /* pnx8550 */
#define write_c0_count2(val)	__write_32bit_c0_register($9, 6, val)

#define read_c0_count3()	__read_32bit_c0_register($9, 7) /* pnx8550 */
#define write_c0_count3(val)	__write_32bit_c0_register($9, 7, val)

#define read_c0_entryhi()	__read_ulong_c0_register($10, 0)
#define write_c0_entryhi(val)	__write_ulong_c0_register($10, 0, val)

#define read_c0_compare()	__read_32bit_c0_register($11, 0)
#define write_c0_compare(val)	__write_32bit_c0_register($11, 0, val)

#define read_c0_compare2()	__read_32bit_c0_register($11, 6) /* pnx8550 */
#define write_c0_compare2(val)	__write_32bit_c0_register($11, 6, val)

#define read_c0_compare3()	__read_32bit_c0_register($11, 7) /* pnx8550 */
#define write_c0_compare3(val)	__write_32bit_c0_register($11, 7, val)

#define read_c0_status()	__read_32bit_c0_register($12, 0)
#ifdef CONFIG_MIPS_MT_SMTC
#define write_c0_status(val)						\
do {									\
	__write_32bit_c0_register($12, 0, val);				\
	__ehb();							\
} while (0)
#else
/*
 * Legacy non-SMTC code, which may be hazardous
 * but which might not support EHB
 */
#define write_c0_status(val)	__write_32bit_c0_register($12, 0, val)
#endif /* CONFIG_MIPS_MT_SMTC */

#define read_c0_cause()		__read_32bit_c0_register($13, 0)
#define write_c0_cause(val)	__write_32bit_c0_register($13, 0, val)

#define read_c0_epc()		__read_ulong_c0_register($14, 0)
#define write_c0_epc(val)	__write_ulong_c0_register($14, 0, val)

#define read_c0_prid()		__read_32bit_c0_register($15, 0)

#define read_c0_config()	__read_32bit_c0_register($16, 0)
#define read_c0_config1()	__read_32bit_c0_register($16, 1)
#define read_c0_config2()	__read_32bit_c0_register($16, 2)
#define read_c0_config3()	__read_32bit_c0_register($16, 3)
#define read_c0_config4()	__read_32bit_c0_register($16, 4)
#define read_c0_config5()	__read_32bit_c0_register($16, 5)
#define read_c0_config6()	__read_32bit_c0_register($16, 6)
#define read_c0_config7()	__read_32bit_c0_register($16, 7)
#define write_c0_config(val)	__write_32bit_c0_register($16, 0, val)
#define write_c0_config1(val)	__write_32bit_c0_register($16, 1, val)
#define write_c0_config2(val)	__write_32bit_c0_register($16, 2, val)
#define write_c0_config3(val)	__write_32bit_c0_register($16, 3, val)
#define write_c0_config4(val)	__write_32bit_c0_register($16, 4, val)
#define write_c0_config5(val)	__write_32bit_c0_register($16, 5, val)
#define write_c0_config6(val)	__write_32bit_c0_register($16, 6, val)
#define write_c0_config7(val)	__write_32bit_c0_register($16, 7, val)

/*
 * The WatchLo register.  There may be up to 8 of them.
 */
#define read_c0_watchlo0()	__read_ulong_c0_register($18, 0)
#define read_c0_watchlo1()	__read_ulong_c0_register($18, 1)
#define read_c0_watchlo2()	__read_ulong_c0_register($18, 2)
#define read_c0_watchlo3()	__read_ulong_c0_register($18, 3)
#define read_c0_watchlo4()	__read_ulong_c0_register($18, 4)
#define read_c0_watchlo5()	__read_ulong_c0_register($18, 5)
#define read_c0_watchlo6()	__read_ulong_c0_register($18, 6)
#define read_c0_watchlo7()	__read_ulong_c0_register($18, 7)
#define write_c0_watchlo0(val)	__write_ulong_c0_register($18, 0, val)
#define write_c0_watchlo1(val)	__write_ulong_c0_register($18, 1, val)
#define write_c0_watchlo2(val)	__write_ulong_c0_register($18, 2, val)
#define write_c0_watchlo3(val)	__write_ulong_c0_register($18, 3, val)
#define write_c0_watchlo4(val)	__write_ulong_c0_register($18, 4, val)
#define write_c0_watchlo5(val)	__write_ulong_c0_register($18, 5, val)
#define write_c0_watchlo6(val)	__write_ulong_c0_register($18, 6, val)
#define write_c0_watchlo7(val)	__write_ulong_c0_register($18, 7, val)

/*
 * The WatchHi register.  There may be up to 8 of them.
 */
#define read_c0_watchhi0()	__read_32bit_c0_register($19, 0)
#define read_c0_watchhi1()	__read_32bit_c0_register($19, 1)
#define read_c0_watchhi2()	__read_32bit_c0_register($19, 2)
#define read_c0_watchhi3()	__read_32bit_c0_register($19, 3)
#define read_c0_watchhi4()	__read_32bit_c0_register($19, 4)
#define read_c0_watchhi5()	__read_32bit_c0_register($19, 5)
#define read_c0_watchhi6()	__read_32bit_c0_register($19, 6)
#define read_c0_watchhi7()	__read_32bit_c0_register($19, 7)

#define write_c0_watchhi0(val)	__write_32bit_c0_register($19, 0, val)
#define write_c0_watchhi1(val)	__write_32bit_c0_register($19, 1, val)
#define write_c0_watchhi2(val)	__write_32bit_c0_register($19, 2, val)
#define write_c0_watchhi3(val)	__write_32bit_c0_register($19, 3, val)
#define write_c0_watchhi4(val)	__write_32bit_c0_register($19, 4, val)
#define write_c0_watchhi5(val)	__write_32bit_c0_register($19, 5, val)
#define write_c0_watchhi6(val)	__write_32bit_c0_register($19, 6, val)
#define write_c0_watchhi7(val)	__write_32bit_c0_register($19, 7, val)

#define read_c0_xcontext()	__read_ulong_c0_register($20, 0)
#define write_c0_xcontext(val)	__write_ulong_c0_register($20, 0, val)

#define read_c0_intcontrol()	__read_32bit_c0_ctrl_register($20)
#define write_c0_intcontrol(val) __write_32bit_c0_ctrl_register($20, val)

#define read_c0_framemask()	__read_32bit_c0_register($21, 0)
#define write_c0_framemask(val) __write_32bit_c0_register($21, 0, val)

#define read_c0_diag()		__read_32bit_c0_register($22, 0)
#define write_c0_diag(val)	__write_32bit_c0_register($22, 0, val)

#define read_c0_diag1()		__read_32bit_c0_register($22, 1)
#define write_c0_diag1(val)	__write_32bit_c0_register($22, 1, val)

#define read_c0_diag2()		__read_32bit_c0_register($22, 2)
#define write_c0_diag2(val)	__write_32bit_c0_register($22, 2, val)

#define read_c0_diag3()		__read_32bit_c0_register($22, 3)
#define write_c0_diag3(val)	__write_32bit_c0_register($22, 3, val)

#define read_c0_diag4()		__read_32bit_c0_register($22, 4)
#define write_c0_diag4(val)	__write_32bit_c0_register($22, 4, val)

#define read_c0_diag5()		__read_32bit_c0_register($22, 5)
#define write_c0_diag5(val)	__write_32bit_c0_register($22, 5, val)

#define read_c0_debug()		__read_32bit_c0_register($23, 0)
#define write_c0_debug(val)	__write_32bit_c0_register($23, 0, val)

#define read_c0_depc()		__read_ulong_c0_register($24, 0)
#define write_c0_depc(val)	__write_ulong_c0_register($24, 0, val)

/*
 * MIPS32 / MIPS64 performance counters
 */
#define read_c0_perfctrl0()	__read_32bit_c0_register($25, 0)
#define write_c0_perfctrl0(val) __write_32bit_c0_register($25, 0, val)
#define read_c0_perfcntr0()	__read_32bit_c0_register($25, 1)
#define write_c0_perfcntr0(val) __write_32bit_c0_register($25, 1, val)
#define read_c0_perfcntr0_64()	__read_64bit_c0_register($25, 1)
#define write_c0_perfcntr0_64(val) __write_64bit_c0_register($25, 1, val)
#define read_c0_perfctrl1()	__read_32bit_c0_register($25, 2)
#define write_c0_perfctrl1(val) __write_32bit_c0_register($25, 2, val)
#define read_c0_perfcntr1()	__read_32bit_c0_register($25, 3)
#define write_c0_perfcntr1(val) __write_32bit_c0_register($25, 3, val)
#define read_c0_perfcntr1_64()	__read_64bit_c0_register($25, 3)
#define write_c0_perfcntr1_64(val) __write_64bit_c0_register($25, 3, val)
#define read_c0_perfctrl2()	__read_32bit_c0_register($25, 4)
#define write_c0_perfctrl2(val) __write_32bit_c0_register($25, 4, val)
#define read_c0_perfcntr2()	__read_32bit_c0_register($25, 5)
#define write_c0_perfcntr2(val) __write_32bit_c0_register($25, 5, val)
#define read_c0_perfcntr2_64()	__read_64bit_c0_register($25, 5)
#define write_c0_perfcntr2_64(val) __write_64bit_c0_register($25, 5, val)
#define read_c0_perfctrl3()	__read_32bit_c0_register($25, 6)
#define write_c0_perfctrl3(val) __write_32bit_c0_register($25, 6, val)
#define read_c0_perfcntr3()	__read_32bit_c0_register($25, 7)
#define write_c0_perfcntr3(val) __write_32bit_c0_register($25, 7, val)
#define read_c0_perfcntr3_64()	__read_64bit_c0_register($25, 7)
#define write_c0_perfcntr3_64(val) __write_64bit_c0_register($25, 7, val)

#define read_c0_ecc()		__read_32bit_c0_register($26, 0)
#define write_c0_ecc(val)	__write_32bit_c0_register($26, 0, val)

#define read_c0_derraddr0()	__read_ulong_c0_register($26, 1)
#define write_c0_derraddr0(val) __write_ulong_c0_register($26, 1, val)

#define read_c0_cacheerr()	__read_32bit_c0_register($27, 0)

#define read_c0_derraddr1()	__read_ulong_c0_register($27, 1)
#define write_c0_derraddr1(val) __write_ulong_c0_register($27, 1, val)

#define read_c0_taglo()		__read_32bit_c0_register($28, 0)
#define write_c0_taglo(val)	__write_32bit_c0_register($28, 0, val)

#define read_c0_dtaglo()	__read_32bit_c0_register($28, 2)
#define write_c0_dtaglo(val)	__write_32bit_c0_register($28, 2, val)

#define read_c0_ddatalo()	__read_32bit_c0_register($28, 3)
#define write_c0_ddatalo(val)	__write_32bit_c0_register($28, 3, val)

#define read_c0_staglo()	__read_32bit_c0_register($28, 4)
#define write_c0_staglo(val)	__write_32bit_c0_register($28, 4, val)

#define read_c0_taghi()		__read_32bit_c0_register($29, 0)
#define write_c0_taghi(val)	__write_32bit_c0_register($29, 0, val)

#define read_c0_errorepc()	__read_ulong_c0_register($30, 0)
#define write_c0_errorepc(val)	__write_ulong_c0_register($30, 0, val)

/* MIPSR2 */
#define read_c0_hwrena()	__read_32bit_c0_register($7, 0)
#define write_c0_hwrena(val)	__write_32bit_c0_register($7, 0, val)

#define read_c0_intctl()	__read_32bit_c0_register($12, 1)
#define write_c0_intctl(val)	__write_32bit_c0_register($12, 1, val)

#define read_c0_srsctl()	__read_32bit_c0_register($12, 2)
#define write_c0_srsctl(val)	__write_32bit_c0_register($12, 2, val)

#define read_c0_srsmap()	__read_32bit_c0_register($12, 3)
#define write_c0_srsmap(val)	__write_32bit_c0_register($12, 3, val)

#define read_c0_ebase()		__read_32bit_c0_register($15, 1)
#define write_c0_ebase(val)	__write_32bit_c0_register($15, 1, val)


/* Cavium OCTEON (cnMIPS) */
#define read_c0_cvmcount()	__read_ulong_c0_register($9, 6)
#define write_c0_cvmcount(val)	__write_ulong_c0_register($9, 6, val)

#define read_c0_cvmctl()	__read_64bit_c0_register($9, 7)
#define write_c0_cvmctl(val)	__write_64bit_c0_register($9, 7, val)

#define read_c0_cvmmemctl()	__read_64bit_c0_register($11, 7)
#define write_c0_cvmmemctl(val) __write_64bit_c0_register($11, 7, val)
/*
 * The cacheerr registers are not standardized.	 On OCTEON, they are
 * 64 bits wide.
 */
#define read_octeon_c0_icacheerr()	__read_64bit_c0_register($27, 0)
#define write_octeon_c0_icacheerr(val)	__write_64bit_c0_register($27, 0, val)

#define read_octeon_c0_dcacheerr()	__read_64bit_c0_register($27, 1)
#define write_octeon_c0_dcacheerr(val)	__write_64bit_c0_register($27, 1, val)

/* BMIPS3300 */
#define read_c0_brcm_config_0()		__read_32bit_c0_register($22, 0)
#define write_c0_brcm_config_0(val)	__write_32bit_c0_register($22, 0, val)

#define read_c0_brcm_bus_pll()		__read_32bit_c0_register($22, 4)
#define write_c0_brcm_bus_pll(val)	__write_32bit_c0_register($22, 4, val)

#define read_c0_brcm_reset()		__read_32bit_c0_register($22, 5)
#define write_c0_brcm_reset(val)	__write_32bit_c0_register($22, 5, val)

/* BMIPS43xx */
#define read_c0_brcm_cmt_intr()		__read_32bit_c0_register($22, 1)
#define write_c0_brcm_cmt_intr(val)	__write_32bit_c0_register($22, 1, val)

#define read_c0_brcm_cmt_ctrl()		__read_32bit_c0_register($22, 2)
#define write_c0_brcm_cmt_ctrl(val)	__write_32bit_c0_register($22, 2, val)

#define read_c0_brcm_cmt_local()	__read_32bit_c0_register($22, 3)
#define write_c0_brcm_cmt_local(val)	__write_32bit_c0_register($22, 3, val)

#define read_c0_brcm_config_1()		__read_32bit_c0_register($22, 5)
#define write_c0_brcm_config_1(val)	__write_32bit_c0_register($22, 5, val)

#define read_c0_brcm_cbr()		__read_32bit_c0_register($22, 6)
#define write_c0_brcm_cbr(val)		__write_32bit_c0_register($22, 6, val)

/* BMIPS5000 */
#define read_c0_brcm_config()		__read_32bit_c0_register($22, 0)
#define write_c0_brcm_config(val)	__write_32bit_c0_register($22, 0, val)

#define read_c0_brcm_mode()		__read_32bit_c0_register($22, 1)
#define write_c0_brcm_mode(val)		__write_32bit_c0_register($22, 1, val)

#define read_c0_brcm_action()		__read_32bit_c0_register($22, 2)
#define write_c0_brcm_action(val)	__write_32bit_c0_register($22, 2, val)

#define read_c0_brcm_edsp()		__read_32bit_c0_register($22, 3)
#define write_c0_brcm_edsp(val)		__write_32bit_c0_register($22, 3, val)

#define read_c0_brcm_bootvec()		__read_32bit_c0_register($22, 4)
#define write_c0_brcm_bootvec(val)	__write_32bit_c0_register($22, 4, val)

#define read_c0_brcm_sleepcount()	__read_32bit_c0_register($22, 7)
#define write_c0_brcm_sleepcount(val)	__write_32bit_c0_register($22, 7, val)

/*
 * Macros to access the floating point coprocessor control registers
 */
#define read_32bit_cp1_register(source)					\
({									\
	int __res;							\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	reorder					\n"	\
	"	# gas fails to assemble cfc1 for some archs,	\n"	\
	"	# like Octeon.					\n"	\
	"	.set	mips1					\n"	\
	"	cfc1	%0,"STR(source)"			\n"	\
	"	.set	pop					\n"	\
	: "=r" (__res));						\
	__res;								\
})

#ifdef HAVE_AS_DSP
#define rddsp(mask)							\
({									\
	unsigned int __dspctl;						\
									\
	__asm__ __volatile__(						\
	"	rddsp	%0, %x1					\n"	\
	: "=r" (__dspctl)						\
	: "i" (mask));							\
	__dspctl;							\
})

#define wrdsp(val, mask)						\
do {									\
	__asm__ __volatile__(						\
	"	wrdsp	%0, %x1					\n"	\
	:								\
	: "r" (val), "i" (mask));					\
} while (0)

#define mflo0() ({ long mflo0; __asm__("mflo %0, $ac0" : "=r" (mflo0)); mflo0;})
#define mflo1() ({ long mflo1; __asm__("mflo %0, $ac1" : "=r" (mflo1)); mflo1;})
#define mflo2() ({ long mflo2; __asm__("mflo %0, $ac2" : "=r" (mflo2)); mflo2;})
#define mflo3() ({ long mflo3; __asm__("mflo %0, $ac3" : "=r" (mflo3)); mflo3;})

#define mfhi0() ({ long mfhi0; __asm__("mfhi %0, $ac0" : "=r" (mfhi0)); mfhi0;})
#define mfhi1() ({ long mfhi1; __asm__("mfhi %0, $ac1" : "=r" (mfhi1)); mfhi1;})
#define mfhi2() ({ long mfhi2; __asm__("mfhi %0, $ac2" : "=r" (mfhi2)); mfhi2;})
#define mfhi3() ({ long mfhi3; __asm__("mfhi %0, $ac3" : "=r" (mfhi3)); mfhi3;})

#define mtlo0(x) __asm__("mtlo %0, $ac0" ::"r" (x))
#define mtlo1(x) __asm__("mtlo %0, $ac1" ::"r" (x))
#define mtlo2(x) __asm__("mtlo %0, $ac2" ::"r" (x))
#define mtlo3(x) __asm__("mtlo %0, $ac3" ::"r" (x))

#define mthi0(x) __asm__("mthi %0, $ac0" ::"r" (x))
#define mthi1(x) __asm__("mthi %0, $ac1" ::"r" (x))
#define mthi2(x) __asm__("mthi %0, $ac2" ::"r" (x))
#define mthi3(x) __asm__("mthi %0, $ac3" ::"r" (x))

#else

#ifdef CONFIG_CPU_MICROMIPS
#define rddsp(mask)							\
({									\
	unsigned int __res;						\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	# rddsp $1, %x1					\n"	\
	"	.hword	((0x0020067c | (%x1 << 14)) >> 16)	\n"	\
	"	.hword	((0x0020067c | (%x1 << 14)) & 0xffff)	\n"	\
	"	move	%0, $1					\n"	\
	"	.set	pop					\n"	\
	: "=r" (__res)							\
	: "i" (mask));							\
	__res;								\
})

#define wrdsp(val, mask)						\
do {									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	move	$1, %0					\n"	\
	"	# wrdsp $1, %x1					\n"	\
	"	.hword	((0x0020167c | (%x1 << 14)) >> 16)	\n"	\
	"	.hword	((0x0020167c | (%x1 << 14)) & 0xffff)	\n"	\
	"	.set	pop					\n"	\
	:								\
	: "r" (val), "i" (mask));					\
} while (0)

#define _umips_dsp_mfxxx(ins)						\
({									\
	unsigned long __treg;						\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	.hword	0x0001					\n"	\
	"	.hword	%x1					\n"	\
	"	move	%0, $1					\n"	\
	"	.set	pop					\n"	\
	: "=r" (__treg)							\
	: "i" (ins));							\
	__treg;								\
})

#define _umips_dsp_mtxxx(val, ins)					\
do {									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	move	$1, %0					\n"	\
	"	.hword	0x0001					\n"	\
	"	.hword	%x1					\n"	\
	"	.set	pop					\n"	\
	:								\
	: "r" (val), "i" (ins));					\
} while (0)

#define _umips_dsp_mflo(reg) _umips_dsp_mfxxx((reg << 14) | 0x107c)
#define _umips_dsp_mfhi(reg) _umips_dsp_mfxxx((reg << 14) | 0x007c)

#define _umips_dsp_mtlo(val, reg) _umips_dsp_mtxxx(val, ((reg << 14) | 0x307c))
#define _umips_dsp_mthi(val, reg) _umips_dsp_mtxxx(val, ((reg << 14) | 0x207c))

#define mflo0() _umips_dsp_mflo(0)
#define mflo1() _umips_dsp_mflo(1)
#define mflo2() _umips_dsp_mflo(2)
#define mflo3() _umips_dsp_mflo(3)

#define mfhi0() _umips_dsp_mfhi(0)
#define mfhi1() _umips_dsp_mfhi(1)
#define mfhi2() _umips_dsp_mfhi(2)
#define mfhi3() _umips_dsp_mfhi(3)

#define mtlo0(x) _umips_dsp_mtlo(x, 0)
#define mtlo1(x) _umips_dsp_mtlo(x, 1)
#define mtlo2(x) _umips_dsp_mtlo(x, 2)
#define mtlo3(x) _umips_dsp_mtlo(x, 3)

#define mthi0(x) _umips_dsp_mthi(x, 0)
#define mthi1(x) _umips_dsp_mthi(x, 1)
#define mthi2(x) _umips_dsp_mthi(x, 2)
#define mthi3(x) _umips_dsp_mthi(x, 3)

#else  /* !CONFIG_CPU_MICROMIPS */
#define rddsp(mask)							\
({									\
	unsigned int __res;						\
									\
	__asm__ __volatile__(						\
	"	.set	push				\n"		\
	"	.set	noat				\n"		\
	"	# rddsp $1, %x1				\n"		\
	"	.word	0x7c000cb8 | (%x1 << 16)	\n"		\
	"	move	%0, $1				\n"		\
	"	.set	pop				\n"		\
	: "=r" (__res)							\
	: "i" (mask));							\
	__res;								\
})

#define wrdsp(val, mask)						\
do {									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	move	$1, %0					\n"	\
	"	# wrdsp $1, %x1					\n"	\
	"	.word	0x7c2004f8 | (%x1 << 11)		\n"	\
	"	.set	pop					\n"	\
        :								\
	: "r" (val), "i" (mask));					\
} while (0)

#define _dsp_mfxxx(ins)							\
({									\
	unsigned long __treg;						\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	.word	(0x00000810 | %1)			\n"	\
	"	move	%0, $1					\n"	\
	"	.set	pop					\n"	\
	: "=r" (__treg)							\
	: "i" (ins));							\
	__treg;								\
})

#define _dsp_mtxxx(val, ins)						\
do {									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	move	$1, %0					\n"	\
	"	.word	(0x00200011 | %1)			\n"	\
	"	.set	pop					\n"	\
	:								\
	: "r" (val), "i" (ins));					\
} while (0)

#define _dsp_mflo(reg) _dsp_mfxxx((reg << 21) | 0x0002)
#define _dsp_mfhi(reg) _dsp_mfxxx((reg << 21) | 0x0000)

#define _dsp_mtlo(val, reg) _dsp_mtxxx(val, ((reg << 11) | 0x0002))
#define _dsp_mthi(val, reg) _dsp_mtxxx(val, ((reg << 11) | 0x0000))

#define mflo0() _dsp_mflo(0)
#define mflo1() _dsp_mflo(1)
#define mflo2() _dsp_mflo(2)
#define mflo3() _dsp_mflo(3)

#define mfhi0() _dsp_mfhi(0)
#define mfhi1() _dsp_mfhi(1)
#define mfhi2() _dsp_mfhi(2)
#define mfhi3() _dsp_mfhi(3)

#define mtlo0(x) _dsp_mtlo(x, 0)
#define mtlo1(x) _dsp_mtlo(x, 1)
#define mtlo2(x) _dsp_mtlo(x, 2)
#define mtlo3(x) _dsp_mtlo(x, 3)

#define mthi0(x) _dsp_mthi(x, 0)
#define mthi1(x) _dsp_mthi(x, 1)
#define mthi2(x) _dsp_mthi(x, 2)
#define mthi3(x) _dsp_mthi(x, 3)

#endif /* CONFIG_CPU_MICROMIPS */
#endif

/*
 * TLB operations.
 *
 * It is responsibility of the caller to take care of any TLB hazards.
 */
static inline void tlb_probe(void)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbp\n\t"
		".set reorder");
}

static inline void tlb_read(void)
{
#if MIPS34K_MISSED_ITLB_WAR
	int res = 0;

	__asm__ __volatile__(
	"	.set	push					\n"
	"	.set	noreorder				\n"
	"	.set	noat					\n"
	"	.set	mips32r2				\n"
	"	.word	0x41610001		# dvpe $1	\n"
	"	move	%0, $1					\n"
	"	ehb						\n"
	"	.set	pop					\n"
	: "=r" (res));

	instruction_hazard();
#endif

	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbr\n\t"
		".set reorder");

#if MIPS34K_MISSED_ITLB_WAR
	if ((res & _ULCAST_(1)))
		__asm__ __volatile__(
		"	.set	push				\n"
		"	.set	noreorder			\n"
		"	.set	noat				\n"
		"	.set	mips32r2			\n"
		"	.word	0x41600021	# evpe		\n"
		"	ehb					\n"
		"	.set	pop				\n");
#endif
}

static inline void tlb_write_indexed(void)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbwi\n\t"
		".set reorder");
}

static inline void tlb_write_random(void)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbwr\n\t"
		".set reorder");
}

/*
 * Manipulate bits in a c0 register.
 */
#ifndef CONFIG_MIPS_MT_SMTC
/*
 * SMTC Linux requires shutting-down microthread scheduling
 * during CP0 register read-modify-write sequences.
 */
#define __BUILD_SET_C0(name)					\
static inline unsigned int					\
set_c0_##name(unsigned int set)					\
{								\
	unsigned int res, new;					\
								\
	res = read_c0_##name();					\
	new = res | set;					\
	write_c0_##name(new);					\
								\
	return res;						\
}								\
								\
static inline unsigned int					\
clear_c0_##name(unsigned int clear)				\
{								\
	unsigned int res, new;					\
								\
	res = read_c0_##name();					\
	new = res & ~clear;					\
	write_c0_##name(new);					\
								\
	return res;						\
}								\
								\
static inline unsigned int					\
change_c0_##name(unsigned int change, unsigned int val)		\
{								\
	unsigned int res, new;					\
								\
	res = read_c0_##name();					\
	new = res & ~change;					\
	new |= (val & change);					\
	write_c0_##name(new);					\
								\
	return res;						\
}

#else /* SMTC versions that manage MT scheduling */

#include <linux/irqflags.h>

/*
 * This is a duplicate of dmt() in mipsmtregs.h to avoid problems with
 * header file recursion.
 */
static inline unsigned int __dmt(void)
{
	int res;

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	mips32r2					\n"
	"	.set	noat						\n"
	"	.word	0x41610BC1			# dmt $1	\n"
	"	ehb							\n"
	"	move	%0, $1						\n"
	"	.set	pop						\n"
	: "=r" (res));

	instruction_hazard();

	return res;
}

#define __VPECONTROL_TE_SHIFT	15
#define __VPECONTROL_TE		(1UL << __VPECONTROL_TE_SHIFT)

#define __EMT_ENABLE		__VPECONTROL_TE

static inline void __emt(unsigned int previous)
{
	if ((previous & __EMT_ENABLE))
		__asm__ __volatile__(
		"	.set	mips32r2				\n"
		"	.word	0x41600be1		# emt		\n"
		"	ehb						\n"
		"	.set	mips0					\n");
}

static inline void __ehb(void)
{
	__asm__ __volatile__(
	"	.set	mips32r2					\n"
	"	ehb							\n"		"	.set	mips0						\n");
}

/*
 * Note that local_irq_save/restore affect TC-specific IXMT state,
 * not Status.IE as in non-SMTC kernel.
 */

#define __BUILD_SET_C0(name)					\
static inline unsigned int					\
set_c0_##name(unsigned int set)					\
{								\
	unsigned int res;					\
	unsigned int new;					\
	unsigned int omt;					\
	unsigned long flags;					\
								\
	local_irq_save(flags);					\
	omt = __dmt();						\
	res = read_c0_##name();					\
	new = res | set;					\
	write_c0_##name(new);					\
	__emt(omt);						\
	local_irq_restore(flags);				\
								\
	return res;						\
}								\
								\
static inline unsigned int					\
clear_c0_##name(unsigned int clear)				\
{								\
	unsigned int res;					\
	unsigned int new;					\
	unsigned int omt;					\
	unsigned long flags;					\
								\
	local_irq_save(flags);					\
	omt = __dmt();						\
	res = read_c0_##name();					\
	new = res & ~clear;					\
	write_c0_##name(new);					\
	__emt(omt);						\
	local_irq_restore(flags);				\
								\
	return res;						\
}								\
								\
static inline unsigned int					\
change_c0_##name(unsigned int change, unsigned int newbits)	\
{								\
	unsigned int res;					\
	unsigned int new;					\
	unsigned int omt;					\
	unsigned long flags;					\
								\
	local_irq_save(flags);					\
								\
	omt = __dmt();						\
	res = read_c0_##name();					\
	new = res & ~change;					\
	new |= (newbits & change);				\
	write_c0_##name(new);					\
	__emt(omt);						\
	local_irq_restore(flags);				\
								\
	return res;						\
}
#endif

__BUILD_SET_C0(status)
__BUILD_SET_C0(cause)
__BUILD_SET_C0(config)
__BUILD_SET_C0(intcontrol)
__BUILD_SET_C0(intctl)
__BUILD_SET_C0(srsmap)
__BUILD_SET_C0(brcm_config_0)
__BUILD_SET_C0(brcm_bus_pll)
__BUILD_SET_C0(brcm_reset)
__BUILD_SET_C0(brcm_cmt_intr)
__BUILD_SET_C0(brcm_cmt_ctrl)
__BUILD_SET_C0(brcm_config)
__BUILD_SET_C0(brcm_mode)

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_MIPSREGS_H */

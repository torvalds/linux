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
#include <linux/types.h>
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
#define CP0_SEGCTL0 $5, 2
#define CP0_SEGCTL1 $5, 3
#define CP0_SEGCTL2 $5, 4
#define CP0_WIRED $6
#define CP0_INFO $7
#define CP0_HWRENA $7, 0
#define CP0_BADVADDR $8
#define CP0_BADINSTR $8, 1
#define CP0_COUNT $9
#define CP0_ENTRYHI $10
#define CP0_GUESTCTL1 $10, 4
#define CP0_GUESTCTL2 $10, 5
#define CP0_GUESTCTL3 $10, 6
#define CP0_COMPARE $11
#define CP0_GUESTCTL0EXT $11, 4
#define CP0_STATUS $12
#define CP0_GUESTCTL0 $12, 6
#define CP0_GTOFFSET $12, 7
#define CP0_CAUSE $13
#define CP0_EPC $14
#define CP0_PRID $15
#define CP0_EBASE $15, 1
#define CP0_CMGCRBASE $15, 3
#define CP0_CONFIG $16
#define CP0_CONFIG3 $16, 3
#define CP0_CONFIG5 $16, 5
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


/* Generic EntryLo bit definitions */
#define ENTRYLO_G		(_ULCAST_(1) << 0)
#define ENTRYLO_V		(_ULCAST_(1) << 1)
#define ENTRYLO_D		(_ULCAST_(1) << 2)
#define ENTRYLO_C_SHIFT		3
#define ENTRYLO_C		(_ULCAST_(7) << ENTRYLO_C_SHIFT)

/* R3000 EntryLo bit definitions */
#define R3K_ENTRYLO_G		(_ULCAST_(1) << 8)
#define R3K_ENTRYLO_V		(_ULCAST_(1) << 9)
#define R3K_ENTRYLO_D		(_ULCAST_(1) << 10)
#define R3K_ENTRYLO_N		(_ULCAST_(1) << 11)

/* MIPS32/64 EntryLo bit definitions */
#define MIPS_ENTRYLO_PFN_SHIFT	6
#define MIPS_ENTRYLO_XI		(_ULCAST_(1) << (BITS_PER_LONG - 2))
#define MIPS_ENTRYLO_RI		(_ULCAST_(1) << (BITS_PER_LONG - 1))

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
#define PG_IEC		(_ULCAST_(1) <<  27)

/* MIPS32/64 EntryHI bit definitions */
#define MIPS_ENTRYHI_EHINV	(_ULCAST_(1) << 10)
#define MIPS_ENTRYHI_ASIDX	(_ULCAST_(0x3) << 8)
#define MIPS_ENTRYHI_ASID	(_ULCAST_(0xff) << 0)

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
 */
#define INTCTLB_IPFDC		23
#define INTCTLF_IPFDC		(_ULCAST_(7) << INTCTLB_IPFDC)
#define INTCTLB_IPPCI		26
#define INTCTLF_IPPCI		(_ULCAST_(7) << INTCTLB_IPPCI)
#define INTCTLB_IPTI		29
#define INTCTLF_IPTI		(_ULCAST_(7) << INTCTLB_IPTI)

/*
 * Bitfields and bit numbers in the coprocessor 0 cause register.
 *
 * Refer to your MIPS R4xx0 manual, chapter 5 for explanation.
 */
#define CAUSEB_EXCCODE		2
#define CAUSEF_EXCCODE		(_ULCAST_(31)  <<  2)
#define CAUSEB_IP		8
#define CAUSEF_IP		(_ULCAST_(255) <<  8)
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
#define CAUSEB_FDCI		21
#define CAUSEF_FDCI		(_ULCAST_(1)   << 21)
#define CAUSEB_WP		22
#define CAUSEF_WP		(_ULCAST_(1)   << 22)
#define CAUSEB_IV		23
#define CAUSEF_IV		(_ULCAST_(1)   << 23)
#define CAUSEB_PCI		26
#define CAUSEF_PCI		(_ULCAST_(1)   << 26)
#define CAUSEB_DC		27
#define CAUSEF_DC		(_ULCAST_(1)   << 27)
#define CAUSEB_CE		28
#define CAUSEF_CE		(_ULCAST_(3)   << 28)
#define CAUSEB_TI		30
#define CAUSEF_TI		(_ULCAST_(1)   << 30)
#define CAUSEB_BD		31
#define CAUSEF_BD		(_ULCAST_(1)   << 31)

/*
 * Cause.ExcCode trap codes.
 */
#define EXCCODE_INT		0	/* Interrupt pending */
#define EXCCODE_MOD		1	/* TLB modified fault */
#define EXCCODE_TLBL		2	/* TLB miss on load or ifetch */
#define EXCCODE_TLBS		3	/* TLB miss on a store */
#define EXCCODE_ADEL		4	/* Address error on a load or ifetch */
#define EXCCODE_ADES		5	/* Address error on a store */
#define EXCCODE_IBE		6	/* Bus error on an ifetch */
#define EXCCODE_DBE		7	/* Bus error on a load or store */
#define EXCCODE_SYS		8	/* System call */
#define EXCCODE_BP		9	/* Breakpoint */
#define EXCCODE_RI		10	/* Reserved instruction exception */
#define EXCCODE_CPU		11	/* Coprocessor unusable */
#define EXCCODE_OV		12	/* Arithmetic overflow */
#define EXCCODE_TR		13	/* Trap instruction */
#define EXCCODE_MSAFPE		14	/* MSA floating point exception */
#define EXCCODE_FPE		15	/* Floating point exception */
#define EXCCODE_TLBRI		19	/* TLB Read-Inhibit exception */
#define EXCCODE_TLBXI		20	/* TLB Execution-Inhibit exception */
#define EXCCODE_MSADIS		21	/* MSA disabled exception */
#define EXCCODE_MDMX		22	/* MDMX unusable exception */
#define EXCCODE_WATCH		23	/* Watch address reference */
#define EXCCODE_MCHECK		24	/* Machine check */
#define EXCCODE_THREAD		25	/* Thread exceptions (MT) */
#define EXCCODE_DSPDIS		26	/* DSP disabled exception */
#define EXCCODE_GE		27	/* Virtualized guest exception (VZ) */

/* Implementation specific trap codes used by MIPS cores */
#define MIPS_EXCCODE_TLBPAR	16	/* TLB parity error exception */

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
#define MIPS_CONF_MT_TLB	(_ULCAST_(1) <<  7)
#define MIPS_CONF_MT_FTLB	(_ULCAST_(4) <<  7)
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
#define MIPS_CONF1_DA_SHF	7
#define MIPS_CONF1_DA_SZ	3
#define MIPS_CONF1_DA		(_ULCAST_(7) <<	 7)
#define MIPS_CONF1_DL_SHF	10
#define MIPS_CONF1_DL_SZ	3
#define MIPS_CONF1_DL		(_ULCAST_(7) << 10)
#define MIPS_CONF1_DS_SHF	13
#define MIPS_CONF1_DS_SZ	3
#define MIPS_CONF1_DS		(_ULCAST_(7) << 13)
#define MIPS_CONF1_IA_SHF	16
#define MIPS_CONF1_IA_SZ	3
#define MIPS_CONF1_IA		(_ULCAST_(7) << 16)
#define MIPS_CONF1_IL_SHF	19
#define MIPS_CONF1_IL_SZ	3
#define MIPS_CONF1_IL		(_ULCAST_(7) << 19)
#define MIPS_CONF1_IS_SHF	22
#define MIPS_CONF1_IS_SZ	3
#define MIPS_CONF1_IS		(_ULCAST_(7) << 22)
#define MIPS_CONF1_TLBS_SHIFT   (25)
#define MIPS_CONF1_TLBS_SIZE    (6)
#define MIPS_CONF1_TLBS         (_ULCAST_(63) << MIPS_CONF1_TLBS_SHIFT)

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
#define MIPS_CONF3_CDMM		(_ULCAST_(1) <<	 3)
#define MIPS_CONF3_SP		(_ULCAST_(1) <<	 4)
#define MIPS_CONF3_VINT		(_ULCAST_(1) <<	 5)
#define MIPS_CONF3_VEIC		(_ULCAST_(1) <<	 6)
#define MIPS_CONF3_LPA		(_ULCAST_(1) <<	 7)
#define MIPS_CONF3_ITL		(_ULCAST_(1) <<	 8)
#define MIPS_CONF3_CTXTC	(_ULCAST_(1) <<	 9)
#define MIPS_CONF3_DSP		(_ULCAST_(1) << 10)
#define MIPS_CONF3_DSP2P	(_ULCAST_(1) << 11)
#define MIPS_CONF3_RXI		(_ULCAST_(1) << 12)
#define MIPS_CONF3_ULRI		(_ULCAST_(1) << 13)
#define MIPS_CONF3_ISA		(_ULCAST_(3) << 14)
#define MIPS_CONF3_ISA_OE	(_ULCAST_(1) << 16)
#define MIPS_CONF3_MCU		(_ULCAST_(1) << 17)
#define MIPS_CONF3_MMAR		(_ULCAST_(7) << 18)
#define MIPS_CONF3_IPLW		(_ULCAST_(3) << 21)
#define MIPS_CONF3_VZ		(_ULCAST_(1) << 23)
#define MIPS_CONF3_PW		(_ULCAST_(1) << 24)
#define MIPS_CONF3_SC		(_ULCAST_(1) << 25)
#define MIPS_CONF3_BI		(_ULCAST_(1) << 26)
#define MIPS_CONF3_BP		(_ULCAST_(1) << 27)
#define MIPS_CONF3_MSA		(_ULCAST_(1) << 28)
#define MIPS_CONF3_CMGCR	(_ULCAST_(1) << 29)
#define MIPS_CONF3_BPG		(_ULCAST_(1) << 30)

#define MIPS_CONF4_MMUSIZEEXT_SHIFT	(0)
#define MIPS_CONF4_MMUSIZEEXT	(_ULCAST_(255) << 0)
#define MIPS_CONF4_FTLBSETS_SHIFT	(0)
#define MIPS_CONF4_FTLBSETS	(_ULCAST_(15) << MIPS_CONF4_FTLBSETS_SHIFT)
#define MIPS_CONF4_FTLBWAYS_SHIFT	(4)
#define MIPS_CONF4_FTLBWAYS	(_ULCAST_(15) << MIPS_CONF4_FTLBWAYS_SHIFT)
#define MIPS_CONF4_FTLBPAGESIZE_SHIFT	(8)
/* bits 10:8 in FTLB-only configurations */
#define MIPS_CONF4_FTLBPAGESIZE (_ULCAST_(7) << MIPS_CONF4_FTLBPAGESIZE_SHIFT)
/* bits 12:8 in VTLB-FTLB only configurations */
#define MIPS_CONF4_VFTLBPAGESIZE (_ULCAST_(31) << MIPS_CONF4_FTLBPAGESIZE_SHIFT)
#define MIPS_CONF4_MMUEXTDEF	(_ULCAST_(3) << 14)
#define MIPS_CONF4_MMUEXTDEF_MMUSIZEEXT (_ULCAST_(1) << 14)
#define MIPS_CONF4_MMUEXTDEF_FTLBSIZEEXT	(_ULCAST_(2) << 14)
#define MIPS_CONF4_MMUEXTDEF_VTLBSIZEEXT	(_ULCAST_(3) << 14)
#define MIPS_CONF4_KSCREXIST_SHIFT	(16)
#define MIPS_CONF4_KSCREXIST	(_ULCAST_(255) << MIPS_CONF4_KSCREXIST_SHIFT)
#define MIPS_CONF4_VTLBSIZEEXT_SHIFT	(24)
#define MIPS_CONF4_VTLBSIZEEXT	(_ULCAST_(15) << MIPS_CONF4_VTLBSIZEEXT_SHIFT)
#define MIPS_CONF4_AE		(_ULCAST_(1) << 28)
#define MIPS_CONF4_IE		(_ULCAST_(3) << 29)
#define MIPS_CONF4_TLBINV	(_ULCAST_(2) << 29)

#define MIPS_CONF5_NF		(_ULCAST_(1) << 0)
#define MIPS_CONF5_UFR		(_ULCAST_(1) << 2)
#define MIPS_CONF5_MRP		(_ULCAST_(1) << 3)
#define MIPS_CONF5_LLB		(_ULCAST_(1) << 4)
#define MIPS_CONF5_MVH		(_ULCAST_(1) << 5)
#define MIPS_CONF5_VP		(_ULCAST_(1) << 7)
#define MIPS_CONF5_FRE		(_ULCAST_(1) << 8)
#define MIPS_CONF5_UFE		(_ULCAST_(1) << 9)
#define MIPS_CONF5_MSAEN	(_ULCAST_(1) << 27)
#define MIPS_CONF5_EVA		(_ULCAST_(1) << 28)
#define MIPS_CONF5_CV		(_ULCAST_(1) << 29)
#define MIPS_CONF5_K		(_ULCAST_(1) << 30)

#define MIPS_CONF6_SYND		(_ULCAST_(1) << 13)
/* proAptiv FTLB on/off bit */
#define MIPS_CONF6_FTLBEN	(_ULCAST_(1) << 15)
/* Loongson-3 FTLB on/off bit */
#define MIPS_CONF6_FTLBDIS	(_ULCAST_(1) << 22)
/* FTLB probability bits */
#define MIPS_CONF6_FTLBP_SHIFT	(16)

#define MIPS_CONF7_WII		(_ULCAST_(1) << 31)

#define MIPS_CONF7_RPS		(_ULCAST_(1) << 2)

#define MIPS_CONF7_IAR		(_ULCAST_(1) << 10)
#define MIPS_CONF7_AR		(_ULCAST_(1) << 16)
/* FTLB probability bits for R6 */
#define MIPS_CONF7_FTLBP_SHIFT	(18)

/* WatchLo* register definitions */
#define MIPS_WATCHLO_IRW	(_ULCAST_(0x7) << 0)

/* WatchHi* register definitions */
#define MIPS_WATCHHI_M		(_ULCAST_(1) << 31)
#define MIPS_WATCHHI_G		(_ULCAST_(1) << 30)
#define MIPS_WATCHHI_WM		(_ULCAST_(0x3) << 28)
#define MIPS_WATCHHI_WM_R_RVA	(_ULCAST_(0) << 28)
#define MIPS_WATCHHI_WM_R_GPA	(_ULCAST_(1) << 28)
#define MIPS_WATCHHI_WM_G_GVA	(_ULCAST_(2) << 28)
#define MIPS_WATCHHI_EAS	(_ULCAST_(0x3) << 24)
#define MIPS_WATCHHI_ASID	(_ULCAST_(0xff) << 16)
#define MIPS_WATCHHI_MASK	(_ULCAST_(0x1ff) << 3)
#define MIPS_WATCHHI_I		(_ULCAST_(1) << 2)
#define MIPS_WATCHHI_R		(_ULCAST_(1) << 1)
#define MIPS_WATCHHI_W		(_ULCAST_(1) << 0)
#define MIPS_WATCHHI_IRW	(_ULCAST_(0x7) << 0)

/* MAAR bit definitions */
#define MIPS_MAAR_ADDR		((BIT_ULL(BITS_PER_LONG - 12) - 1) << 12)
#define MIPS_MAAR_ADDR_SHIFT	12
#define MIPS_MAAR_S		(_ULCAST_(1) << 1)
#define MIPS_MAAR_V		(_ULCAST_(1) << 0)

/* EBase bit definitions */
#define MIPS_EBASE_CPUNUM_SHIFT	0
#define MIPS_EBASE_CPUNUM	(_ULCAST_(0x3ff) << 0)
#define MIPS_EBASE_WG_SHIFT	11
#define MIPS_EBASE_WG		(_ULCAST_(1) << 11)
#define MIPS_EBASE_BASE_SHIFT	12
#define MIPS_EBASE_BASE		(~_ULCAST_((1 << MIPS_EBASE_BASE_SHIFT) - 1))

/* CMGCRBase bit definitions */
#define MIPS_CMGCRB_BASE	11
#define MIPS_CMGCRF_BASE	(~_ULCAST_((1 << MIPS_CMGCRB_BASE) - 1))

/*
 * Bits in the MIPS32 Memory Segmentation registers.
 */
#define MIPS_SEGCFG_PA_SHIFT	9
#define MIPS_SEGCFG_PA		(_ULCAST_(127) << MIPS_SEGCFG_PA_SHIFT)
#define MIPS_SEGCFG_AM_SHIFT	4
#define MIPS_SEGCFG_AM		(_ULCAST_(7) << MIPS_SEGCFG_AM_SHIFT)
#define MIPS_SEGCFG_EU_SHIFT	3
#define MIPS_SEGCFG_EU		(_ULCAST_(1) << MIPS_SEGCFG_EU_SHIFT)
#define MIPS_SEGCFG_C_SHIFT	0
#define MIPS_SEGCFG_C		(_ULCAST_(7) << MIPS_SEGCFG_C_SHIFT)

#define MIPS_SEGCFG_UUSK	_ULCAST_(7)
#define MIPS_SEGCFG_USK		_ULCAST_(5)
#define MIPS_SEGCFG_MUSUK	_ULCAST_(4)
#define MIPS_SEGCFG_MUSK	_ULCAST_(3)
#define MIPS_SEGCFG_MSK		_ULCAST_(2)
#define MIPS_SEGCFG_MK		_ULCAST_(1)
#define MIPS_SEGCFG_UK		_ULCAST_(0)

#define MIPS_PWFIELD_GDI_SHIFT	24
#define MIPS_PWFIELD_GDI_MASK	0x3f000000
#define MIPS_PWFIELD_UDI_SHIFT	18
#define MIPS_PWFIELD_UDI_MASK	0x00fc0000
#define MIPS_PWFIELD_MDI_SHIFT	12
#define MIPS_PWFIELD_MDI_MASK	0x0003f000
#define MIPS_PWFIELD_PTI_SHIFT	6
#define MIPS_PWFIELD_PTI_MASK	0x00000fc0
#define MIPS_PWFIELD_PTEI_SHIFT	0
#define MIPS_PWFIELD_PTEI_MASK	0x0000003f

#define MIPS_PWSIZE_GDW_SHIFT	24
#define MIPS_PWSIZE_GDW_MASK	0x3f000000
#define MIPS_PWSIZE_UDW_SHIFT	18
#define MIPS_PWSIZE_UDW_MASK	0x00fc0000
#define MIPS_PWSIZE_MDW_SHIFT	12
#define MIPS_PWSIZE_MDW_MASK	0x0003f000
#define MIPS_PWSIZE_PTW_SHIFT	6
#define MIPS_PWSIZE_PTW_MASK	0x00000fc0
#define MIPS_PWSIZE_PTEW_SHIFT	0
#define MIPS_PWSIZE_PTEW_MASK	0x0000003f

#define MIPS_PWCTL_PWEN_SHIFT	31
#define MIPS_PWCTL_PWEN_MASK	0x80000000
#define MIPS_PWCTL_DPH_SHIFT	7
#define MIPS_PWCTL_DPH_MASK	0x00000080
#define MIPS_PWCTL_HUGEPG_SHIFT	6
#define MIPS_PWCTL_HUGEPG_MASK	0x00000060
#define MIPS_PWCTL_PSN_SHIFT	0
#define MIPS_PWCTL_PSN_MASK	0x0000003f

/* GuestCtl0 fields */
#define MIPS_GCTL0_GM_SHIFT	31
#define MIPS_GCTL0_GM		(_ULCAST_(1) << MIPS_GCTL0_GM_SHIFT)
#define MIPS_GCTL0_RI_SHIFT	30
#define MIPS_GCTL0_RI		(_ULCAST_(1) << MIPS_GCTL0_RI_SHIFT)
#define MIPS_GCTL0_MC_SHIFT	29
#define MIPS_GCTL0_MC		(_ULCAST_(1) << MIPS_GCTL0_MC_SHIFT)
#define MIPS_GCTL0_CP0_SHIFT	28
#define MIPS_GCTL0_CP0		(_ULCAST_(1) << MIPS_GCTL0_CP0_SHIFT)
#define MIPS_GCTL0_AT_SHIFT	26
#define MIPS_GCTL0_AT		(_ULCAST_(0x3) << MIPS_GCTL0_AT_SHIFT)
#define MIPS_GCTL0_GT_SHIFT	25
#define MIPS_GCTL0_GT		(_ULCAST_(1) << MIPS_GCTL0_GT_SHIFT)
#define MIPS_GCTL0_CG_SHIFT	24
#define MIPS_GCTL0_CG		(_ULCAST_(1) << MIPS_GCTL0_CG_SHIFT)
#define MIPS_GCTL0_CF_SHIFT	23
#define MIPS_GCTL0_CF		(_ULCAST_(1) << MIPS_GCTL0_CF_SHIFT)
#define MIPS_GCTL0_G1_SHIFT	22
#define MIPS_GCTL0_G1		(_ULCAST_(1) << MIPS_GCTL0_G1_SHIFT)
#define MIPS_GCTL0_G0E_SHIFT	19
#define MIPS_GCTL0_G0E		(_ULCAST_(1) << MIPS_GCTL0_G0E_SHIFT)
#define MIPS_GCTL0_PT_SHIFT	18
#define MIPS_GCTL0_PT		(_ULCAST_(1) << MIPS_GCTL0_PT_SHIFT)
#define MIPS_GCTL0_RAD_SHIFT	9
#define MIPS_GCTL0_RAD		(_ULCAST_(1) << MIPS_GCTL0_RAD_SHIFT)
#define MIPS_GCTL0_DRG_SHIFT	8
#define MIPS_GCTL0_DRG		(_ULCAST_(1) << MIPS_GCTL0_DRG_SHIFT)
#define MIPS_GCTL0_G2_SHIFT	7
#define MIPS_GCTL0_G2		(_ULCAST_(1) << MIPS_GCTL0_G2_SHIFT)
#define MIPS_GCTL0_GEXC_SHIFT	2
#define MIPS_GCTL0_GEXC		(_ULCAST_(0x1f) << MIPS_GCTL0_GEXC_SHIFT)
#define MIPS_GCTL0_SFC2_SHIFT	1
#define MIPS_GCTL0_SFC2		(_ULCAST_(1) << MIPS_GCTL0_SFC2_SHIFT)
#define MIPS_GCTL0_SFC1_SHIFT	0
#define MIPS_GCTL0_SFC1		(_ULCAST_(1) << MIPS_GCTL0_SFC1_SHIFT)

/* GuestCtl0.AT Guest address translation control */
#define MIPS_GCTL0_AT_ROOT	1  /* Guest MMU under Root control */
#define MIPS_GCTL0_AT_GUEST	3  /* Guest MMU under Guest control */

/* GuestCtl0.GExcCode Hypervisor exception cause codes */
#define MIPS_GCTL0_GEXC_GPSI	0  /* Guest Privileged Sensitive Instruction */
#define MIPS_GCTL0_GEXC_GSFC	1  /* Guest Software Field Change */
#define MIPS_GCTL0_GEXC_HC	2  /* Hypercall */
#define MIPS_GCTL0_GEXC_GRR	3  /* Guest Reserved Instruction Redirect */
#define MIPS_GCTL0_GEXC_GVA	8  /* Guest Virtual Address available */
#define MIPS_GCTL0_GEXC_GHFC	9  /* Guest Hardware Field Change */
#define MIPS_GCTL0_GEXC_GPA	10 /* Guest Physical Address available */

/* GuestCtl0Ext fields */
#define MIPS_GCTL0EXT_RPW_SHIFT	8
#define MIPS_GCTL0EXT_RPW	(_ULCAST_(0x3) << MIPS_GCTL0EXT_RPW_SHIFT)
#define MIPS_GCTL0EXT_NCC_SHIFT	6
#define MIPS_GCTL0EXT_NCC	(_ULCAST_(0x3) << MIPS_GCTL0EXT_NCC_SHIFT)
#define MIPS_GCTL0EXT_CGI_SHIFT	4
#define MIPS_GCTL0EXT_CGI	(_ULCAST_(1) << MIPS_GCTL0EXT_CGI_SHIFT)
#define MIPS_GCTL0EXT_FCD_SHIFT	3
#define MIPS_GCTL0EXT_FCD	(_ULCAST_(1) << MIPS_GCTL0EXT_FCD_SHIFT)
#define MIPS_GCTL0EXT_OG_SHIFT	2
#define MIPS_GCTL0EXT_OG	(_ULCAST_(1) << MIPS_GCTL0EXT_OG_SHIFT)
#define MIPS_GCTL0EXT_BG_SHIFT	1
#define MIPS_GCTL0EXT_BG	(_ULCAST_(1) << MIPS_GCTL0EXT_BG_SHIFT)
#define MIPS_GCTL0EXT_MG_SHIFT	0
#define MIPS_GCTL0EXT_MG	(_ULCAST_(1) << MIPS_GCTL0EXT_MG_SHIFT)

/* GuestCtl0Ext.RPW Root page walk configuration */
#define MIPS_GCTL0EXT_RPW_BOTH	0  /* Root PW for GPA->RPA and RVA->RPA */
#define MIPS_GCTL0EXT_RPW_GPA	2  /* Root PW for GPA->RPA */
#define MIPS_GCTL0EXT_RPW_RVA	3  /* Root PW for RVA->RPA */

/* GuestCtl0Ext.NCC Nested cache coherency attributes */
#define MIPS_GCTL0EXT_NCC_IND	0  /* Guest CCA independent of Root CCA */
#define MIPS_GCTL0EXT_NCC_MOD	1  /* Guest CCA modified by Root CCA */

/* GuestCtl1 fields */
#define MIPS_GCTL1_ID_SHIFT	0
#define MIPS_GCTL1_ID_WIDTH	8
#define MIPS_GCTL1_ID		(_ULCAST_(0xff) << MIPS_GCTL1_ID_SHIFT)
#define MIPS_GCTL1_RID_SHIFT	16
#define MIPS_GCTL1_RID_WIDTH	8
#define MIPS_GCTL1_RID		(_ULCAST_(0xff) << MIPS_GCTL1_RID_SHIFT)
#define MIPS_GCTL1_EID_SHIFT	24
#define MIPS_GCTL1_EID_WIDTH	8
#define MIPS_GCTL1_EID		(_ULCAST_(0xff) << MIPS_GCTL1_EID_SHIFT)

/* GuestID reserved for root context */
#define MIPS_GCTL1_ROOT_GUESTID	0

/* CDMMBase register bit definitions */
#define MIPS_CDMMBASE_SIZE_SHIFT 0
#define MIPS_CDMMBASE_SIZE	(_ULCAST_(511) << MIPS_CDMMBASE_SIZE_SHIFT)
#define MIPS_CDMMBASE_CI	(_ULCAST_(1) << 9)
#define MIPS_CDMMBASE_EN	(_ULCAST_(1) << 10)
#define MIPS_CDMMBASE_ADDR_SHIFT 11
#define MIPS_CDMMBASE_ADDR_START 15

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
 * Interesting Bits in the R10K CP0 Branch Diagnostic Register
 */
/* Disable Branch Target Address Cache */
#define R10K_DIAG_D_BTAC	(_ULCAST_(1) << 27)
/* Enable Branch Prediction Global History */
#define R10K_DIAG_E_GHIST	(_ULCAST_(1) << 26)
/* Disable Branch Return Cache */
#define R10K_DIAG_D_BRC		(_ULCAST_(1) << 22)

/* Flush ITLB */
#define LOONGSON_DIAG_ITLB	(_ULCAST_(1) << 2)
/* Flush DTLB */
#define LOONGSON_DIAG_DTLB	(_ULCAST_(1) << 3)
/* Flush VTLB */
#define LOONGSON_DIAG_VTLB	(_ULCAST_(1) << 12)
/* Flush FTLB */
#define LOONGSON_DIAG_FTLB	(_ULCAST_(1) << 13)

/*
 * Coprocessor 1 (FPU) register names
 */
#define CP1_REVISION	$0
#define CP1_UFR		$1
#define CP1_UNFR	$4
#define CP1_FCCR	$25
#define CP1_FEXR	$26
#define CP1_FENR	$28
#define CP1_STATUS	$31


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
#define MIPS_FPIR_HAS2008	(_ULCAST_(1) << 23)
#define MIPS_FPIR_UFRP		(_ULCAST_(1) << 28)
#define MIPS_FPIR_FREP		(_ULCAST_(1) << 29)

/*
 * Bits in the MIPS32/64 coprocessor 1 (FPU) condition codes register.
 */
#define MIPS_FCCR_CONDX_S	0
#define MIPS_FCCR_CONDX		(_ULCAST_(255) << MIPS_FCCR_CONDX_S)
#define MIPS_FCCR_COND0_S	0
#define MIPS_FCCR_COND0		(_ULCAST_(1) << MIPS_FCCR_COND0_S)
#define MIPS_FCCR_COND1_S	1
#define MIPS_FCCR_COND1		(_ULCAST_(1) << MIPS_FCCR_COND1_S)
#define MIPS_FCCR_COND2_S	2
#define MIPS_FCCR_COND2		(_ULCAST_(1) << MIPS_FCCR_COND2_S)
#define MIPS_FCCR_COND3_S	3
#define MIPS_FCCR_COND3		(_ULCAST_(1) << MIPS_FCCR_COND3_S)
#define MIPS_FCCR_COND4_S	4
#define MIPS_FCCR_COND4		(_ULCAST_(1) << MIPS_FCCR_COND4_S)
#define MIPS_FCCR_COND5_S	5
#define MIPS_FCCR_COND5		(_ULCAST_(1) << MIPS_FCCR_COND5_S)
#define MIPS_FCCR_COND6_S	6
#define MIPS_FCCR_COND6		(_ULCAST_(1) << MIPS_FCCR_COND6_S)
#define MIPS_FCCR_COND7_S	7
#define MIPS_FCCR_COND7		(_ULCAST_(1) << MIPS_FCCR_COND7_S)

/*
 * Bits in the MIPS32/64 coprocessor 1 (FPU) enables register.
 */
#define MIPS_FENR_FS_S		2
#define MIPS_FENR_FS		(_ULCAST_(1) << MIPS_FENR_FS_S)

/*
 * FPU Status Register Values
 */
#define FPU_CSR_COND_S	23					/* $fcc0 */
#define FPU_CSR_COND	(_ULCAST_(1) << FPU_CSR_COND_S)

#define FPU_CSR_FS_S	24		/* flush denormalised results to 0 */
#define FPU_CSR_FS	(_ULCAST_(1) << FPU_CSR_FS_S)

#define FPU_CSR_CONDX_S	25					/* $fcc[7:1] */
#define FPU_CSR_CONDX	(_ULCAST_(127) << FPU_CSR_CONDX_S)
#define FPU_CSR_COND1_S	25					/* $fcc1 */
#define FPU_CSR_COND1	(_ULCAST_(1) << FPU_CSR_COND1_S)
#define FPU_CSR_COND2_S	26					/* $fcc2 */
#define FPU_CSR_COND2	(_ULCAST_(1) << FPU_CSR_COND2_S)
#define FPU_CSR_COND3_S	27					/* $fcc3 */
#define FPU_CSR_COND3	(_ULCAST_(1) << FPU_CSR_COND3_S)
#define FPU_CSR_COND4_S	28					/* $fcc4 */
#define FPU_CSR_COND4	(_ULCAST_(1) << FPU_CSR_COND4_S)
#define FPU_CSR_COND5_S	29					/* $fcc5 */
#define FPU_CSR_COND5	(_ULCAST_(1) << FPU_CSR_COND5_S)
#define FPU_CSR_COND6_S	30					/* $fcc6 */
#define FPU_CSR_COND6	(_ULCAST_(1) << FPU_CSR_COND6_S)
#define FPU_CSR_COND7_S	31					/* $fcc7 */
#define FPU_CSR_COND7	(_ULCAST_(1) << FPU_CSR_COND7_S)

/*
 * Bits 22:20 of the FPU Status Register will be read as 0,
 * and should be written as zero.
 */
#define FPU_CSR_RSVD	(_ULCAST_(7) << 20)

#define FPU_CSR_ABS2008	(_ULCAST_(1) << 19)
#define FPU_CSR_NAN2008	(_ULCAST_(1) << 18)

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


#ifndef __ASSEMBLY__

/*
 * Macros for handling the ISA mode bit for MIPS16 and microMIPS.
 */
#if defined(CONFIG_SYS_SUPPORTS_MIPS16) || \
    defined(CONFIG_SYS_SUPPORTS_MICROMIPS)
#define get_isa16_mode(x)		((x) & 0x1)
#define msk_isa16_mode(x)		((x) & ~0x1)
#define set_isa16_mode(x)		do { (x) |= 0x1; } while(0)
#else
#define get_isa16_mode(x)		0
#define msk_isa16_mode(x)		(x)
#define set_isa16_mode(x)		do { } while(0)
#endif

/*
 * microMIPS instructions can be 16-bit or 32-bit in length. This
 * returns a 1 if the instruction is 16-bit and a 0 if 32-bit.
 */
static inline int mm_insn_16bit(u16 insn)
{
	u16 opcode = (insn >> 10) & 0x7;

	return (opcode >= 1 && opcode <= 3) ? 1 : 0;
}

/*
 * TLB Invalidate Flush
 */
static inline void tlbinvf(void)
{
	__asm__ __volatile__(
		".set push\n\t"
		".set noreorder\n\t"
		".word 0x42000004\n\t" /* tlbinvf */
		".set pop");
}


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
({ unsigned int __res;							\
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
({ unsigned int __res;							\
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

#define __readx_32bit_c0_register(source)				\
({									\
	unsigned int __res;						\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	.set	mips32r2				\n"	\
	"	.insn						\n"	\
	"	# mfhc0 $1, %1					\n"	\
	"	.word	(0x40410000 | ((%1 & 0x1f) << 11))	\n"	\
	"	move	%0, $1					\n"	\
	"	.set	pop					\n"	\
	: "=r" (__res)							\
	: "i" (source));						\
	__res;								\
})

#define __writex_32bit_c0_register(register, value)			\
do {									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	.set	mips32r2				\n"	\
	"	move	$1, %0					\n"	\
	"	# mthc0 $1, %1					\n"	\
	"	.insn						\n"	\
	"	.word	(0x40c10000 | ((%1 & 0x1f) << 11))	\n"	\
	"	.set	pop					\n"	\
	:								\
	: "r" (value), "i" (register));					\
} while (0)

#define read_c0_index()		__read_32bit_c0_register($0, 0)
#define write_c0_index(val)	__write_32bit_c0_register($0, 0, val)

#define read_c0_random()	__read_32bit_c0_register($1, 0)
#define write_c0_random(val)	__write_32bit_c0_register($1, 0, val)

#define read_c0_entrylo0()	__read_ulong_c0_register($2, 0)
#define write_c0_entrylo0(val)	__write_ulong_c0_register($2, 0, val)

#define readx_c0_entrylo0()	__readx_32bit_c0_register(2)
#define writex_c0_entrylo0(val)	__writex_32bit_c0_register(2, val)

#define read_c0_entrylo1()	__read_ulong_c0_register($3, 0)
#define write_c0_entrylo1(val)	__write_ulong_c0_register($3, 0, val)

#define readx_c0_entrylo1()	__readx_32bit_c0_register(3)
#define writex_c0_entrylo1(val)	__writex_32bit_c0_register(3, val)

#define read_c0_conf()		__read_32bit_c0_register($3, 0)
#define write_c0_conf(val)	__write_32bit_c0_register($3, 0, val)

#define read_c0_context()	__read_ulong_c0_register($4, 0)
#define write_c0_context(val)	__write_ulong_c0_register($4, 0, val)

#define read_c0_contextconfig()		__read_32bit_c0_register($4, 1)
#define write_c0_contextconfig(val)	__write_32bit_c0_register($4, 1, val)

#define read_c0_userlocal()	__read_ulong_c0_register($4, 2)
#define write_c0_userlocal(val) __write_ulong_c0_register($4, 2, val)

#define read_c0_xcontextconfig()	__read_ulong_c0_register($4, 3)
#define write_c0_xcontextconfig(val)	__write_ulong_c0_register($4, 3, val)

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

#define read_c0_badinstr()	__read_32bit_c0_register($8, 1)
#define read_c0_badinstrp()	__read_32bit_c0_register($8, 2)

#define read_c0_count()		__read_32bit_c0_register($9, 0)
#define write_c0_count(val)	__write_32bit_c0_register($9, 0, val)

#define read_c0_count2()	__read_32bit_c0_register($9, 6) /* pnx8550 */
#define write_c0_count2(val)	__write_32bit_c0_register($9, 6, val)

#define read_c0_count3()	__read_32bit_c0_register($9, 7) /* pnx8550 */
#define write_c0_count3(val)	__write_32bit_c0_register($9, 7, val)

#define read_c0_entryhi()	__read_ulong_c0_register($10, 0)
#define write_c0_entryhi(val)	__write_ulong_c0_register($10, 0, val)

#define read_c0_guestctl1()	__read_32bit_c0_register($10, 4)
#define write_c0_guestctl1(val)	__write_32bit_c0_register($10, 4, val)

#define read_c0_guestctl2()	__read_32bit_c0_register($10, 5)
#define write_c0_guestctl2(val)	__write_32bit_c0_register($10, 5, val)

#define read_c0_guestctl3()	__read_32bit_c0_register($10, 6)
#define write_c0_guestctl3(val)	__write_32bit_c0_register($10, 6, val)

#define read_c0_compare()	__read_32bit_c0_register($11, 0)
#define write_c0_compare(val)	__write_32bit_c0_register($11, 0, val)

#define read_c0_guestctl0ext()	__read_32bit_c0_register($11, 4)
#define write_c0_guestctl0ext(val) __write_32bit_c0_register($11, 4, val)

#define read_c0_compare2()	__read_32bit_c0_register($11, 6) /* pnx8550 */
#define write_c0_compare2(val)	__write_32bit_c0_register($11, 6, val)

#define read_c0_compare3()	__read_32bit_c0_register($11, 7) /* pnx8550 */
#define write_c0_compare3(val)	__write_32bit_c0_register($11, 7, val)

#define read_c0_status()	__read_32bit_c0_register($12, 0)

#define write_c0_status(val)	__write_32bit_c0_register($12, 0, val)

#define read_c0_guestctl0()	__read_32bit_c0_register($12, 6)
#define write_c0_guestctl0(val)	__write_32bit_c0_register($12, 6, val)

#define read_c0_gtoffset()	__read_32bit_c0_register($12, 7)
#define write_c0_gtoffset(val)	__write_32bit_c0_register($12, 7, val)

#define read_c0_cause()		__read_32bit_c0_register($13, 0)
#define write_c0_cause(val)	__write_32bit_c0_register($13, 0, val)

#define read_c0_epc()		__read_ulong_c0_register($14, 0)
#define write_c0_epc(val)	__write_ulong_c0_register($14, 0, val)

#define read_c0_prid()		__read_32bit_c0_register($15, 0)

#define read_c0_cmgcrbase()	__read_ulong_c0_register($15, 3)

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

#define read_c0_lladdr()	__read_ulong_c0_register($17, 0)
#define write_c0_lladdr(val)	__write_ulong_c0_register($17, 0, val)
#define read_c0_maar()		__read_ulong_c0_register($17, 1)
#define write_c0_maar(val)	__write_ulong_c0_register($17, 1, val)
#define read_c0_maari()		__read_32bit_c0_register($17, 2)
#define write_c0_maari(val)	__write_32bit_c0_register($17, 2, val)

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

/* R10K CP0 Branch Diagnostic register is 64bits wide */
#define read_c0_r10k_diag()	__read_64bit_c0_register($22, 0)
#define write_c0_r10k_diag(val)	__write_64bit_c0_register($22, 0, val)

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

#define read_c0_ebase_64()	__read_64bit_c0_register($15, 1)
#define write_c0_ebase_64(val)	__write_64bit_c0_register($15, 1, val)

#define read_c0_cdmmbase()	__read_ulong_c0_register($15, 2)
#define write_c0_cdmmbase(val)	__write_ulong_c0_register($15, 2, val)

/* MIPSR3 */
#define read_c0_segctl0()	__read_32bit_c0_register($5, 2)
#define write_c0_segctl0(val)	__write_32bit_c0_register($5, 2, val)

#define read_c0_segctl1()	__read_32bit_c0_register($5, 3)
#define write_c0_segctl1(val)	__write_32bit_c0_register($5, 3, val)

#define read_c0_segctl2()	__read_32bit_c0_register($5, 4)
#define write_c0_segctl2(val)	__write_32bit_c0_register($5, 4, val)

/* Hardware Page Table Walker */
#define read_c0_pwbase()	__read_ulong_c0_register($5, 5)
#define write_c0_pwbase(val)	__write_ulong_c0_register($5, 5, val)

#define read_c0_pwfield()	__read_ulong_c0_register($5, 6)
#define write_c0_pwfield(val)	__write_ulong_c0_register($5, 6, val)

#define read_c0_pwsize()	__read_ulong_c0_register($5, 7)
#define write_c0_pwsize(val)	__write_ulong_c0_register($5, 7, val)

#define read_c0_pwctl()		__read_32bit_c0_register($6, 6)
#define write_c0_pwctl(val)	__write_32bit_c0_register($6, 6, val)

#define read_c0_pgd()		__read_64bit_c0_register($9, 7)
#define write_c0_pgd(val)	__write_64bit_c0_register($9, 7, val)

#define read_c0_kpgd()		__read_64bit_c0_register($31, 7)
#define write_c0_kpgd(val)	__write_64bit_c0_register($31, 7, val)

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
 * Macros to access the guest system control coprocessor
 */

#ifdef TOOLCHAIN_SUPPORTS_VIRT

#define __read_32bit_gc0_register(source, sel)				\
({ int __res;								\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tmips32r2\n\t"					\
		".set\tvirt\n\t"					\
		"mfgc0\t%0, $%1, %2\n\t"				\
		".set\tpop"						\
		: "=r" (__res)						\
		: "i" (source), "i" (sel));				\
	__res;								\
})

#define __read_64bit_gc0_register(source, sel)				\
({ unsigned long long __res;						\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tmips64r2\n\t"					\
		".set\tvirt\n\t"					\
		"dmfgc0\t%0, $%1, %2\n\t"			\
		".set\tpop"						\
		: "=r" (__res)						\
		: "i" (source), "i" (sel));				\
	__res;								\
})

#define __write_32bit_gc0_register(register, sel, value)		\
do {									\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tmips32r2\n\t"					\
		".set\tvirt\n\t"					\
		"mtgc0\t%z0, $%1, %2\n\t"				\
		".set\tpop"						\
		: : "Jr" ((unsigned int)(value)),			\
		    "i" (register), "i" (sel));				\
} while (0)

#define __write_64bit_gc0_register(register, sel, value)		\
do {									\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tmips64r2\n\t"					\
		".set\tvirt\n\t"					\
		"dmtgc0\t%z0, $%1, %2\n\t"				\
		".set\tpop"						\
		: : "Jr" (value),					\
		    "i" (register), "i" (sel));				\
} while (0)

#else	/* TOOLCHAIN_SUPPORTS_VIRT */

#define __read_32bit_gc0_register(source, sel)				\
({ int __res;								\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tnoat\n\t"					\
		"# mfgc0\t$1, $%1, %2\n\t"				\
		".word\t(0x40610000 | %1 << 11 | %2)\n\t"		\
		"move\t%0, $1\n\t"					\
		".set\tpop"						\
		: "=r" (__res)						\
		: "i" (source), "i" (sel));				\
	__res;								\
})

#define __read_64bit_gc0_register(source, sel)				\
({ unsigned long long __res;						\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tnoat\n\t"					\
		"# dmfgc0\t$1, $%1, %2\n\t"				\
		".word\t(0x40610100 | %1 << 11 | %2)\n\t"		\
		"move\t%0, $1\n\t"					\
		".set\tpop"						\
		: "=r" (__res)						\
		: "i" (source), "i" (sel));				\
	__res;								\
})

#define __write_32bit_gc0_register(register, sel, value)		\
do {									\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tnoat\n\t"					\
		"move\t$1, %0\n\t"					\
		"# mtgc0\t$1, $%1, %2\n\t"				\
		".word\t(0x40610200 | %1 << 11 | %2)\n\t"		\
		".set\tpop"						\
		: : "Jr" ((unsigned int)(value)),			\
		    "i" (register), "i" (sel));				\
} while (0)

#define __write_64bit_gc0_register(register, sel, value)		\
do {									\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tnoat\n\t"					\
		"move\t$1, %0\n\t"					\
		"# dmtgc0\t$1, $%1, %2\n\t"				\
		".word\t(0x40610300 | %1 << 11 | %2)\n\t"		\
		".set\tpop"						\
		: : "Jr" (value),					\
		    "i" (register), "i" (sel));				\
} while (0)

#endif	/* !TOOLCHAIN_SUPPORTS_VIRT */

#define __read_ulong_gc0_register(reg, sel)				\
	((sizeof(unsigned long) == 4) ?					\
	(unsigned long) __read_32bit_gc0_register(reg, sel) :		\
	(unsigned long) __read_64bit_gc0_register(reg, sel))

#define __write_ulong_gc0_register(reg, sel, val)			\
do {									\
	if (sizeof(unsigned long) == 4)					\
		__write_32bit_gc0_register(reg, sel, val);		\
	else								\
		__write_64bit_gc0_register(reg, sel, val);		\
} while (0)

#define read_gc0_index()		__read_32bit_gc0_register(0, 0)
#define write_gc0_index(val)		__write_32bit_gc0_register(0, 0, val)

#define read_gc0_entrylo0()		__read_ulong_gc0_register(2, 0)
#define write_gc0_entrylo0(val)		__write_ulong_gc0_register(2, 0, val)

#define read_gc0_entrylo1()		__read_ulong_gc0_register(3, 0)
#define write_gc0_entrylo1(val)		__write_ulong_gc0_register(3, 0, val)

#define read_gc0_context()		__read_ulong_gc0_register(4, 0)
#define write_gc0_context(val)		__write_ulong_gc0_register(4, 0, val)

#define read_gc0_contextconfig()	__read_32bit_gc0_register(4, 1)
#define write_gc0_contextconfig(val)	__write_32bit_gc0_register(4, 1, val)

#define read_gc0_userlocal()		__read_ulong_gc0_register(4, 2)
#define write_gc0_userlocal(val)	__write_ulong_gc0_register(4, 2, val)

#define read_gc0_xcontextconfig()	__read_ulong_gc0_register(4, 3)
#define write_gc0_xcontextconfig(val)	__write_ulong_gc0_register(4, 3, val)

#define read_gc0_pagemask()		__read_32bit_gc0_register(5, 0)
#define write_gc0_pagemask(val)		__write_32bit_gc0_register(5, 0, val)

#define read_gc0_pagegrain()		__read_32bit_gc0_register(5, 1)
#define write_gc0_pagegrain(val)	__write_32bit_gc0_register(5, 1, val)

#define read_gc0_segctl0()		__read_ulong_gc0_register(5, 2)
#define write_gc0_segctl0(val)		__write_ulong_gc0_register(5, 2, val)

#define read_gc0_segctl1()		__read_ulong_gc0_register(5, 3)
#define write_gc0_segctl1(val)		__write_ulong_gc0_register(5, 3, val)

#define read_gc0_segctl2()		__read_ulong_gc0_register(5, 4)
#define write_gc0_segctl2(val)		__write_ulong_gc0_register(5, 4, val)

#define read_gc0_pwbase()		__read_ulong_gc0_register(5, 5)
#define write_gc0_pwbase(val)		__write_ulong_gc0_register(5, 5, val)

#define read_gc0_pwfield()		__read_ulong_gc0_register(5, 6)
#define write_gc0_pwfield(val)		__write_ulong_gc0_register(5, 6, val)

#define read_gc0_pwsize()		__read_ulong_gc0_register(5, 7)
#define write_gc0_pwsize(val)		__write_ulong_gc0_register(5, 7, val)

#define read_gc0_wired()		__read_32bit_gc0_register(6, 0)
#define write_gc0_wired(val)		__write_32bit_gc0_register(6, 0, val)

#define read_gc0_pwctl()		__read_32bit_gc0_register(6, 6)
#define write_gc0_pwctl(val)		__write_32bit_gc0_register(6, 6, val)

#define read_gc0_hwrena()		__read_32bit_gc0_register(7, 0)
#define write_gc0_hwrena(val)		__write_32bit_gc0_register(7, 0, val)

#define read_gc0_badvaddr()		__read_ulong_gc0_register(8, 0)
#define write_gc0_badvaddr(val)		__write_ulong_gc0_register(8, 0, val)

#define read_gc0_badinstr()		__read_32bit_gc0_register(8, 1)
#define write_gc0_badinstr(val)		__write_32bit_gc0_register(8, 1, val)

#define read_gc0_badinstrp()		__read_32bit_gc0_register(8, 2)
#define write_gc0_badinstrp(val)	__write_32bit_gc0_register(8, 2, val)

#define read_gc0_count()		__read_32bit_gc0_register(9, 0)

#define read_gc0_entryhi()		__read_ulong_gc0_register(10, 0)
#define write_gc0_entryhi(val)		__write_ulong_gc0_register(10, 0, val)

#define read_gc0_compare()		__read_32bit_gc0_register(11, 0)
#define write_gc0_compare(val)		__write_32bit_gc0_register(11, 0, val)

#define read_gc0_status()		__read_32bit_gc0_register(12, 0)
#define write_gc0_status(val)		__write_32bit_gc0_register(12, 0, val)

#define read_gc0_intctl()		__read_32bit_gc0_register(12, 1)
#define write_gc0_intctl(val)		__write_32bit_gc0_register(12, 1, val)

#define read_gc0_cause()		__read_32bit_gc0_register(13, 0)
#define write_gc0_cause(val)		__write_32bit_gc0_register(13, 0, val)

#define read_gc0_epc()			__read_ulong_gc0_register(14, 0)
#define write_gc0_epc(val)		__write_ulong_gc0_register(14, 0, val)

#define read_gc0_ebase()		__read_32bit_gc0_register(15, 1)
#define write_gc0_ebase(val)		__write_32bit_gc0_register(15, 1, val)

#define read_gc0_ebase_64()		__read_64bit_gc0_register(15, 1)
#define write_gc0_ebase_64(val)		__write_64bit_gc0_register(15, 1, val)

#define read_gc0_config()		__read_32bit_gc0_register(16, 0)
#define read_gc0_config1()		__read_32bit_gc0_register(16, 1)
#define read_gc0_config2()		__read_32bit_gc0_register(16, 2)
#define read_gc0_config3()		__read_32bit_gc0_register(16, 3)
#define read_gc0_config4()		__read_32bit_gc0_register(16, 4)
#define read_gc0_config5()		__read_32bit_gc0_register(16, 5)
#define read_gc0_config6()		__read_32bit_gc0_register(16, 6)
#define read_gc0_config7()		__read_32bit_gc0_register(16, 7)
#define write_gc0_config(val)		__write_32bit_gc0_register(16, 0, val)
#define write_gc0_config1(val)		__write_32bit_gc0_register(16, 1, val)
#define write_gc0_config2(val)		__write_32bit_gc0_register(16, 2, val)
#define write_gc0_config3(val)		__write_32bit_gc0_register(16, 3, val)
#define write_gc0_config4(val)		__write_32bit_gc0_register(16, 4, val)
#define write_gc0_config5(val)		__write_32bit_gc0_register(16, 5, val)
#define write_gc0_config6(val)		__write_32bit_gc0_register(16, 6, val)
#define write_gc0_config7(val)		__write_32bit_gc0_register(16, 7, val)

#define read_gc0_watchlo0()		__read_ulong_gc0_register(18, 0)
#define read_gc0_watchlo1()		__read_ulong_gc0_register(18, 1)
#define read_gc0_watchlo2()		__read_ulong_gc0_register(18, 2)
#define read_gc0_watchlo3()		__read_ulong_gc0_register(18, 3)
#define read_gc0_watchlo4()		__read_ulong_gc0_register(18, 4)
#define read_gc0_watchlo5()		__read_ulong_gc0_register(18, 5)
#define read_gc0_watchlo6()		__read_ulong_gc0_register(18, 6)
#define read_gc0_watchlo7()		__read_ulong_gc0_register(18, 7)
#define write_gc0_watchlo0(val)		__write_ulong_gc0_register(18, 0, val)
#define write_gc0_watchlo1(val)		__write_ulong_gc0_register(18, 1, val)
#define write_gc0_watchlo2(val)		__write_ulong_gc0_register(18, 2, val)
#define write_gc0_watchlo3(val)		__write_ulong_gc0_register(18, 3, val)
#define write_gc0_watchlo4(val)		__write_ulong_gc0_register(18, 4, val)
#define write_gc0_watchlo5(val)		__write_ulong_gc0_register(18, 5, val)
#define write_gc0_watchlo6(val)		__write_ulong_gc0_register(18, 6, val)
#define write_gc0_watchlo7(val)		__write_ulong_gc0_register(18, 7, val)

#define read_gc0_watchhi0()		__read_32bit_gc0_register(19, 0)
#define read_gc0_watchhi1()		__read_32bit_gc0_register(19, 1)
#define read_gc0_watchhi2()		__read_32bit_gc0_register(19, 2)
#define read_gc0_watchhi3()		__read_32bit_gc0_register(19, 3)
#define read_gc0_watchhi4()		__read_32bit_gc0_register(19, 4)
#define read_gc0_watchhi5()		__read_32bit_gc0_register(19, 5)
#define read_gc0_watchhi6()		__read_32bit_gc0_register(19, 6)
#define read_gc0_watchhi7()		__read_32bit_gc0_register(19, 7)
#define write_gc0_watchhi0(val)		__write_32bit_gc0_register(19, 0, val)
#define write_gc0_watchhi1(val)		__write_32bit_gc0_register(19, 1, val)
#define write_gc0_watchhi2(val)		__write_32bit_gc0_register(19, 2, val)
#define write_gc0_watchhi3(val)		__write_32bit_gc0_register(19, 3, val)
#define write_gc0_watchhi4(val)		__write_32bit_gc0_register(19, 4, val)
#define write_gc0_watchhi5(val)		__write_32bit_gc0_register(19, 5, val)
#define write_gc0_watchhi6(val)		__write_32bit_gc0_register(19, 6, val)
#define write_gc0_watchhi7(val)		__write_32bit_gc0_register(19, 7, val)

#define read_gc0_xcontext()		__read_ulong_gc0_register(20, 0)
#define write_gc0_xcontext(val)		__write_ulong_gc0_register(20, 0, val)

#define read_gc0_perfctrl0()		__read_32bit_gc0_register(25, 0)
#define write_gc0_perfctrl0(val)	__write_32bit_gc0_register(25, 0, val)
#define read_gc0_perfcntr0()		__read_32bit_gc0_register(25, 1)
#define write_gc0_perfcntr0(val)	__write_32bit_gc0_register(25, 1, val)
#define read_gc0_perfcntr0_64()		__read_64bit_gc0_register(25, 1)
#define write_gc0_perfcntr0_64(val)	__write_64bit_gc0_register(25, 1, val)
#define read_gc0_perfctrl1()		__read_32bit_gc0_register(25, 2)
#define write_gc0_perfctrl1(val)	__write_32bit_gc0_register(25, 2, val)
#define read_gc0_perfcntr1()		__read_32bit_gc0_register(25, 3)
#define write_gc0_perfcntr1(val)	__write_32bit_gc0_register(25, 3, val)
#define read_gc0_perfcntr1_64()		__read_64bit_gc0_register(25, 3)
#define write_gc0_perfcntr1_64(val)	__write_64bit_gc0_register(25, 3, val)
#define read_gc0_perfctrl2()		__read_32bit_gc0_register(25, 4)
#define write_gc0_perfctrl2(val)	__write_32bit_gc0_register(25, 4, val)
#define read_gc0_perfcntr2()		__read_32bit_gc0_register(25, 5)
#define write_gc0_perfcntr2(val)	__write_32bit_gc0_register(25, 5, val)
#define read_gc0_perfcntr2_64()		__read_64bit_gc0_register(25, 5)
#define write_gc0_perfcntr2_64(val)	__write_64bit_gc0_register(25, 5, val)
#define read_gc0_perfctrl3()		__read_32bit_gc0_register(25, 6)
#define write_gc0_perfctrl3(val)	__write_32bit_gc0_register(25, 6, val)
#define read_gc0_perfcntr3()		__read_32bit_gc0_register(25, 7)
#define write_gc0_perfcntr3(val)	__write_32bit_gc0_register(25, 7, val)
#define read_gc0_perfcntr3_64()		__read_64bit_gc0_register(25, 7)
#define write_gc0_perfcntr3_64(val)	__write_64bit_gc0_register(25, 7, val)

#define read_gc0_errorepc()		__read_ulong_gc0_register(30, 0)
#define write_gc0_errorepc(val)		__write_ulong_gc0_register(30, 0, val)

#define read_gc0_kscratch1()		__read_ulong_gc0_register(31, 2)
#define read_gc0_kscratch2()		__read_ulong_gc0_register(31, 3)
#define read_gc0_kscratch3()		__read_ulong_gc0_register(31, 4)
#define read_gc0_kscratch4()		__read_ulong_gc0_register(31, 5)
#define read_gc0_kscratch5()		__read_ulong_gc0_register(31, 6)
#define read_gc0_kscratch6()		__read_ulong_gc0_register(31, 7)
#define write_gc0_kscratch1(val)	__write_ulong_gc0_register(31, 2, val)
#define write_gc0_kscratch2(val)	__write_ulong_gc0_register(31, 3, val)
#define write_gc0_kscratch3(val)	__write_ulong_gc0_register(31, 4, val)
#define write_gc0_kscratch4(val)	__write_ulong_gc0_register(31, 5, val)
#define write_gc0_kscratch5(val)	__write_ulong_gc0_register(31, 6, val)
#define write_gc0_kscratch6(val)	__write_ulong_gc0_register(31, 7, val)

/*
 * Macros to access the floating point coprocessor control registers
 */
#define _read_32bit_cp1_register(source, gas_hardfloat)			\
({									\
	unsigned int __res;						\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	reorder					\n"	\
	"	# gas fails to assemble cfc1 for some archs,	\n"	\
	"	# like Octeon.					\n"	\
	"	.set	mips1					\n"	\
	"	"STR(gas_hardfloat)"				\n"	\
	"	cfc1	%0,"STR(source)"			\n"	\
	"	.set	pop					\n"	\
	: "=r" (__res));						\
	__res;								\
})

#define _write_32bit_cp1_register(dest, val, gas_hardfloat)		\
do {									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	reorder					\n"	\
	"	"STR(gas_hardfloat)"				\n"	\
	"	ctc1	%0,"STR(dest)"				\n"	\
	"	.set	pop					\n"	\
	: : "r" (val));							\
} while (0)

#ifdef GAS_HAS_SET_HARDFLOAT
#define read_32bit_cp1_register(source)					\
	_read_32bit_cp1_register(source, .set hardfloat)
#define write_32bit_cp1_register(dest, val)				\
	_write_32bit_cp1_register(dest, val, .set hardfloat)
#else
#define read_32bit_cp1_register(source)					\
	_read_32bit_cp1_register(source, )
#define write_32bit_cp1_register(dest, val)				\
	_write_32bit_cp1_register(dest, val, )
#endif

#ifdef HAVE_AS_DSP
#define rddsp(mask)							\
({									\
	unsigned int __dspctl;						\
									\
	__asm__ __volatile__(						\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	rddsp	%0, %x1					\n"	\
	"	.set pop					\n"	\
	: "=r" (__dspctl)						\
	: "i" (mask));							\
	__dspctl;							\
})

#define wrdsp(val, mask)						\
do {									\
	__asm__ __volatile__(						\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	wrdsp	%0, %x1					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (val), "i" (mask));					\
} while (0)

#define mflo0()								\
({									\
	long mflo0;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mflo %0, $ac0					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mflo0)); 						\
	mflo0;								\
})

#define mflo1()								\
({									\
	long mflo1;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mflo %0, $ac1					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mflo1)); 						\
	mflo1;								\
})

#define mflo2()								\
({									\
	long mflo2;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mflo %0, $ac2					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mflo2)); 						\
	mflo2;								\
})

#define mflo3()								\
({									\
	long mflo3;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mflo %0, $ac3					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mflo3)); 						\
	mflo3;								\
})

#define mfhi0()								\
({									\
	long mfhi0;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mfhi %0, $ac0					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mfhi0)); 						\
	mfhi0;								\
})

#define mfhi1()								\
({									\
	long mfhi1;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mfhi %0, $ac1					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mfhi1)); 						\
	mfhi1;								\
})

#define mfhi2()								\
({									\
	long mfhi2;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mfhi %0, $ac2					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mfhi2)); 						\
	mfhi2;								\
})

#define mfhi3()								\
({									\
	long mfhi3;							\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mfhi %0, $ac3					\n"	\
	"	.set pop					\n" 	\
	: "=r" (mfhi3)); 						\
	mfhi3;								\
})


#define mtlo0(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mtlo %0, $ac0					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

#define mtlo1(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mtlo %0, $ac1					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

#define mtlo2(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mtlo %0, $ac2					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

#define mtlo3(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mtlo %0, $ac3					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

#define mthi0(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mthi %0, $ac0					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

#define mthi1(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mthi %0, $ac1					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

#define mthi2(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mthi %0, $ac2					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

#define mthi3(x)							\
({									\
	__asm__(							\
	"	.set push					\n"	\
	"	.set dsp					\n"	\
	"	mthi %0, $ac3					\n"	\
	"	.set pop					\n"	\
	:								\
	: "r" (x));							\
})

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

#ifdef TOOLCHAIN_SUPPORTS_VIRT

/*
 * Guest TLB operations.
 *
 * It is responsibility of the caller to take care of any TLB hazards.
 */
static inline void guest_tlb_probe(void)
{
	__asm__ __volatile__(
		".set push\n\t"
		".set noreorder\n\t"
		".set virt\n\t"
		"tlbgp\n\t"
		".set pop");
}

static inline void guest_tlb_read(void)
{
	__asm__ __volatile__(
		".set push\n\t"
		".set noreorder\n\t"
		".set virt\n\t"
		"tlbgr\n\t"
		".set pop");
}

static inline void guest_tlb_write_indexed(void)
{
	__asm__ __volatile__(
		".set push\n\t"
		".set noreorder\n\t"
		".set virt\n\t"
		"tlbgwi\n\t"
		".set pop");
}

static inline void guest_tlb_write_random(void)
{
	__asm__ __volatile__(
		".set push\n\t"
		".set noreorder\n\t"
		".set virt\n\t"
		"tlbgwr\n\t"
		".set pop");
}

/*
 * Guest TLB Invalidate Flush
 */
static inline void guest_tlbinvf(void)
{
	__asm__ __volatile__(
		".set push\n\t"
		".set noreorder\n\t"
		".set virt\n\t"
		"tlbginvf\n\t"
		".set pop");
}

#else	/* TOOLCHAIN_SUPPORTS_VIRT */

/*
 * Guest TLB operations.
 *
 * It is responsibility of the caller to take care of any TLB hazards.
 */
static inline void guest_tlb_probe(void)
{
	__asm__ __volatile__(
		"# tlbgp\n\t"
		".word 0x42000010");
}

static inline void guest_tlb_read(void)
{
	__asm__ __volatile__(
		"# tlbgr\n\t"
		".word 0x42000009");
}

static inline void guest_tlb_write_indexed(void)
{
	__asm__ __volatile__(
		"# tlbgwi\n\t"
		".word 0x4200000a");
}

static inline void guest_tlb_write_random(void)
{
	__asm__ __volatile__(
		"# tlbgwr\n\t"
		".word 0x4200000e");
}

/*
 * Guest TLB Invalidate Flush
 */
static inline void guest_tlbinvf(void)
{
	__asm__ __volatile__(
		"# tlbginvf\n\t"
		".word 0x4200000c");
}

#endif	/* !TOOLCHAIN_SUPPORTS_VIRT */

/*
 * Manipulate bits in a register.
 */
#define __BUILD_SET_COMMON(name)				\
static inline unsigned int					\
set_##name(unsigned int set)					\
{								\
	unsigned int res, new;					\
								\
	res = read_##name();					\
	new = res | set;					\
	write_##name(new);					\
								\
	return res;						\
}								\
								\
static inline unsigned int					\
clear_##name(unsigned int clear)				\
{								\
	unsigned int res, new;					\
								\
	res = read_##name();					\
	new = res & ~clear;					\
	write_##name(new);					\
								\
	return res;						\
}								\
								\
static inline unsigned int					\
change_##name(unsigned int change, unsigned int val)		\
{								\
	unsigned int res, new;					\
								\
	res = read_##name();					\
	new = res & ~change;					\
	new |= (val & change);					\
	write_##name(new);					\
								\
	return res;						\
}

/*
 * Manipulate bits in a c0 register.
 */
#define __BUILD_SET_C0(name)	__BUILD_SET_COMMON(c0_##name)

__BUILD_SET_C0(status)
__BUILD_SET_C0(cause)
__BUILD_SET_C0(config)
__BUILD_SET_C0(config5)
__BUILD_SET_C0(intcontrol)
__BUILD_SET_C0(intctl)
__BUILD_SET_C0(srsmap)
__BUILD_SET_C0(pagegrain)
__BUILD_SET_C0(guestctl0)
__BUILD_SET_C0(guestctl0ext)
__BUILD_SET_C0(guestctl1)
__BUILD_SET_C0(guestctl2)
__BUILD_SET_C0(guestctl3)
__BUILD_SET_C0(brcm_config_0)
__BUILD_SET_C0(brcm_bus_pll)
__BUILD_SET_C0(brcm_reset)
__BUILD_SET_C0(brcm_cmt_intr)
__BUILD_SET_C0(brcm_cmt_ctrl)
__BUILD_SET_C0(brcm_config)
__BUILD_SET_C0(brcm_mode)

/*
 * Manipulate bits in a guest c0 register.
 */
#define __BUILD_SET_GC0(name)	__BUILD_SET_COMMON(gc0_##name)

__BUILD_SET_GC0(status)
__BUILD_SET_GC0(cause)
__BUILD_SET_GC0(ebase)

/*
 * Return low 10 bits of ebase.
 * Note that under KVM (MIPSVZ) this returns vcpu id.
 */
static inline unsigned int get_ebase_cpunum(void)
{
	return read_c0_ebase() & MIPS_EBASE_CPUNUM;
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_MIPSREGS_H */

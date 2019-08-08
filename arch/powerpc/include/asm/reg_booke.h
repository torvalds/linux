/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Contains register definitions common to the Book E PowerPC
 * specification.  Notice that while the IBM-40x series of CPUs
 * are not true Book E PowerPCs, they borrowed a number of features
 * before Book E was finalized, and are included here as well.  Unfortunately,
 * they sometimes used different locations than true Book E CPUs did.
 *
 * Copyright 2009-2010 Freescale Semiconductor, Inc.
 */
#ifdef __KERNEL__
#ifndef __ASM_POWERPC_REG_BOOKE_H__
#define __ASM_POWERPC_REG_BOOKE_H__

#include <asm/ppc-opcode.h>

/* Machine State Register (MSR) Fields */
#define MSR_GS_LG	28	/* Guest state */
#define MSR_UCLE_LG	26	/* User-mode cache lock enable */
#define MSR_SPE_LG	25	/* Enable SPE */
#define MSR_DWE_LG	10	/* Debug Wait Enable */
#define MSR_UBLE_LG	10	/* BTB lock enable (e500) */
#define MSR_IS_LG	MSR_IR_LG /* Instruction Space */
#define MSR_DS_LG	MSR_DR_LG /* Data Space */
#define MSR_PMM_LG	2	/* Performance monitor mark bit */
#define MSR_CM_LG	31	/* Computation Mode (0=32-bit, 1=64-bit) */

#define MSR_GS		__MASK(MSR_GS_LG)
#define MSR_UCLE	__MASK(MSR_UCLE_LG)
#define MSR_SPE		__MASK(MSR_SPE_LG)
#define MSR_DWE		__MASK(MSR_DWE_LG)
#define MSR_UBLE	__MASK(MSR_UBLE_LG)
#define MSR_IS		__MASK(MSR_IS_LG)
#define MSR_DS		__MASK(MSR_DS_LG)
#define MSR_PMM		__MASK(MSR_PMM_LG)
#define MSR_CM		__MASK(MSR_CM_LG)

#if defined(CONFIG_PPC_BOOK3E_64)
#define MSR_64BIT	MSR_CM

#define MSR_		(MSR_ME | MSR_RI | MSR_CE)
#define MSR_KERNEL	(MSR_ | MSR_64BIT)
#define MSR_USER32	(MSR_ | MSR_PR | MSR_EE)
#define MSR_USER64	(MSR_USER32 | MSR_64BIT)
#elif defined (CONFIG_40x)
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_IR|MSR_DR|MSR_CE)
#define MSR_USER	(MSR_KERNEL|MSR_PR|MSR_EE)
#else
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_CE)
#define MSR_USER	(MSR_KERNEL|MSR_PR|MSR_EE)
#endif

/* Special Purpose Registers (SPRNs)*/
#define SPRN_DECAR	0x036	/* Decrementer Auto Reload Register */
#define SPRN_IVPR	0x03F	/* Interrupt Vector Prefix Register */
#define SPRN_USPRG0	0x100	/* User Special Purpose Register General 0 */
#define SPRN_SPRG3R	0x103	/* Special Purpose Register General 3 Read */
#define SPRN_SPRG4R	0x104	/* Special Purpose Register General 4 Read */
#define SPRN_SPRG5R	0x105	/* Special Purpose Register General 5 Read */
#define SPRN_SPRG6R	0x106	/* Special Purpose Register General 6 Read */
#define SPRN_SPRG7R	0x107	/* Special Purpose Register General 7 Read */
#define SPRN_SPRG4W	0x114	/* Special Purpose Register General 4 Write */
#define SPRN_SPRG5W	0x115	/* Special Purpose Register General 5 Write */
#define SPRN_SPRG6W	0x116	/* Special Purpose Register General 6 Write */
#define SPRN_SPRG7W	0x117	/* Special Purpose Register General 7 Write */
#define SPRN_EPCR	0x133	/* Embedded Processor Control Register */
#define SPRN_DBCR2	0x136	/* Debug Control Register 2 */
#define SPRN_DBCR4	0x233	/* Debug Control Register 4 */
#define SPRN_MSRP	0x137	/* MSR Protect Register */
#define SPRN_IAC3	0x13A	/* Instruction Address Compare 3 */
#define SPRN_IAC4	0x13B	/* Instruction Address Compare 4 */
#define SPRN_DVC1	0x13E	/* Data Value Compare Register 1 */
#define SPRN_DVC2	0x13F	/* Data Value Compare Register 2 */
#define SPRN_LPID	0x152	/* Logical Partition ID */
#define SPRN_MAS8	0x155	/* MMU Assist Register 8 */
#define SPRN_TLB0PS	0x158	/* TLB 0 Page Size Register */
#define SPRN_TLB1PS	0x159	/* TLB 1 Page Size Register */
#define SPRN_MAS5_MAS6	0x15c	/* MMU Assist Register 5 || 6 */
#define SPRN_MAS8_MAS1	0x15d	/* MMU Assist Register 8 || 1 */
#define SPRN_EPTCFG	0x15e	/* Embedded Page Table Config */
#define SPRN_GSPRG0	0x170	/* Guest SPRG0 */
#define SPRN_GSPRG1	0x171	/* Guest SPRG1 */
#define SPRN_GSPRG2	0x172	/* Guest SPRG2 */
#define SPRN_GSPRG3	0x173	/* Guest SPRG3 */
#define SPRN_MAS7_MAS3	0x174	/* MMU Assist Register 7 || 3 */
#define SPRN_MAS0_MAS1	0x175	/* MMU Assist Register 0 || 1 */
#define SPRN_GSRR0	0x17A	/* Guest SRR0 */
#define SPRN_GSRR1	0x17B	/* Guest SRR1 */
#define SPRN_GEPR	0x17C	/* Guest EPR */
#define SPRN_GDEAR	0x17D	/* Guest DEAR */
#define SPRN_GPIR	0x17E	/* Guest PIR */
#define SPRN_GESR	0x17F	/* Guest Exception Syndrome Register */
#define SPRN_IVOR0	0x190	/* Interrupt Vector Offset Register 0 */
#define SPRN_IVOR1	0x191	/* Interrupt Vector Offset Register 1 */
#define SPRN_IVOR2	0x192	/* Interrupt Vector Offset Register 2 */
#define SPRN_IVOR3	0x193	/* Interrupt Vector Offset Register 3 */
#define SPRN_IVOR4	0x194	/* Interrupt Vector Offset Register 4 */
#define SPRN_IVOR5	0x195	/* Interrupt Vector Offset Register 5 */
#define SPRN_IVOR6	0x196	/* Interrupt Vector Offset Register 6 */
#define SPRN_IVOR7	0x197	/* Interrupt Vector Offset Register 7 */
#define SPRN_IVOR8	0x198	/* Interrupt Vector Offset Register 8 */
#define SPRN_IVOR9	0x199	/* Interrupt Vector Offset Register 9 */
#define SPRN_IVOR10	0x19A	/* Interrupt Vector Offset Register 10 */
#define SPRN_IVOR11	0x19B	/* Interrupt Vector Offset Register 11 */
#define SPRN_IVOR12	0x19C	/* Interrupt Vector Offset Register 12 */
#define SPRN_IVOR13	0x19D	/* Interrupt Vector Offset Register 13 */
#define SPRN_IVOR14	0x19E	/* Interrupt Vector Offset Register 14 */
#define SPRN_IVOR15	0x19F	/* Interrupt Vector Offset Register 15 */
#define SPRN_IVOR38	0x1B0	/* Interrupt Vector Offset Register 38 */
#define SPRN_IVOR39	0x1B1	/* Interrupt Vector Offset Register 39 */
#define SPRN_IVOR40	0x1B2	/* Interrupt Vector Offset Register 40 */
#define SPRN_IVOR41	0x1B3	/* Interrupt Vector Offset Register 41 */
#define SPRN_IVOR42	0x1B4	/* Interrupt Vector Offset Register 42 */
#define SPRN_GIVOR2	0x1B8	/* Guest IVOR2 */
#define SPRN_GIVOR3	0x1B9	/* Guest IVOR3 */
#define SPRN_GIVOR4	0x1BA	/* Guest IVOR4 */
#define SPRN_GIVOR8	0x1BB	/* Guest IVOR8 */
#define SPRN_GIVOR13	0x1BC	/* Guest IVOR13 */
#define SPRN_GIVOR14	0x1BD	/* Guest IVOR14 */
#define SPRN_GIVPR	0x1BF	/* Guest IVPR */
#define SPRN_SPEFSCR	0x200	/* SPE & Embedded FP Status & Control */
#define SPRN_BBEAR	0x201	/* Branch Buffer Entry Address Register */
#define SPRN_BBTAR	0x202	/* Branch Buffer Target Address Register */
#define SPRN_L1CFG0	0x203	/* L1 Cache Configure Register 0 */
#define SPRN_L1CFG1	0x204	/* L1 Cache Configure Register 1 */
#define SPRN_ATB	0x20E	/* Alternate Time Base */
#define SPRN_ATBL	0x20E	/* Alternate Time Base Lower */
#define SPRN_ATBU	0x20F	/* Alternate Time Base Upper */
#define SPRN_IVOR32	0x210	/* Interrupt Vector Offset Register 32 */
#define SPRN_IVOR33	0x211	/* Interrupt Vector Offset Register 33 */
#define SPRN_IVOR34	0x212	/* Interrupt Vector Offset Register 34 */
#define SPRN_IVOR35	0x213	/* Interrupt Vector Offset Register 35 */
#define SPRN_IVOR36	0x214	/* Interrupt Vector Offset Register 36 */
#define SPRN_IVOR37	0x215	/* Interrupt Vector Offset Register 37 */
#define SPRN_MCARU	0x239	/* Machine Check Address Register Upper */
#define SPRN_MCSRR0	0x23A	/* Machine Check Save and Restore Register 0 */
#define SPRN_MCSRR1	0x23B	/* Machine Check Save and Restore Register 1 */
#define SPRN_MCSR	0x23C	/* Machine Check Status Register */
#define SPRN_MCAR	0x23D	/* Machine Check Address Register */
#define SPRN_DSRR0	0x23E	/* Debug Save and Restore Register 0 */
#define SPRN_DSRR1	0x23F	/* Debug Save and Restore Register 1 */
#define SPRN_SPRG8	0x25C	/* Special Purpose Register General 8 */
#define SPRN_SPRG9	0x25D	/* Special Purpose Register General 9 */
#define SPRN_L1CSR2	0x25E	/* L1 Cache Control and Status Register 2 */
#define SPRN_MAS0	0x270	/* MMU Assist Register 0 */
#define SPRN_MAS1	0x271	/* MMU Assist Register 1 */
#define SPRN_MAS2	0x272	/* MMU Assist Register 2 */
#define SPRN_MAS3	0x273	/* MMU Assist Register 3 */
#define SPRN_MAS4	0x274	/* MMU Assist Register 4 */
#define SPRN_MAS5	0x153	/* MMU Assist Register 5 */
#define SPRN_MAS6	0x276	/* MMU Assist Register 6 */
#define SPRN_PID1	0x279	/* Process ID Register 1 */
#define SPRN_PID2	0x27A	/* Process ID Register 2 */
#define SPRN_TLB0CFG	0x2B0	/* TLB 0 Config Register */
#define SPRN_TLB1CFG	0x2B1	/* TLB 1 Config Register */
#define SPRN_TLB2CFG	0x2B2	/* TLB 2 Config Register */
#define SPRN_TLB3CFG	0x2B3	/* TLB 3 Config Register */
#define SPRN_EPR	0x2BE	/* External Proxy Register */
#define SPRN_CCR1	0x378	/* Core Configuration Register 1 */
#define SPRN_ZPR	0x3B0	/* Zone Protection Register (40x) */
#define SPRN_MAS7	0x3B0	/* MMU Assist Register 7 */
#define SPRN_MMUCR	0x3B2	/* MMU Control Register */
#define SPRN_CCR0	0x3B3	/* Core Configuration Register 0 */
#define SPRN_EPLC	0x3B3	/* External Process ID Load Context */
#define SPRN_EPSC	0x3B4	/* External Process ID Store Context */
#define SPRN_SGR	0x3B9	/* Storage Guarded Register */
#define SPRN_DCWR	0x3BA	/* Data Cache Write-thru Register */
#define SPRN_SLER	0x3BB	/* Little-endian real mode */
#define SPRN_SU0R	0x3BC	/* "User 0" real mode (40x) */
#define SPRN_DCMP	0x3D1	/* Data TLB Compare Register */
#define SPRN_ICDBDR	0x3D3	/* Instruction Cache Debug Data Register */
#define SPRN_EVPR	0x3D6	/* Exception Vector Prefix Register */
#define SPRN_L1CSR0	0x3F2	/* L1 Cache Control and Status Register 0 */
#define SPRN_L1CSR1	0x3F3	/* L1 Cache Control and Status Register 1 */
#define SPRN_MMUCSR0	0x3F4	/* MMU Control and Status Register 0 */
#define SPRN_MMUCFG	0x3F7	/* MMU Configuration Register */
#define SPRN_PIT	0x3DB	/* Programmable Interval Timer */
#define SPRN_BUCSR	0x3F5	/* Branch Unit Control and Status */
#define SPRN_L2CSR0	0x3F9	/* L2 Data Cache Control and Status Register 0 */
#define SPRN_L2CSR1	0x3FA	/* L2 Data Cache Control and Status Register 1 */
#define SPRN_DCCR	0x3FA	/* Data Cache Cacheability Register */
#define SPRN_ICCR	0x3FB	/* Instruction Cache Cacheability Register */
#define SPRN_PWRMGTCR0	0x3FB	/* Power management control register 0 */
#define SPRN_SVR	0x3FF	/* System Version Register */

/*
 * SPRs which have conflicting definitions on true Book E versus classic,
 * or IBM 40x.
 */
#ifdef CONFIG_BOOKE
#define SPRN_CSRR0	0x03A	/* Critical Save and Restore Register 0 */
#define SPRN_CSRR1	0x03B	/* Critical Save and Restore Register 1 */
#define SPRN_DEAR	0x03D	/* Data Error Address Register */
#define SPRN_ESR	0x03E	/* Exception Syndrome Register */
#define SPRN_PIR	0x11E	/* Processor Identification Register */
#define SPRN_DBSR	0x130	/* Debug Status Register */
#define SPRN_DBCR0	0x134	/* Debug Control Register 0 */
#define SPRN_DBCR1	0x135	/* Debug Control Register 1 */
#define SPRN_IAC1	0x138	/* Instruction Address Compare 1 */
#define SPRN_IAC2	0x139	/* Instruction Address Compare 2 */
#define SPRN_DAC1	0x13C	/* Data Address Compare 1 */
#define SPRN_DAC2	0x13D	/* Data Address Compare 2 */
#define SPRN_TSR	0x150	/* Timer Status Register */
#define SPRN_TCR	0x154	/* Timer Control Register */
#endif /* Book E */
#ifdef CONFIG_40x
#define SPRN_DBCR1	0x3BD	/* Debug Control Register 1 */		
#define SPRN_ESR	0x3D4	/* Exception Syndrome Register */
#define SPRN_DEAR	0x3D5	/* Data Error Address Register */
#define SPRN_TSR	0x3D8	/* Timer Status Register */
#define SPRN_TCR	0x3DA	/* Timer Control Register */
#define SPRN_SRR2	0x3DE	/* Save/Restore Register 2 */
#define SPRN_SRR3	0x3DF	/* Save/Restore Register 3 */
#define SPRN_DBSR	0x3F0	/* Debug Status Register */		
#define SPRN_DBCR0	0x3F2	/* Debug Control Register 0 */
#define SPRN_DAC1	0x3F6	/* Data Address Compare 1 */
#define SPRN_DAC2	0x3F7	/* Data Address Compare 2 */
#define SPRN_CSRR0	SPRN_SRR2 /* Critical Save and Restore Register 0 */
#define SPRN_CSRR1	SPRN_SRR3 /* Critical Save and Restore Register 1 */
#endif
#define SPRN_HACOP	0x15F	/* Hypervisor Available Coprocessor Register */

/* Bit definitions for CCR1. */
#define	CCR1_DPC	0x00000100 /* Disable L1 I-Cache/D-Cache parity checking */
#define	CCR1_TCS	0x00000080 /* Timer Clock Select */

/* Bit definitions for PWRMGTCR0. */
#define PWRMGTCR0_PW20_WAIT		(1 << 14) /* PW20 state enable bit */
#define PWRMGTCR0_PW20_ENT_SHIFT	8
#define PWRMGTCR0_PW20_ENT		0x3F00
#define PWRMGTCR0_AV_IDLE_PD_EN		(1 << 22) /* Altivec idle enable */
#define PWRMGTCR0_AV_IDLE_CNT_SHIFT	16
#define PWRMGTCR0_AV_IDLE_CNT		0x3F0000

/* Bit definitions for the MCSR. */
#define MCSR_MCS	0x80000000 /* Machine Check Summary */
#define MCSR_IB		0x40000000 /* Instruction PLB Error */
#define MCSR_DRB	0x20000000 /* Data Read PLB Error */
#define MCSR_DWB	0x10000000 /* Data Write PLB Error */
#define MCSR_TLBP	0x08000000 /* TLB Parity Error */
#define MCSR_ICP	0x04000000 /* I-Cache Parity Error */
#define MCSR_DCSP	0x02000000 /* D-Cache Search Parity Error */
#define MCSR_DCFP	0x01000000 /* D-Cache Flush Parity Error */
#define MCSR_IMPE	0x00800000 /* Imprecise Machine Check Exception */

#define PPC47x_MCSR_GPR	0x01000000 /* GPR parity error */
#define PPC47x_MCSR_FPR	0x00800000 /* FPR parity error */
#define PPC47x_MCSR_IPR	0x00400000 /* Imprecise Machine Check Exception */

#ifdef CONFIG_E500
/* All e500 */
#define MCSR_MCP 	0x80000000UL /* Machine Check Input Pin */
#define MCSR_ICPERR 	0x40000000UL /* I-Cache Parity Error */

/* e500v1/v2 */
#define MCSR_DCP_PERR 	0x20000000UL /* D-Cache Push Parity Error */
#define MCSR_DCPERR 	0x10000000UL /* D-Cache Parity Error */
#define MCSR_BUS_IAERR 	0x00000080UL /* Instruction Address Error */
#define MCSR_BUS_RAERR 	0x00000040UL /* Read Address Error */
#define MCSR_BUS_WAERR 	0x00000020UL /* Write Address Error */
#define MCSR_BUS_IBERR 	0x00000010UL /* Instruction Data Error */
#define MCSR_BUS_RBERR 	0x00000008UL /* Read Data Bus Error */
#define MCSR_BUS_WBERR 	0x00000004UL /* Write Data Bus Error */
#define MCSR_BUS_IPERR 	0x00000002UL /* Instruction parity Error */
#define MCSR_BUS_RPERR 	0x00000001UL /* Read parity Error */

/* e500mc */
#define MCSR_DCPERR_MC	0x20000000UL /* D-Cache Parity Error */
#define MCSR_L2MMU_MHIT	0x08000000UL /* Hit on multiple TLB entries */
#define MCSR_NMI	0x00100000UL /* Non-Maskable Interrupt */
#define MCSR_MAV	0x00080000UL /* MCAR address valid */
#define MCSR_MEA	0x00040000UL /* MCAR is effective address */
#define MCSR_IF		0x00010000UL /* Instruction Fetch */
#define MCSR_LD		0x00008000UL /* Load */
#define MCSR_ST		0x00004000UL /* Store */
#define MCSR_LDG	0x00002000UL /* Guarded Load */
#define MCSR_TLBSYNC	0x00000002UL /* Multiple tlbsyncs detected */
#define MCSR_BSL2_ERR	0x00000001UL /* Backside L2 cache error */

#define MSRP_UCLEP	0x04000000 /* Protect MSR[UCLE] */
#define MSRP_DEP	0x00000200 /* Protect MSR[DE] */
#define MSRP_PMMP	0x00000004 /* Protect MSR[PMM] */
#endif

#ifdef CONFIG_E200
#define MCSR_MCP 	0x80000000UL /* Machine Check Input Pin */
#define MCSR_CP_PERR 	0x20000000UL /* Cache Push Parity Error */
#define MCSR_CPERR 	0x10000000UL /* Cache Parity Error */
#define MCSR_EXCP_ERR 	0x08000000UL /* ISI, ITLB, or Bus Error on 1st insn
					fetch for an exception handler */
#define MCSR_BUS_IRERR 	0x00000010UL /* Read Bus Error on instruction fetch*/
#define MCSR_BUS_DRERR 	0x00000008UL /* Read Bus Error on data load */
#define MCSR_BUS_WRERR 	0x00000004UL /* Write Bus Error on buffered
					store or cache line push */
#endif

/* Bit definitions for the HID1 */
#ifdef CONFIG_E500
/* e500v1/v2 */
#define HID1_PLL_CFG_MASK 0xfc000000	/* PLL_CFG input pins */
#define HID1_RFXE	0x00020000	/* Read fault exception enable */
#define HID1_R1DPE	0x00008000	/* R1 data bus parity enable */
#define HID1_R2DPE	0x00004000	/* R2 data bus parity enable */
#define HID1_ASTME	0x00002000	/* Address bus streaming mode enable */
#define HID1_ABE	0x00001000	/* Address broadcast enable */
#define HID1_MPXTT	0x00000400	/* MPX re-map transfer type */
#define HID1_ATS	0x00000080	/* Atomic status */
#define HID1_MID_MASK	0x0000000f	/* MID input pins */
#endif

/* Bit definitions for the DBSR. */
/*
 * DBSR bits which have conflicting definitions on true Book E versus IBM 40x.
 */
#ifdef CONFIG_BOOKE
#define DBSR_IDE	0x80000000	/* Imprecise Debug Event */
#define DBSR_MRR	0x30000000	/* Most Recent Reset */
#define DBSR_IC		0x08000000	/* Instruction Completion */
#define DBSR_BT		0x04000000	/* Branch Taken */
#define DBSR_IRPT	0x02000000	/* Exception Debug Event */
#define DBSR_TIE	0x01000000	/* Trap Instruction Event */
#define DBSR_IAC1	0x00800000	/* Instr Address Compare 1 Event */
#define DBSR_IAC2	0x00400000	/* Instr Address Compare 2 Event */
#define DBSR_IAC3	0x00200000	/* Instr Address Compare 3 Event */
#define DBSR_IAC4	0x00100000	/* Instr Address Compare 4 Event */
#define DBSR_DAC1R	0x00080000	/* Data Addr Compare 1 Read Event */
#define DBSR_DAC1W	0x00040000	/* Data Addr Compare 1 Write Event */
#define DBSR_DAC2R	0x00020000	/* Data Addr Compare 2 Read Event */
#define DBSR_DAC2W	0x00010000	/* Data Addr Compare 2 Write Event */
#define DBSR_RET	0x00008000	/* Return Debug Event */
#define DBSR_CIRPT	0x00000040	/* Critical Interrupt Taken Event */
#define DBSR_CRET	0x00000020	/* Critical Return Debug Event */
#define DBSR_IAC12ATS	0x00000002	/* Instr Address Compare 1/2 Toggle */
#define DBSR_IAC34ATS	0x00000001	/* Instr Address Compare 3/4 Toggle */
#endif
#ifdef CONFIG_40x
#define DBSR_IC		0x80000000	/* Instruction Completion */
#define DBSR_BT		0x40000000	/* Branch taken */
#define DBSR_IRPT	0x20000000	/* Exception Debug Event */
#define DBSR_TIE	0x10000000	/* Trap Instruction debug Event */
#define DBSR_IAC1	0x04000000	/* Instruction Address Compare 1 Event */
#define DBSR_IAC2	0x02000000	/* Instruction Address Compare 2 Event */
#define DBSR_IAC3	0x00080000	/* Instruction Address Compare 3 Event */
#define DBSR_IAC4	0x00040000	/* Instruction Address Compare 4 Event */
#define DBSR_DAC1R	0x01000000	/* Data Address Compare 1 Read Event */
#define DBSR_DAC1W	0x00800000	/* Data Address Compare 1 Write Event */
#define DBSR_DAC2R	0x00400000	/* Data Address Compare 2 Read Event */
#define DBSR_DAC2W	0x00200000	/* Data Address Compare 2 Write Event */
#endif

/* Bit definitions related to the ESR. */
#define ESR_MCI		0x80000000	/* Machine Check - Instruction */
#define ESR_IMCP	0x80000000	/* Instr. Machine Check - Protection */
#define ESR_IMCN	0x40000000	/* Instr. Machine Check - Non-config */
#define ESR_IMCB	0x20000000	/* Instr. Machine Check - Bus error */
#define ESR_IMCT	0x10000000	/* Instr. Machine Check - Timeout */
#define ESR_PIL		0x08000000	/* Program Exception - Illegal */
#define ESR_PPR		0x04000000	/* Program Exception - Privileged */
#define ESR_PTR		0x02000000	/* Program Exception - Trap */
#define ESR_FP		0x01000000	/* Floating Point Operation */
#define ESR_DST		0x00800000	/* Storage Exception - Data miss */
#define ESR_DIZ		0x00400000	/* Storage Exception - Zone fault */
#define ESR_ST		0x00800000	/* Store Operation */
#define ESR_DLK		0x00200000	/* Data Cache Locking */
#define ESR_ILK		0x00100000	/* Instr. Cache Locking */
#define ESR_PUO		0x00040000	/* Unimplemented Operation exception */
#define ESR_BO		0x00020000	/* Byte Ordering */
#define ESR_SPV		0x00000080	/* Signal Processing operation */

/* Bit definitions related to the DBCR0. */
#if defined(CONFIG_40x)
#define DBCR0_EDM	0x80000000	/* External Debug Mode */
#define DBCR0_IDM	0x40000000	/* Internal Debug Mode */
#define DBCR0_RST	0x30000000	/* all the bits in the RST field */
#define DBCR0_RST_SYSTEM 0x30000000	/* System Reset */
#define DBCR0_RST_CHIP	0x20000000	/* Chip Reset */
#define DBCR0_RST_CORE	0x10000000	/* Core Reset */
#define DBCR0_RST_NONE	0x00000000	/* No Reset */
#define DBCR0_IC	0x08000000	/* Instruction Completion */
#define DBCR0_ICMP	DBCR0_IC
#define DBCR0_BT	0x04000000	/* Branch Taken */
#define DBCR0_BRT	DBCR0_BT
#define DBCR0_EDE	0x02000000	/* Exception Debug Event */
#define DBCR0_IRPT	DBCR0_EDE
#define DBCR0_TDE	0x01000000	/* TRAP Debug Event */
#define DBCR0_IA1	0x00800000	/* Instr Addr compare 1 enable */
#define DBCR0_IAC1	DBCR0_IA1
#define DBCR0_IA2	0x00400000	/* Instr Addr compare 2 enable */
#define DBCR0_IAC2	DBCR0_IA2
#define DBCR0_IA12	0x00200000	/* Instr Addr 1-2 range enable */
#define DBCR0_IA12X	0x00100000	/* Instr Addr 1-2 range eXclusive */
#define DBCR0_IA3	0x00080000	/* Instr Addr compare 3 enable */
#define DBCR0_IAC3	DBCR0_IA3
#define DBCR0_IA4	0x00040000	/* Instr Addr compare 4 enable */
#define DBCR0_IAC4	DBCR0_IA4
#define DBCR0_IA34	0x00020000	/* Instr Addr 3-4 range Enable */
#define DBCR0_IA34X	0x00010000	/* Instr Addr 3-4 range eXclusive */
#define DBCR0_IA12T	0x00008000	/* Instr Addr 1-2 range Toggle */
#define DBCR0_IA34T	0x00004000	/* Instr Addr 3-4 range Toggle */
#define DBCR0_FT	0x00000001	/* Freeze Timers on debug event */

#define dbcr_iac_range(task)	((task)->thread.debug.dbcr0)
#define DBCR_IAC12I	DBCR0_IA12			/* Range Inclusive */
#define DBCR_IAC12X	(DBCR0_IA12 | DBCR0_IA12X)	/* Range Exclusive */
#define DBCR_IAC12MODE	(DBCR0_IA12 | DBCR0_IA12X)	/* IAC 1-2 Mode Bits */
#define DBCR_IAC34I	DBCR0_IA34			/* Range Inclusive */
#define DBCR_IAC34X	(DBCR0_IA34 | DBCR0_IA34X)	/* Range Exclusive */
#define DBCR_IAC34MODE	(DBCR0_IA34 | DBCR0_IA34X)	/* IAC 3-4 Mode Bits */

/* Bit definitions related to the DBCR1. */
#define DBCR1_DAC1R	0x80000000	/* DAC1 Read Debug Event */
#define DBCR1_DAC2R	0x40000000	/* DAC2 Read Debug Event */
#define DBCR1_DAC1W	0x20000000	/* DAC1 Write Debug Event */
#define DBCR1_DAC2W	0x10000000	/* DAC2 Write Debug Event */

#define dbcr_dac(task)	((task)->thread.debug.dbcr1)
#define DBCR_DAC1R	DBCR1_DAC1R
#define DBCR_DAC1W	DBCR1_DAC1W
#define DBCR_DAC2R	DBCR1_DAC2R
#define DBCR_DAC2W	DBCR1_DAC2W

/*
 * Are there any active Debug Events represented in the
 * Debug Control Registers?
 */
#define DBCR0_ACTIVE_EVENTS	(DBCR0_ICMP | DBCR0_IAC1 | DBCR0_IAC2 | \
				 DBCR0_IAC3 | DBCR0_IAC4)
#define DBCR1_ACTIVE_EVENTS	(DBCR1_DAC1R | DBCR1_DAC2R | \
				 DBCR1_DAC1W | DBCR1_DAC2W)
#define DBCR_ACTIVE_EVENTS(dbcr0, dbcr1)  (((dbcr0) & DBCR0_ACTIVE_EVENTS) || \
					   ((dbcr1) & DBCR1_ACTIVE_EVENTS))

#elif defined(CONFIG_BOOKE)
#define DBCR0_EDM	0x80000000	/* External Debug Mode */
#define DBCR0_IDM	0x40000000	/* Internal Debug Mode */
#define DBCR0_RST	0x30000000	/* all the bits in the RST field */
/* DBCR0_RST_* is 44x specific and not followed in fsl booke */
#define DBCR0_RST_SYSTEM 0x30000000	/* System Reset */
#define DBCR0_RST_CHIP	0x20000000	/* Chip Reset */
#define DBCR0_RST_CORE	0x10000000	/* Core Reset */
#define DBCR0_RST_NONE	0x00000000	/* No Reset */
#define DBCR0_ICMP	0x08000000	/* Instruction Completion */
#define DBCR0_IC	DBCR0_ICMP
#define DBCR0_BRT	0x04000000	/* Branch Taken */
#define DBCR0_BT	DBCR0_BRT
#define DBCR0_IRPT	0x02000000	/* Exception Debug Event */
#define DBCR0_TDE	0x01000000	/* TRAP Debug Event */
#define DBCR0_TIE	DBCR0_TDE
#define DBCR0_IAC1	0x00800000	/* Instr Addr compare 1 enable */
#define DBCR0_IAC2	0x00400000	/* Instr Addr compare 2 enable */
#define DBCR0_IAC3	0x00200000	/* Instr Addr compare 3 enable */
#define DBCR0_IAC4	0x00100000	/* Instr Addr compare 4 enable */
#define DBCR0_DAC1R	0x00080000	/* DAC 1 Read enable */
#define DBCR0_DAC1W	0x00040000	/* DAC 1 Write enable */
#define DBCR0_DAC2R	0x00020000	/* DAC 2 Read enable */
#define DBCR0_DAC2W	0x00010000	/* DAC 2 Write enable */
#define DBCR0_RET	0x00008000	/* Return Debug Event */
#define DBCR0_CIRPT	0x00000040	/* Critical Interrupt Taken Event */
#define DBCR0_CRET	0x00000020	/* Critical Return Debug Event */
#define DBCR0_FT	0x00000001	/* Freeze Timers on debug event */

#define dbcr_dac(task)	((task)->thread.debug.dbcr0)
#define DBCR_DAC1R	DBCR0_DAC1R
#define DBCR_DAC1W	DBCR0_DAC1W
#define DBCR_DAC2R	DBCR0_DAC2R
#define DBCR_DAC2W	DBCR0_DAC2W

/* Bit definitions related to the DBCR1. */
#define DBCR1_IAC1US	0xC0000000	/* Instr Addr Cmp 1 Sup/User   */
#define DBCR1_IAC1ER	0x30000000	/* Instr Addr Cmp 1 Eff/Real */
#define DBCR1_IAC1ER_01	0x10000000	/* reserved */
#define DBCR1_IAC1ER_10	0x20000000	/* Instr Addr Cmp 1 Eff/Real MSR[IS]=0 */
#define DBCR1_IAC1ER_11	0x30000000	/* Instr Addr Cmp 1 Eff/Real MSR[IS]=1 */
#define DBCR1_IAC2US	0x0C000000	/* Instr Addr Cmp 2 Sup/User   */
#define DBCR1_IAC2ER	0x03000000	/* Instr Addr Cmp 2 Eff/Real */
#define DBCR1_IAC2ER_01	0x01000000	/* reserved */
#define DBCR1_IAC2ER_10	0x02000000	/* Instr Addr Cmp 2 Eff/Real MSR[IS]=0 */
#define DBCR1_IAC2ER_11	0x03000000	/* Instr Addr Cmp 2 Eff/Real MSR[IS]=1 */
#define DBCR1_IAC12M	0x00800000	/* Instr Addr 1-2 range enable */
#define DBCR1_IAC12MX	0x00C00000	/* Instr Addr 1-2 range eXclusive */
#define DBCR1_IAC12AT	0x00010000	/* Instr Addr 1-2 range Toggle */
#define DBCR1_IAC3US	0x0000C000	/* Instr Addr Cmp 3 Sup/User   */
#define DBCR1_IAC3ER	0x00003000	/* Instr Addr Cmp 3 Eff/Real */
#define DBCR1_IAC3ER_01	0x00001000	/* reserved */
#define DBCR1_IAC3ER_10	0x00002000	/* Instr Addr Cmp 3 Eff/Real MSR[IS]=0 */
#define DBCR1_IAC3ER_11	0x00003000	/* Instr Addr Cmp 3 Eff/Real MSR[IS]=1 */
#define DBCR1_IAC4US	0x00000C00	/* Instr Addr Cmp 4 Sup/User   */
#define DBCR1_IAC4ER	0x00000300	/* Instr Addr Cmp 4 Eff/Real */
#define DBCR1_IAC4ER_01	0x00000100	/* Instr Addr Cmp 4 Eff/Real MSR[IS]=0 */
#define DBCR1_IAC4ER_10	0x00000200	/* Instr Addr Cmp 4 Eff/Real MSR[IS]=0 */
#define DBCR1_IAC4ER_11	0x00000300	/* Instr Addr Cmp 4 Eff/Real MSR[IS]=1 */
#define DBCR1_IAC34M	0x00000080	/* Instr Addr 3-4 range enable */
#define DBCR1_IAC34MX	0x000000C0	/* Instr Addr 3-4 range eXclusive */
#define DBCR1_IAC34AT	0x00000001	/* Instr Addr 3-4 range Toggle */

#define dbcr_iac_range(task)	((task)->thread.debug.dbcr1)
#define DBCR_IAC12I	DBCR1_IAC12M	/* Range Inclusive */
#define DBCR_IAC12X	DBCR1_IAC12MX	/* Range Exclusive */
#define DBCR_IAC12MODE	DBCR1_IAC12MX	/* IAC 1-2 Mode Bits */
#define DBCR_IAC34I	DBCR1_IAC34M	/* Range Inclusive */
#define DBCR_IAC34X	DBCR1_IAC34MX	/* Range Exclusive */
#define DBCR_IAC34MODE	DBCR1_IAC34MX	/* IAC 3-4 Mode Bits */

/* Bit definitions related to the DBCR2. */
#define DBCR2_DAC1US	0xC0000000	/* Data Addr Cmp 1 Sup/User   */
#define DBCR2_DAC1ER	0x30000000	/* Data Addr Cmp 1 Eff/Real */
#define DBCR2_DAC2US	0x0C000000	/* Data Addr Cmp 2 Sup/User   */
#define DBCR2_DAC2ER	0x03000000	/* Data Addr Cmp 2 Eff/Real */
#define DBCR2_DAC12M	0x00800000	/* DAC 1-2 range enable */
#define DBCR2_DAC12MM	0x00400000	/* DAC 1-2 Mask mode*/
#define DBCR2_DAC12MX	0x00C00000	/* DAC 1-2 range eXclusive */
#define DBCR2_DAC12MODE	0x00C00000	/* DAC 1-2 Mode Bits */
#define DBCR2_DAC12A	0x00200000	/* DAC 1-2 Asynchronous */
#define DBCR2_DVC1M	0x000C0000	/* Data Value Comp 1 Mode */
#define DBCR2_DVC1M_SHIFT	18	/* # of bits to shift DBCR2_DVC1M */
#define DBCR2_DVC2M	0x00030000	/* Data Value Comp 2 Mode */
#define DBCR2_DVC2M_SHIFT	16	/* # of bits to shift DBCR2_DVC2M */
#define DBCR2_DVC1BE	0x00000F00	/* Data Value Comp 1 Byte */
#define DBCR2_DVC1BE_SHIFT	8	/* # of bits to shift DBCR2_DVC1BE */
#define DBCR2_DVC2BE	0x0000000F	/* Data Value Comp 2 Byte */
#define DBCR2_DVC2BE_SHIFT	0	/* # of bits to shift DBCR2_DVC2BE */

/*
 * Are there any active Debug Events represented in the
 * Debug Control Registers?
 */
#define DBCR0_ACTIVE_EVENTS  (DBCR0_ICMP | DBCR0_IAC1 | DBCR0_IAC2 | \
			      DBCR0_IAC3 | DBCR0_IAC4 | DBCR0_DAC1R | \
			      DBCR0_DAC1W  | DBCR0_DAC2R | DBCR0_DAC2W)
#define DBCR1_ACTIVE_EVENTS	0

#define DBCR_ACTIVE_EVENTS(dbcr0, dbcr1)  (((dbcr0) & DBCR0_ACTIVE_EVENTS) || \
					   ((dbcr1) & DBCR1_ACTIVE_EVENTS))
#endif /* #elif defined(CONFIG_BOOKE) */

/* Bit definitions related to the TCR. */
#define TCR_WP(x)	(((x)&0x3)<<30)	/* WDT Period */
#define TCR_WP_MASK	TCR_WP(3)
#define WP_2_17		0		/* 2^17 clocks */
#define WP_2_21		1		/* 2^21 clocks */
#define WP_2_25		2		/* 2^25 clocks */
#define WP_2_29		3		/* 2^29 clocks */
#define TCR_WRC(x)	(((x)&0x3)<<28)	/* WDT Reset Control */
#define TCR_WRC_MASK	TCR_WRC(3)
#define WRC_NONE	0		/* No reset will occur */
#define WRC_CORE	1		/* Core reset will occur */
#define WRC_CHIP	2		/* Chip reset will occur */
#define WRC_SYSTEM	3		/* System reset will occur */
#define TCR_WIE		0x08000000	/* WDT Interrupt Enable */
#define TCR_PIE		0x04000000	/* PIT Interrupt Enable */
#define TCR_DIE		TCR_PIE		/* DEC Interrupt Enable */
#define TCR_FP(x)	(((x)&0x3)<<24)	/* FIT Period */
#define TCR_FP_MASK	TCR_FP(3)
#define FP_2_9		0		/* 2^9 clocks */
#define FP_2_13		1		/* 2^13 clocks */
#define FP_2_17		2		/* 2^17 clocks */
#define FP_2_21		3		/* 2^21 clocks */
#define TCR_FIE		0x00800000	/* FIT Interrupt Enable */
#define TCR_ARE		0x00400000	/* Auto Reload Enable */

#ifdef CONFIG_E500
#define TCR_GET_WP(tcr)  ((((tcr) & 0xC0000000) >> 30) | \
			      (((tcr) & 0x1E0000) >> 15))
#else
#define TCR_GET_WP(tcr)  (((tcr) & 0xC0000000) >> 30)
#endif

/* Bit definitions for the TSR. */
#define TSR_ENW		0x80000000	/* Enable Next Watchdog */
#define TSR_WIS		0x40000000	/* WDT Interrupt Status */
#define TSR_WRS(x)	(((x)&0x3)<<28)	/* WDT Reset Status */
#define WRS_NONE	0		/* No WDT reset occurred */
#define WRS_CORE	1		/* WDT forced core reset */
#define WRS_CHIP	2		/* WDT forced chip reset */
#define WRS_SYSTEM	3		/* WDT forced system reset */
#define TSR_PIS		0x08000000	/* PIT Interrupt Status */
#define TSR_DIS		TSR_PIS		/* DEC Interrupt Status */
#define TSR_FIS		0x04000000	/* FIT Interrupt Status */

/* Bit definitions for the DCCR. */
#define DCCR_NOCACHE	0		/* Noncacheable */
#define DCCR_CACHE	1		/* Cacheable */

/* Bit definitions for DCWR. */
#define DCWR_COPY	0		/* Copy-back */
#define DCWR_WRITE	1		/* Write-through */

/* Bit definitions for ICCR. */
#define ICCR_NOCACHE	0		/* Noncacheable */
#define ICCR_CACHE	1		/* Cacheable */

/* Bit definitions for L1CSR0. */
#define L1CSR0_CPE	0x00010000	/* Data Cache Parity Enable */
#define L1CSR0_CUL	0x00000400	/* Data Cache Unable to Lock */
#define L1CSR0_CLFC	0x00000100	/* Cache Lock Bits Flash Clear */
#define L1CSR0_DCFI	0x00000002	/* Data Cache Flash Invalidate */
#define L1CSR0_CFI	0x00000002	/* Cache Flash Invalidate */
#define L1CSR0_DCE	0x00000001	/* Data Cache Enable */

/* Bit definitions for L1CSR1. */
#define L1CSR1_CPE	0x00010000	/* Instruction Cache Parity Enable */
#define L1CSR1_ICLFR	0x00000100	/* Instr Cache Lock Bits Flash Reset */
#define L1CSR1_ICFI	0x00000002	/* Instr Cache Flash Invalidate */
#define L1CSR1_ICE	0x00000001	/* Instr Cache Enable */

/* Bit definitions for L1CSR2. */
#define L1CSR2_DCWS	0x40000000	/* Data Cache write shadow */

/* Bit definitions for BUCSR. */
#define BUCSR_STAC_EN	0x01000000	/* Segment Target Address Cache */
#define BUCSR_LS_EN	0x00400000	/* Link Stack */
#define BUCSR_BBFI	0x00000200	/* Branch Buffer flash invalidate */
#define BUCSR_BPEN	0x00000001	/* Branch prediction enable */
#define BUCSR_INIT	(BUCSR_STAC_EN | BUCSR_LS_EN | BUCSR_BBFI | BUCSR_BPEN)

/* Bit definitions for L2CSR0. */
#define L2CSR0_L2E	0x80000000	/* L2 Cache Enable */
#define L2CSR0_L2PE	0x40000000	/* L2 Cache Parity/ECC Enable */
#define L2CSR0_L2WP	0x1c000000	/* L2 I/D Way Partioning */
#define L2CSR0_L2CM	0x03000000	/* L2 Cache Coherency Mode */
#define L2CSR0_L2FI	0x00200000	/* L2 Cache Flash Invalidate */
#define L2CSR0_L2IO	0x00100000	/* L2 Cache Instruction Only */
#define L2CSR0_L2DO	0x00010000	/* L2 Cache Data Only */
#define L2CSR0_L2REP	0x00003000	/* L2 Line Replacement Algo */
#define L2CSR0_L2FL	0x00000800	/* L2 Cache Flush */
#define L2CSR0_L2LFC	0x00000400	/* L2 Cache Lock Flash Clear */
#define L2CSR0_L2LOA	0x00000080	/* L2 Cache Lock Overflow Allocate */
#define L2CSR0_L2LO	0x00000020	/* L2 Cache Lock Overflow */

/* Bit definitions for SGR. */
#define SGR_NORMAL	0		/* Speculative fetching allowed. */
#define SGR_GUARDED	1		/* Speculative fetching disallowed. */

/* Bit definitions for EPCR */
#define SPRN_EPCR_EXTGS		0x80000000	/* External Input interrupt
						 * directed to Guest state */
#define SPRN_EPCR_DTLBGS	0x40000000	/* Data TLB Error interrupt
						 * directed to guest state */
#define SPRN_EPCR_ITLBGS	0x20000000	/* Instr. TLB error interrupt
						 * directed to guest state */
#define SPRN_EPCR_DSIGS		0x10000000	/* Data Storage interrupt
						 * directed to guest state */
#define SPRN_EPCR_ISIGS		0x08000000	/* Instr. Storage interrupt
						 * directed to guest state */
#define SPRN_EPCR_DUVD		0x04000000	/* Disable Hypervisor Debug */
#define SPRN_EPCR_ICM		0x02000000	/* Interrupt computation mode
						 * (copied to MSR:CM on intr) */
#define SPRN_EPCR_GICM		0x01000000	/* Guest Interrupt Comp. mode */
#define SPRN_EPCR_DGTMI		0x00800000	/* Disable TLB Guest Management
						 * instructions */
#define SPRN_EPCR_DMIUH		0x00400000	/* Disable MAS Interrupt updates
						 * for hypervisor */

/* Bit definitions for EPLC/EPSC */
#define EPC_EPR		0x80000000 /* 1 = user, 0 = kernel */
#define EPC_EPR_SHIFT	31
#define EPC_EAS		0x40000000 /* Address Space */
#define EPC_EAS_SHIFT	30
#define EPC_EGS		0x20000000 /* 1 = guest, 0 = hypervisor */
#define EPC_EGS_SHIFT	29
#define EPC_ELPID	0x00ff0000
#define EPC_ELPID_SHIFT	16
#define EPC_EPID	0x00003fff
#define EPC_EPID_SHIFT	0

/*
 * The IBM-403 is an even more odd special case, as it is much
 * older than the IBM-405 series.  We put these down here incase someone
 * wishes to support these machines again.
 */
#ifdef CONFIG_403GCX
/* Special Purpose Registers (SPRNs)*/
#define SPRN_TBHU	0x3CC	/* Time Base High User-mode */
#define SPRN_TBLU	0x3CD	/* Time Base Low User-mode */
#define SPRN_CDBCR	0x3D7	/* Cache Debug Control Register */
#define SPRN_TBHI	0x3DC	/* Time Base High */
#define SPRN_TBLO	0x3DD	/* Time Base Low */
#define SPRN_DBCR	0x3F2	/* Debug Control Register */
#define SPRN_PBL1	0x3FC	/* Protection Bound Lower 1 */
#define SPRN_PBL2	0x3FE	/* Protection Bound Lower 2 */
#define SPRN_PBU1	0x3FD	/* Protection Bound Upper 1 */
#define SPRN_PBU2	0x3FF	/* Protection Bound Upper 2 */


/* Bit definitions for the DBCR. */
#define DBCR_EDM	DBCR0_EDM
#define DBCR_IDM	DBCR0_IDM
#define DBCR_RST(x)	(((x) & 0x3) << 28)
#define DBCR_RST_NONE	0
#define DBCR_RST_CORE	1
#define DBCR_RST_CHIP	2
#define DBCR_RST_SYSTEM	3
#define DBCR_IC		DBCR0_IC	/* Instruction Completion Debug Evnt */
#define DBCR_BT		DBCR0_BT	/* Branch Taken Debug Event */
#define DBCR_EDE	DBCR0_EDE	/* Exception Debug Event */
#define DBCR_TDE	DBCR0_TDE	/* TRAP Debug Event */
#define DBCR_FER	0x00F80000	/* First Events Remaining Mask */
#define DBCR_FT		0x00040000	/* Freeze Timers on Debug Event */
#define DBCR_IA1	0x00020000	/* Instr. Addr. Compare 1 Enable */
#define DBCR_IA2	0x00010000	/* Instr. Addr. Compare 2 Enable */
#define DBCR_D1R	0x00008000	/* Data Addr. Compare 1 Read Enable */
#define DBCR_D1W	0x00004000	/* Data Addr. Compare 1 Write Enable */
#define DBCR_D1S(x)	(((x) & 0x3) << 12)	/* Data Adrr. Compare 1 Size */
#define DAC_BYTE	0
#define DAC_HALF	1
#define DAC_WORD	2
#define DAC_QUAD	3
#define DBCR_D2R	0x00000800	/* Data Addr. Compare 2 Read Enable */
#define DBCR_D2W	0x00000400	/* Data Addr. Compare 2 Write Enable */
#define DBCR_D2S(x)	(((x) & 0x3) << 8)	/* Data Addr. Compare 2 Size */
#define DBCR_SBT	0x00000040	/* Second Branch Taken Debug Event */
#define DBCR_SED	0x00000020	/* Second Exception Debug Event */
#define DBCR_STD	0x00000010	/* Second Trap Debug Event */
#define DBCR_SIA	0x00000008	/* Second IAC Enable */
#define DBCR_SDA	0x00000004	/* Second DAC Enable */
#define DBCR_JOI	0x00000002	/* JTAG Serial Outbound Int. Enable */
#define DBCR_JII	0x00000001	/* JTAG Serial Inbound Int. Enable */
#endif /* 403GCX */

/* Some 476 specific registers */
#define SPRN_SSPCR		830
#define SPRN_USPCR		831
#define SPRN_ISPCR		829
#define SPRN_MMUBE0		820
#define MMUBE0_IBE0_SHIFT	24
#define MMUBE0_IBE1_SHIFT	16
#define MMUBE0_IBE2_SHIFT	8
#define MMUBE0_VBE0		0x00000004
#define MMUBE0_VBE1		0x00000002
#define MMUBE0_VBE2		0x00000001
#define SPRN_MMUBE1		821
#define MMUBE1_IBE3_SHIFT	24
#define MMUBE1_IBE4_SHIFT	16
#define MMUBE1_IBE5_SHIFT	8
#define MMUBE1_VBE3		0x00000004
#define MMUBE1_VBE4		0x00000002
#define MMUBE1_VBE5		0x00000001

#define TMRN_TMCFG0      16	/* Thread Management Configuration Register 0 */
#define TMRN_TMCFG0_NPRIBITS       0x003f0000 /* Bits of thread priority */
#define TMRN_TMCFG0_NPRIBITS_SHIFT 16
#define TMRN_TMCFG0_NATHRD         0x00003f00 /* Number of active threads */
#define TMRN_TMCFG0_NATHRD_SHIFT   8
#define TMRN_TMCFG0_NTHRD          0x0000003f /* Number of threads */
#define TMRN_IMSR0	0x120	/* Initial MSR Register 0 (e6500) */
#define TMRN_IMSR1	0x121	/* Initial MSR Register 1 (e6500) */
#define TMRN_INIA0	0x140	/* Next Instruction Address Register 0 */
#define TMRN_INIA1	0x141	/* Next Instruction Address Register 1 */
#define SPRN_TENSR	0x1b5	/* Thread Enable Status Register */
#define SPRN_TENS	0x1b6	/* Thread Enable Set Register */
#define SPRN_TENC	0x1b7	/* Thread Enable Clear Register */

#define TEN_THREAD(x)	(1 << (x))

#ifndef __ASSEMBLY__
#define mftmr(rn)	({unsigned long rval; \
			asm volatile(MFTMR(rn, %0) : "=r" (rval)); rval;})
#define mttmr(rn, v)	asm volatile(MTTMR(rn, %0) : \
				     : "r" ((unsigned long)(v)) \
				     : "memory")
#endif /* !__ASSEMBLY__ */

#endif /* __ASM_POWERPC_REG_BOOKE_H__ */
#endif /* __KERNEL__ */

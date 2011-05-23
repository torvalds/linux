/* MN10300 Core system registers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_CPU_REGS_H
#define _ASM_CPU_REGS_H

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

/* we tell the compiler to pretend to be AM33 so that it doesn't try and use
 * the FP regs, but tell the assembler that we're actually allowed AM33v2
 * instructions */
#ifndef __ASSEMBLY__
asm(" .am33_2\n");
#else
.am33_2
#endif

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#define __SYSREG(ADDR, TYPE) (*(volatile TYPE *)(ADDR))
#define __SYSREGC(ADDR, TYPE) (*(const volatile TYPE *)(ADDR))
#else
#define __SYSREG(ADDR, TYPE) ADDR
#define __SYSREGC(ADDR, TYPE) ADDR
#endif

/* CPU registers */
#define EPSW_FLAG_Z		0x00000001	/* zero flag */
#define EPSW_FLAG_N		0x00000002	/* negative flag */
#define EPSW_FLAG_C		0x00000004	/* carry flag */
#define EPSW_FLAG_V		0x00000008	/* overflow flag */
#define EPSW_IM			0x00000700	/* interrupt mode */
#define EPSW_IM_0		0x00000000	/* interrupt mode 0 */
#define EPSW_IM_1		0x00000100	/* interrupt mode 1 */
#define EPSW_IM_2		0x00000200	/* interrupt mode 2 */
#define EPSW_IM_3		0x00000300	/* interrupt mode 3 */
#define EPSW_IM_4		0x00000400	/* interrupt mode 4 */
#define EPSW_IM_5		0x00000500	/* interrupt mode 5 */
#define EPSW_IM_6		0x00000600	/* interrupt mode 6 */
#define EPSW_IM_7		0x00000700	/* interrupt mode 7 */
#define EPSW_IE			0x00000800	/* interrupt enable */
#define EPSW_S			0x00003000	/* software auxiliary bits */
#define EPSW_T			0x00008000	/* trace enable */
#define EPSW_nSL		0x00010000	/* not supervisor level */
#define EPSW_NMID		0x00020000	/* nonmaskable interrupt disable */
#define EPSW_nAR		0x00040000	/* register bank control */
#define EPSW_ML			0x00080000	/* monitor level */
#define EPSW_FE			0x00100000	/* FPU enable */
#define EPSW_IM_SHIFT		8		/* EPSW_IM_SHIFT determines the interrupt mode */

#define NUM2EPSW_IM(num)	((num) << EPSW_IM_SHIFT)

/* FPU registers */
#define FPCR_EF_I		0x00000001	/* inexact result FPU exception flag */
#define FPCR_EF_U		0x00000002	/* underflow FPU exception flag */
#define FPCR_EF_O		0x00000004	/* overflow FPU exception flag */
#define FPCR_EF_Z		0x00000008	/* zero divide FPU exception flag */
#define FPCR_EF_V		0x00000010	/* invalid operand FPU exception flag */
#define FPCR_EE_I		0x00000020	/* inexact result FPU exception enable */
#define FPCR_EE_U		0x00000040	/* underflow FPU exception enable */
#define FPCR_EE_O		0x00000080	/* overflow FPU exception enable */
#define FPCR_EE_Z		0x00000100	/* zero divide FPU exception enable */
#define FPCR_EE_V		0x00000200	/* invalid operand FPU exception enable */
#define FPCR_EC_I		0x00000400	/* inexact result FPU exception cause */
#define FPCR_EC_U		0x00000800	/* underflow FPU exception cause */
#define FPCR_EC_O		0x00001000	/* overflow FPU exception cause */
#define FPCR_EC_Z		0x00002000	/* zero divide FPU exception cause */
#define FPCR_EC_V		0x00004000	/* invalid operand FPU exception cause */
#define FPCR_RM			0x00030000	/* rounding mode */
#define FPCR_RM_NEAREST		0x00000000	/* - round to nearest value */
#define FPCR_FCC_U		0x00040000	/* FPU unordered condition code */
#define FPCR_FCC_E		0x00080000	/* FPU equal condition code */
#define FPCR_FCC_G		0x00100000	/* FPU greater than condition code */
#define FPCR_FCC_L		0x00200000	/* FPU less than condition code */
#define FPCR_INIT		0x00000000	/* no exceptions, rounding to nearest */

/* CPU control registers */
#define CPUP			__SYSREG(0xc0000020, u16)	/* CPU pipeline register */
#define CPUP_DWBD		0x0020		/* write buffer disable flag */
#define CPUP_IPFD		0x0040		/* instruction prefetch disable flag */
#define CPUP_EXM		0x0080		/* exception operation mode */
#define CPUP_EXM_AM33V1		0x0000		/* - AM33 v1 exception mode */
#define CPUP_EXM_AM33V2		0x0080		/* - AM33 v2 exception mode */

#define CPUM			__SYSREG(0xc0000040, u16)	/* CPU mode register */
#define CPUM_SLEEP		0x0004		/* set to enter sleep state */
#define CPUM_HALT		0x0008		/* set to enter halt state */
#define CPUM_STOP		0x0010		/* set to enter stop state */

#define CPUREV			__SYSREGC(0xc0000050, u32)	/* CPU revision register */
#define CPUREV_TYPE		0x0000000f	/* CPU type */
#define CPUREV_TYPE_S		0
#define CPUREV_TYPE_AM33_1	0x00000000	/* - AM33-1 core, AM33/1.00 arch */
#define CPUREV_TYPE_AM33_2	0x00000001	/* - AM33-2 core, AM33/2.00 arch */
#define CPUREV_TYPE_AM34_1	0x00000002	/* - AM34-1 core, AM33/2.00 arch */
#define CPUREV_TYPE_AM33_3	0x00000003	/* - AM33-3 core, AM33/2.00 arch */
#define CPUREV_TYPE_AM34_2	0x00000004	/* - AM34-2 core, AM33/3.00 arch */
#define CPUREV_REVISION		0x000000f0	/* CPU revision */
#define CPUREV_REVISION_S	4
#define CPUREV_ICWAY		0x00000f00	/* number of instruction cache ways */
#define CPUREV_ICWAY_S		8
#define CPUREV_ICSIZE		0x0000f000	/* instruction cache way size */
#define CPUREV_ICSIZE_S		12
#define CPUREV_DCWAY		0x000f0000	/* number of data cache ways */
#define CPUREV_DCWAY_S		16
#define CPUREV_DCSIZE		0x00f00000	/* data cache way size */
#define CPUREV_DCSIZE_S		20
#define CPUREV_FPUTYPE		0x0f000000	/* FPU core type */
#define CPUREV_FPUTYPE_NONE	0x00000000	/* - no FPU core implemented */
#define CPUREV_OCDCTG		0xf0000000	/* on-chip debug function category */

#define DCR			__SYSREG(0xc0000030, u16)	/* Debug control register */

/* interrupt/exception control registers */
#define IVAR0			__SYSREG(0xc0000000, u16)	/* interrupt vector 0 */
#define IVAR1			__SYSREG(0xc0000004, u16)	/* interrupt vector 1 */
#define IVAR2			__SYSREG(0xc0000008, u16)	/* interrupt vector 2 */
#define IVAR3			__SYSREG(0xc000000c, u16)	/* interrupt vector 3 */
#define IVAR4			__SYSREG(0xc0000010, u16)	/* interrupt vector 4 */
#define IVAR5			__SYSREG(0xc0000014, u16)	/* interrupt vector 5 */
#define IVAR6			__SYSREG(0xc0000018, u16)	/* interrupt vector 6 */

#define TBR			__SYSREG(0xc0000024, u32)	/* Trap table base */
#define TBR_TB			0xff000000	/* table base address bits 31-24 */
#define TBR_INT_CODE		0x00ffffff	/* interrupt code */

#define DEAR			__SYSREG(0xc0000038, u32)	/* Data access exception address */

#define sISR			__SYSREG(0xc0000044, u32)	/* Supervisor interrupt status */
#define	sISR_IRQICE		0x00000001	/* ICE interrupt */
#define	sISR_ISTEP		0x00000002	/* single step interrupt */
#define	sISR_MISSA		0x00000004	/* memory access address misalignment fault */
#define	sISR_UNIMP		0x00000008	/* unimplemented instruction execution fault */
#define	sISR_PIEXE		0x00000010	/* program interrupt */
#define	sISR_MEMERR		0x00000020	/* illegal memory access fault */
#define	sISR_IBREAK		0x00000040	/* instraction break interrupt */
#define	sISR_DBSRL		0x00000080	/* debug serial interrupt */
#define	sISR_PERIDB		0x00000100	/* peripheral debug interrupt */
#define	sISR_EXUNIMP		0x00000200	/* unimplemented ex-instruction execution fault */
#define	sISR_OBREAK		0x00000400	/* operand break interrupt */
#define	sISR_PRIV		0x00000800	/* privileged instruction execution fault */
#define	sISR_BUSERR		0x00001000	/* bus error fault */
#define	sISR_DBLFT		0x00002000	/* double fault */
#define	sISR_DBG		0x00008000	/* debug reserved interrupt */
#define sISR_ITMISS		0x00010000	/* instruction TLB miss */
#define sISR_DTMISS		0x00020000	/* data TLB miss */
#define sISR_ITEX		0x00040000	/* instruction TLB access exception */
#define sISR_DTEX		0x00080000	/* data TLB access exception */
#define sISR_ILGIA		0x00100000	/* illegal instruction access exception */
#define sISR_ILGDA		0x00200000	/* illegal data access exception */
#define sISR_IOIA		0x00400000	/* internal I/O space instruction access excep */
#define sISR_PRIVA		0x00800000	/* privileged space instruction access excep */
#define sISR_PRIDA		0x01000000	/* privileged space data access excep */
#define sISR_DISA		0x02000000	/* data space instruction access excep */
#define sISR_SYSC		0x04000000	/* system call instruction excep */
#define sISR_FPUD		0x08000000	/* FPU disabled excep */
#define sISR_FPUUI		0x10000000	/* FPU unimplemented instruction excep */
#define sISR_FPUOP		0x20000000	/* FPU operation excep */
#define sISR_NE			0x80000000	/* multiple synchronous exceptions excep */

/* cache control registers */
#define CHCTR			__SYSREG(0xc0000070, u16)	/* cache control */
#define CHCTR_ICEN		0x0001		/* instruction cache enable */
#define CHCTR_DCEN		0x0002		/* data cache enable */
#define CHCTR_ICBUSY		0x0004		/* instruction cache busy */
#define CHCTR_DCBUSY		0x0008		/* data cache busy */
#define CHCTR_ICINV		0x0010		/* instruction cache invalidate */
#define CHCTR_DCINV		0x0020		/* data cache invalidate */
#define CHCTR_DCWTMD		0x0040		/* data cache writing mode */
#define CHCTR_DCWTMD_WRBACK	0x0000		/* - write back mode */
#define CHCTR_DCWTMD_WRTHROUGH	0x0040		/* - write through mode */
#define CHCTR_DCALMD		0x0080		/* data cache allocation mode */
#define CHCTR_ICWMD		0x0f00		/* instruction cache way mode */
#define CHCTR_DCWMD		0xf000		/* data cache way mode */

#ifdef CONFIG_AM34_2
#define ICIVCR			__SYSREG(0xc0000c00, u32)	/* icache area invalidate control */
#define ICIVCR_ICIVBSY		0x00000008			/* icache area invalidate busy */
#define ICIVCR_ICI		0x00000001			/* icache area invalidate */

#define ICIVMR			__SYSREG(0xc0000c04, u32)	/* icache area invalidate mask */

#define	DCPGCR			__SYSREG(0xc0000c10, u32)	/* data cache area purge control */
#define	DCPGCR_DCPGBSY		0x00000008			/* data cache area purge busy */
#define	DCPGCR_DCP		0x00000002			/* data cache area purge */
#define	DCPGCR_DCI		0x00000001			/* data cache area invalidate */

#define	DCPGMR			__SYSREG(0xc0000c14, u32)	/* data cache area purge mask */
#endif /* CONFIG_AM34_2 */

/* MMU control registers */
#define MMUCTR			__SYSREG(0xc0000090, u32)	/* MMU control register */
#define MMUCTR_IRP		0x0000003f	/* instruction TLB replace pointer */
#define MMUCTR_ITE		0x00000040	/* instruction TLB enable */
#define MMUCTR_IIV		0x00000080	/* instruction TLB invalidate */
#define MMUCTR_ITL		0x00000700	/* instruction TLB lock pointer */
#define MMUCTR_ITL_NOLOCK	0x00000000	/* - no lock */
#define MMUCTR_ITL_LOCK0	0x00000100	/* - entry 0 locked */
#define MMUCTR_ITL_LOCK0_1	0x00000200	/* - entry 0-1 locked */
#define MMUCTR_ITL_LOCK0_3	0x00000300	/* - entry 0-3 locked */
#define MMUCTR_ITL_LOCK0_7	0x00000400	/* - entry 0-7 locked */
#define MMUCTR_ITL_LOCK0_15	0x00000500	/* - entry 0-15 locked */
#define MMUCTR_CE		0x00008000	/* cacheable bit enable */
#define MMUCTR_DRP		0x003f0000	/* data TLB replace pointer */
#define MMUCTR_DTE		0x00400000	/* data TLB enable */
#define MMUCTR_DIV		0x00800000	/* data TLB invalidate */
#define MMUCTR_DTL		0x07000000	/* data TLB lock pointer */
#define MMUCTR_DTL_NOLOCK	0x00000000	/* - no lock */
#define MMUCTR_DTL_LOCK0	0x01000000	/* - entry 0 locked */
#define MMUCTR_DTL_LOCK0_1	0x02000000	/* - entry 0-1 locked */
#define MMUCTR_DTL_LOCK0_3	0x03000000	/* - entry 0-3 locked */
#define MMUCTR_DTL_LOCK0_7	0x04000000	/* - entry 0-7 locked */
#define MMUCTR_DTL_LOCK0_15	0x05000000	/* - entry 0-15 locked */
#ifdef CONFIG_AM34_2
#define MMUCTR_WTE		0x80000000	/* write-through cache TLB entry bit enable */
#endif

#define PIDR			__SYSREG(0xc0000094, u16)	/* PID register */
#define PIDR_PID		0x00ff		/* process identifier */

#define PTBR			__SYSREG(0xc0000098, unsigned long) /* Page table base register */

#define IPTEL			__SYSREG(0xc00000a0, u32)	/* instruction TLB entry */
#define DPTEL			__SYSREG(0xc00000b0, u32)	/* data TLB entry */
#define xPTEL_V			0x00000001	/* TLB entry valid */
#define xPTEL_UNUSED1		0x00000002	/* unused bit */
#define xPTEL_UNUSED2		0x00000004	/* unused bit */
#define xPTEL_C			0x00000008	/* cached if set */
#define xPTEL_PV		0x00000010	/* page valid */
#define xPTEL_D			0x00000020	/* dirty */
#define xPTEL_PR		0x000001c0	/* page protection */
#define xPTEL_PR_ROK		0x00000000	/* - R/O kernel */
#define xPTEL_PR_RWK		0x00000100	/* - R/W kernel */
#define xPTEL_PR_ROK_ROU	0x00000080	/* - R/O kernel and R/O user */
#define xPTEL_PR_RWK_ROU	0x00000180	/* - R/W kernel and R/O user */
#define xPTEL_PR_RWK_RWU	0x000001c0	/* - R/W kernel and R/W user */
#define xPTEL_G			0x00000200	/* global (use PID if 0) */
#define xPTEL_PS		0x00000c00	/* page size */
#define xPTEL_PS_4Kb		0x00000000	/* - 4Kb page */
#define xPTEL_PS_128Kb		0x00000400	/* - 128Kb page */
#define xPTEL_PS_1Kb		0x00000800	/* - 1Kb page */
#define xPTEL_PS_4Mb		0x00000c00	/* - 4Mb page */
#define xPTEL_PPN		0xfffff006	/* physical page number */

#define IPTEU			__SYSREG(0xc00000a4, u32)	/* instruction TLB virtual addr */
#define DPTEU			__SYSREG(0xc00000b4, u32)	/* data TLB virtual addr */
#define xPTEU_VPN		0xfffffc00	/* virtual page number */
#define xPTEU_PID		0x000000ff	/* process identifier to which applicable */

#define IPTEL2			__SYSREG(0xc00000a8, u32)	/* instruction TLB entry */
#define DPTEL2			__SYSREG(0xc00000b8, u32)	/* data TLB entry */
#define xPTEL2_V		0x00000001	/* TLB entry valid */
#define xPTEL2_C		0x00000002	/* cacheable */
#define xPTEL2_PV		0x00000004	/* page valid */
#define xPTEL2_D		0x00000008	/* dirty */
#define xPTEL2_PR		0x00000070	/* page protection */
#define xPTEL2_PR_ROK		0x00000000	/* - R/O kernel */
#define xPTEL2_PR_RWK		0x00000040	/* - R/W kernel */
#define xPTEL2_PR_ROK_ROU	0x00000020	/* - R/O kernel and R/O user */
#define xPTEL2_PR_RWK_ROU	0x00000060	/* - R/W kernel and R/O user */
#define xPTEL2_PR_RWK_RWU	0x00000070	/* - R/W kernel and R/W user */
#define xPTEL2_G		0x00000080	/* global (use PID if 0) */
#define xPTEL2_PS		0x00000300	/* page size */
#define xPTEL2_PS_4Kb		0x00000000	/* - 4Kb page */
#define xPTEL2_PS_128Kb		0x00000100	/* - 128Kb page */
#define xPTEL2_PS_1Kb		0x00000200	/* - 1Kb page */
#define xPTEL2_PS_4Mb		0x00000300	/* - 4Mb page */
#define xPTEL2_CWT		0x00000400	/* cacheable write-through */
#define xPTEL2_UNUSED1		0x00000800	/* unused bit (broadcast mask) */
#define xPTEL2_PPN		0xfffff000	/* physical page number */

#define xPTEL2_V_BIT		0	/* bit numbers corresponding to above masks */
#define xPTEL2_C_BIT		1
#define xPTEL2_PV_BIT		2
#define xPTEL2_D_BIT		3
#define xPTEL2_G_BIT		7
#define xPTEL2_UNUSED1_BIT	11

#define MMUFCR			__SYSREGC(0xc000009c, u32)	/* MMU exception cause */
#define MMUFCR_IFC		__SYSREGC(0xc000009c, u16)	/* MMU instruction excep cause */
#define MMUFCR_DFC		__SYSREGC(0xc000009e, u16)	/* MMU data exception cause */
#define MMUFCR_xFC_TLBMISS	0x0001		/* TLB miss flag */
#define MMUFCR_xFC_INITWR	0x0002		/* initial write excep flag */
#define MMUFCR_xFC_PGINVAL	0x0004		/* page invalid excep flag */
#define MMUFCR_xFC_PROTVIOL	0x0008		/* protection violation excep flag */
#define MMUFCR_xFC_ACCESS	0x0010		/* access level flag */
#define MMUFCR_xFC_ACCESS_USR	0x0000		/* - user mode */
#define MMUFCR_xFC_ACCESS_SR	0x0010		/* - supervisor mode */
#define MMUFCR_xFC_TYPE		0x0020		/* access type flag */
#define MMUFCR_xFC_TYPE_READ	0x0000		/* - read */
#define MMUFCR_xFC_TYPE_WRITE	0x0020		/* - write */
#define MMUFCR_xFC_PR		0x01c0		/* page protection flag */
#define MMUFCR_xFC_PR_ROK	0x0000		/* - R/O kernel */
#define MMUFCR_xFC_PR_RWK	0x0100		/* - R/W kernel */
#define MMUFCR_xFC_PR_ROK_ROU	0x0080		/* - R/O kernel and R/O user */
#define MMUFCR_xFC_PR_RWK_ROU	0x0180		/* - R/W kernel and R/O user */
#define MMUFCR_xFC_PR_RWK_RWU	0x01c0		/* - R/W kernel and R/W user */
#define MMUFCR_xFC_ILLADDR	0x0200		/* illegal address excep flag */

#ifdef CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT
/* atomic operation registers */
#define AAR		__SYSREG(0xc0000a00, u32)	/* cacheable address */
#define AAR2		__SYSREG(0xc0000a04, u32)	/* uncacheable address */
#define ADR		__SYSREG(0xc0000a08, u32)	/* data */
#define ASR		__SYSREG(0xc0000a0c, u32)	/* status */
#define AARU		__SYSREG(0xd400aa00, u32)	/* user address */
#define ADRU		__SYSREG(0xd400aa08, u32)	/* user data */
#define ASRU		__SYSREG(0xd400aa0c, u32)	/* user status */

#define ASR_RW		0x00000008	/* read */
#define ASR_BW		0x00000004	/* bus error */
#define ASR_IW		0x00000002	/* interrupt */
#define ASR_LW		0x00000001	/* bus lock */

#define ASRU_RW		ASR_RW		/* read */
#define ASRU_BW		ASR_BW		/* bus error */
#define ASRU_IW		ASR_IW		/* interrupt */
#define ASRU_LW		ASR_LW		/* bus lock */

/* in inline ASM, we stick the base pointer in to a reg and use offsets from
 * it */
#define ATOMIC_OPS_BASE_ADDR 0xc0000a00
#ifndef __ASSEMBLY__
asm(
	"_AAR	= 0\n"
	"_AAR2	= 4\n"
	"_ADR	= 8\n"
	"_ASR	= 12\n");
#else
#define _AAR		0
#define _AAR2		4
#define _ADR		8
#define _ASR		12
#endif

/* physical page address for userspace atomic operations registers */
#define USER_ATOMIC_OPS_PAGE_ADDR  0xd400a000

#endif /* CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT */

#endif /* __KERNEL__ */

#endif /* _ASM_CPU_REGS_H */

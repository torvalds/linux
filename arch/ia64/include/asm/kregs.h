#ifndef _ASM_IA64_KREGS_H
#define _ASM_IA64_KREGS_H

/*
 * Copyright (C) 2001-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
/*
 * This file defines the kernel register usage convention used by Linux/ia64.
 */

/*
 * Kernel registers:
 */
#define IA64_KR_IO_BASE		0	/* ar.k0: legacy I/O base address */
#define IA64_KR_TSSD		1	/* ar.k1: IVE uses this as the TSSD */
#define IA64_KR_PER_CPU_DATA	3	/* ar.k3: physical per-CPU base */
#define IA64_KR_CURRENT_STACK	4	/* ar.k4: what's mapped in IA64_TR_CURRENT_STACK */
#define IA64_KR_FPU_OWNER	5	/* ar.k5: fpu-owner (UP only, at the moment) */
#define IA64_KR_CURRENT		6	/* ar.k6: "current" task pointer */
#define IA64_KR_PT_BASE		7	/* ar.k7: page table base address (physical) */

#define _IA64_KR_PASTE(x,y)	x##y
#define _IA64_KR_PREFIX(n)	_IA64_KR_PASTE(ar.k, n)
#define IA64_KR(n)		_IA64_KR_PREFIX(IA64_KR_##n)

/*
 * Translation registers:
 */
#define IA64_TR_KERNEL		0	/* itr0, dtr0: maps kernel image (code & data) */
#define IA64_TR_PALCODE		1	/* itr1: maps PALcode as required by EFI */
#define IA64_TR_CURRENT_STACK	1	/* dtr1: maps kernel's memory- & register-stacks */

#define IA64_TR_ALLOC_BASE	2 	/* itr&dtr: Base of dynamic TR resource*/
#define IA64_TR_ALLOC_MAX	64 	/* Max number for dynamic use*/

/* Processor status register bits: */
#define IA64_PSR_BE_BIT		1
#define IA64_PSR_UP_BIT		2
#define IA64_PSR_AC_BIT		3
#define IA64_PSR_MFL_BIT	4
#define IA64_PSR_MFH_BIT	5
#define IA64_PSR_IC_BIT		13
#define IA64_PSR_I_BIT		14
#define IA64_PSR_PK_BIT		15
#define IA64_PSR_DT_BIT		17
#define IA64_PSR_DFL_BIT	18
#define IA64_PSR_DFH_BIT	19
#define IA64_PSR_SP_BIT		20
#define IA64_PSR_PP_BIT		21
#define IA64_PSR_DI_BIT		22
#define IA64_PSR_SI_BIT		23
#define IA64_PSR_DB_BIT		24
#define IA64_PSR_LP_BIT		25
#define IA64_PSR_TB_BIT		26
#define IA64_PSR_RT_BIT		27
/* The following are not affected by save_flags()/restore_flags(): */
#define IA64_PSR_CPL0_BIT	32
#define IA64_PSR_CPL1_BIT	33
#define IA64_PSR_IS_BIT		34
#define IA64_PSR_MC_BIT		35
#define IA64_PSR_IT_BIT		36
#define IA64_PSR_ID_BIT		37
#define IA64_PSR_DA_BIT		38
#define IA64_PSR_DD_BIT		39
#define IA64_PSR_SS_BIT		40
#define IA64_PSR_RI_BIT		41
#define IA64_PSR_ED_BIT		43
#define IA64_PSR_BN_BIT		44
#define IA64_PSR_IA_BIT		45

/* A mask of PSR bits that we generally don't want to inherit across a clone2() or an
   execve().  Only list flags here that need to be cleared/set for BOTH clone2() and
   execve().  */
#define IA64_PSR_BITS_TO_CLEAR	(IA64_PSR_MFL | IA64_PSR_MFH | IA64_PSR_DB | IA64_PSR_LP | \
				 IA64_PSR_TB  | IA64_PSR_ID  | IA64_PSR_DA | IA64_PSR_DD | \
				 IA64_PSR_SS  | IA64_PSR_ED  | IA64_PSR_IA)
#define IA64_PSR_BITS_TO_SET	(IA64_PSR_DFH | IA64_PSR_SP)

#define IA64_PSR_BE	(__IA64_UL(1) << IA64_PSR_BE_BIT)
#define IA64_PSR_UP	(__IA64_UL(1) << IA64_PSR_UP_BIT)
#define IA64_PSR_AC	(__IA64_UL(1) << IA64_PSR_AC_BIT)
#define IA64_PSR_MFL	(__IA64_UL(1) << IA64_PSR_MFL_BIT)
#define IA64_PSR_MFH	(__IA64_UL(1) << IA64_PSR_MFH_BIT)
#define IA64_PSR_IC	(__IA64_UL(1) << IA64_PSR_IC_BIT)
#define IA64_PSR_I	(__IA64_UL(1) << IA64_PSR_I_BIT)
#define IA64_PSR_PK	(__IA64_UL(1) << IA64_PSR_PK_BIT)
#define IA64_PSR_DT	(__IA64_UL(1) << IA64_PSR_DT_BIT)
#define IA64_PSR_DFL	(__IA64_UL(1) << IA64_PSR_DFL_BIT)
#define IA64_PSR_DFH	(__IA64_UL(1) << IA64_PSR_DFH_BIT)
#define IA64_PSR_SP	(__IA64_UL(1) << IA64_PSR_SP_BIT)
#define IA64_PSR_PP	(__IA64_UL(1) << IA64_PSR_PP_BIT)
#define IA64_PSR_DI	(__IA64_UL(1) << IA64_PSR_DI_BIT)
#define IA64_PSR_SI	(__IA64_UL(1) << IA64_PSR_SI_BIT)
#define IA64_PSR_DB	(__IA64_UL(1) << IA64_PSR_DB_BIT)
#define IA64_PSR_LP	(__IA64_UL(1) << IA64_PSR_LP_BIT)
#define IA64_PSR_TB	(__IA64_UL(1) << IA64_PSR_TB_BIT)
#define IA64_PSR_RT	(__IA64_UL(1) << IA64_PSR_RT_BIT)
/* The following are not affected by save_flags()/restore_flags(): */
#define IA64_PSR_CPL	(__IA64_UL(3) << IA64_PSR_CPL0_BIT)
#define IA64_PSR_IS	(__IA64_UL(1) << IA64_PSR_IS_BIT)
#define IA64_PSR_MC	(__IA64_UL(1) << IA64_PSR_MC_BIT)
#define IA64_PSR_IT	(__IA64_UL(1) << IA64_PSR_IT_BIT)
#define IA64_PSR_ID	(__IA64_UL(1) << IA64_PSR_ID_BIT)
#define IA64_PSR_DA	(__IA64_UL(1) << IA64_PSR_DA_BIT)
#define IA64_PSR_DD	(__IA64_UL(1) << IA64_PSR_DD_BIT)
#define IA64_PSR_SS	(__IA64_UL(1) << IA64_PSR_SS_BIT)
#define IA64_PSR_RI	(__IA64_UL(3) << IA64_PSR_RI_BIT)
#define IA64_PSR_ED	(__IA64_UL(1) << IA64_PSR_ED_BIT)
#define IA64_PSR_BN	(__IA64_UL(1) << IA64_PSR_BN_BIT)
#define IA64_PSR_IA	(__IA64_UL(1) << IA64_PSR_IA_BIT)

/* User mask bits: */
#define IA64_PSR_UM	(IA64_PSR_BE | IA64_PSR_UP | IA64_PSR_AC | IA64_PSR_MFL | IA64_PSR_MFH)

/* Default Control Register */
#define IA64_DCR_PP_BIT		 0	/* privileged performance monitor default */
#define IA64_DCR_BE_BIT		 1	/* big-endian default */
#define IA64_DCR_LC_BIT		 2	/* ia32 lock-check enable */
#define IA64_DCR_DM_BIT		 8	/* defer TLB miss faults */
#define IA64_DCR_DP_BIT		 9	/* defer page-not-present faults */
#define IA64_DCR_DK_BIT		10	/* defer key miss faults */
#define IA64_DCR_DX_BIT		11	/* defer key permission faults */
#define IA64_DCR_DR_BIT		12	/* defer access right faults */
#define IA64_DCR_DA_BIT		13	/* defer access bit faults */
#define IA64_DCR_DD_BIT		14	/* defer debug faults */

#define IA64_DCR_PP	(__IA64_UL(1) << IA64_DCR_PP_BIT)
#define IA64_DCR_BE	(__IA64_UL(1) << IA64_DCR_BE_BIT)
#define IA64_DCR_LC	(__IA64_UL(1) << IA64_DCR_LC_BIT)
#define IA64_DCR_DM	(__IA64_UL(1) << IA64_DCR_DM_BIT)
#define IA64_DCR_DP	(__IA64_UL(1) << IA64_DCR_DP_BIT)
#define IA64_DCR_DK	(__IA64_UL(1) << IA64_DCR_DK_BIT)
#define IA64_DCR_DX	(__IA64_UL(1) << IA64_DCR_DX_BIT)
#define IA64_DCR_DR	(__IA64_UL(1) << IA64_DCR_DR_BIT)
#define IA64_DCR_DA	(__IA64_UL(1) << IA64_DCR_DA_BIT)
#define IA64_DCR_DD	(__IA64_UL(1) << IA64_DCR_DD_BIT)

/* Interrupt Status Register */
#define IA64_ISR_X_BIT		32	/* execute access */
#define IA64_ISR_W_BIT		33	/* write access */
#define IA64_ISR_R_BIT		34	/* read access */
#define IA64_ISR_NA_BIT		35	/* non-access */
#define IA64_ISR_SP_BIT		36	/* speculative load exception */
#define IA64_ISR_RS_BIT		37	/* mandatory register-stack exception */
#define IA64_ISR_IR_BIT		38	/* invalid register frame exception */
#define IA64_ISR_CODE_MASK	0xf

#define IA64_ISR_X	(__IA64_UL(1) << IA64_ISR_X_BIT)
#define IA64_ISR_W	(__IA64_UL(1) << IA64_ISR_W_BIT)
#define IA64_ISR_R	(__IA64_UL(1) << IA64_ISR_R_BIT)
#define IA64_ISR_NA	(__IA64_UL(1) << IA64_ISR_NA_BIT)
#define IA64_ISR_SP	(__IA64_UL(1) << IA64_ISR_SP_BIT)
#define IA64_ISR_RS	(__IA64_UL(1) << IA64_ISR_RS_BIT)
#define IA64_ISR_IR	(__IA64_UL(1) << IA64_ISR_IR_BIT)

/* ISR code field for non-access instructions */
#define IA64_ISR_CODE_TPA	0
#define IA64_ISR_CODE_FC	1
#define IA64_ISR_CODE_PROBE	2
#define IA64_ISR_CODE_TAK	3
#define IA64_ISR_CODE_LFETCH	4
#define IA64_ISR_CODE_PROBEF	5

#endif /* _ASM_IA64_kREGS_H */

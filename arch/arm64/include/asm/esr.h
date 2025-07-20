/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ASM_ESR_H
#define __ASM_ESR_H

#include <asm/memory.h>
#include <asm/sysreg.h>

#define ESR_ELx_EC_UNKNOWN	UL(0x00)
#define ESR_ELx_EC_WFx		UL(0x01)
/* Unallocated EC: 0x02 */
#define ESR_ELx_EC_CP15_32	UL(0x03)
#define ESR_ELx_EC_CP15_64	UL(0x04)
#define ESR_ELx_EC_CP14_MR	UL(0x05)
#define ESR_ELx_EC_CP14_LS	UL(0x06)
#define ESR_ELx_EC_FP_ASIMD	UL(0x07)
#define ESR_ELx_EC_CP10_ID	UL(0x08)	/* EL2 only */
#define ESR_ELx_EC_PAC		UL(0x09)	/* EL2 and above */
#define ESR_ELx_EC_OTHER	UL(0x0A)
/* Unallocated EC: 0x0B */
#define ESR_ELx_EC_CP14_64	UL(0x0C)
#define ESR_ELx_EC_BTI		UL(0x0D)
#define ESR_ELx_EC_ILL		UL(0x0E)
/* Unallocated EC: 0x0F - 0x10 */
#define ESR_ELx_EC_SVC32	UL(0x11)
#define ESR_ELx_EC_HVC32	UL(0x12)	/* EL2 only */
#define ESR_ELx_EC_SMC32	UL(0x13)	/* EL2 and above */
/* Unallocated EC: 0x14 */
#define ESR_ELx_EC_SVC64	UL(0x15)
#define ESR_ELx_EC_HVC64	UL(0x16)	/* EL2 and above */
#define ESR_ELx_EC_SMC64	UL(0x17)	/* EL2 and above */
#define ESR_ELx_EC_SYS64	UL(0x18)
#define ESR_ELx_EC_SVE		UL(0x19)
#define ESR_ELx_EC_ERET		UL(0x1a)	/* EL2 only */
/* Unallocated EC: 0x1B */
#define ESR_ELx_EC_FPAC		UL(0x1C)	/* EL1 and above */
#define ESR_ELx_EC_SME		UL(0x1D)
/* Unallocated EC: 0x1E */
#define ESR_ELx_EC_IMP_DEF	UL(0x1f)	/* EL3 only */
#define ESR_ELx_EC_IABT_LOW	UL(0x20)
#define ESR_ELx_EC_IABT_CUR	UL(0x21)
#define ESR_ELx_EC_PC_ALIGN	UL(0x22)
/* Unallocated EC: 0x23 */
#define ESR_ELx_EC_DABT_LOW	UL(0x24)
#define ESR_ELx_EC_DABT_CUR	UL(0x25)
#define ESR_ELx_EC_SP_ALIGN	UL(0x26)
#define ESR_ELx_EC_MOPS		UL(0x27)
#define ESR_ELx_EC_FP_EXC32	UL(0x28)
/* Unallocated EC: 0x29 - 0x2B */
#define ESR_ELx_EC_FP_EXC64	UL(0x2C)
#define ESR_ELx_EC_GCS		UL(0x2D)
/* Unallocated EC:  0x2E */
#define ESR_ELx_EC_SERROR	UL(0x2F)
#define ESR_ELx_EC_BREAKPT_LOW	UL(0x30)
#define ESR_ELx_EC_BREAKPT_CUR	UL(0x31)
#define ESR_ELx_EC_SOFTSTP_LOW	UL(0x32)
#define ESR_ELx_EC_SOFTSTP_CUR	UL(0x33)
#define ESR_ELx_EC_WATCHPT_LOW	UL(0x34)
#define ESR_ELx_EC_WATCHPT_CUR	UL(0x35)
/* Unallocated EC: 0x36 - 0x37 */
#define ESR_ELx_EC_BKPT32	UL(0x38)
/* Unallocated EC: 0x39 */
#define ESR_ELx_EC_VECTOR32	UL(0x3A)	/* EL2 only */
/* Unallocated EC: 0x3B */
#define ESR_ELx_EC_BRK64	UL(0x3C)
/* Unallocated EC: 0x3D - 0x3F */
#define ESR_ELx_EC_MAX		UL(0x3F)

#define ESR_ELx_EC_SHIFT	(26)
#define ESR_ELx_EC_WIDTH	(6)
#define ESR_ELx_EC_MASK		(UL(0x3F) << ESR_ELx_EC_SHIFT)
#define ESR_ELx_EC(esr)		(((esr) & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT)

#define ESR_ELx_IL_SHIFT	(25)
#define ESR_ELx_IL		(UL(1) << ESR_ELx_IL_SHIFT)
#define ESR_ELx_ISS_MASK	(GENMASK(24, 0))
#define ESR_ELx_ISS(esr)	((esr) & ESR_ELx_ISS_MASK)
#define ESR_ELx_ISS2_SHIFT	(32)
#define ESR_ELx_ISS2_MASK	(GENMASK_ULL(55, 32))
#define ESR_ELx_ISS2(esr)	(((esr) & ESR_ELx_ISS2_MASK) >> ESR_ELx_ISS2_SHIFT)

/* ISS field definitions shared by different classes */
#define ESR_ELx_WNR_SHIFT	(6)
#define ESR_ELx_WNR		(UL(1) << ESR_ELx_WNR_SHIFT)

/* Asynchronous Error Type */
#define ESR_ELx_IDS_SHIFT	(24)
#define ESR_ELx_IDS		(UL(1) << ESR_ELx_IDS_SHIFT)
#define ESR_ELx_AET_SHIFT	(10)
#define ESR_ELx_AET		(UL(0x7) << ESR_ELx_AET_SHIFT)

#define ESR_ELx_AET_UC		(UL(0) << ESR_ELx_AET_SHIFT)
#define ESR_ELx_AET_UEU		(UL(1) << ESR_ELx_AET_SHIFT)
#define ESR_ELx_AET_UEO		(UL(2) << ESR_ELx_AET_SHIFT)
#define ESR_ELx_AET_UER		(UL(3) << ESR_ELx_AET_SHIFT)
#define ESR_ELx_AET_CE		(UL(6) << ESR_ELx_AET_SHIFT)

/* Shared ISS field definitions for Data/Instruction aborts */
#define ESR_ELx_VNCR_SHIFT	(13)
#define ESR_ELx_VNCR		(UL(1) << ESR_ELx_VNCR_SHIFT)
#define ESR_ELx_SET_SHIFT	(11)
#define ESR_ELx_SET_MASK	(UL(3) << ESR_ELx_SET_SHIFT)
#define ESR_ELx_FnV_SHIFT	(10)
#define ESR_ELx_FnV		(UL(1) << ESR_ELx_FnV_SHIFT)
#define ESR_ELx_EA_SHIFT	(9)
#define ESR_ELx_EA		(UL(1) << ESR_ELx_EA_SHIFT)
#define ESR_ELx_S1PTW_SHIFT	(7)
#define ESR_ELx_S1PTW		(UL(1) << ESR_ELx_S1PTW_SHIFT)

/* Shared ISS fault status code(IFSC/DFSC) for Data/Instruction aborts */
#define ESR_ELx_FSC		(0x3F)
#define ESR_ELx_FSC_TYPE	(0x3C)
#define ESR_ELx_FSC_LEVEL	(0x03)
#define ESR_ELx_FSC_EXTABT	(0x10)
#define ESR_ELx_FSC_MTE		(0x11)
#define ESR_ELx_FSC_SERROR	(0x11)
#define ESR_ELx_FSC_ACCESS	(0x08)
#define ESR_ELx_FSC_FAULT	(0x04)
#define ESR_ELx_FSC_PERM	(0x0C)
#define ESR_ELx_FSC_SEA_TTW(n)	(0x14 + (n))
#define ESR_ELx_FSC_SECC	(0x18)
#define ESR_ELx_FSC_SECC_TTW(n)	(0x1c + (n))
#define ESR_ELx_FSC_ADDRSZ	(0x00)

/*
 * Annoyingly, the negative levels for Address size faults aren't laid out
 * contiguously (or in the desired order)
 */
#define ESR_ELx_FSC_ADDRSZ_nL(n)	((n) == -1 ? 0x25 : 0x2C)
#define ESR_ELx_FSC_ADDRSZ_L(n)		((n) < 0 ? ESR_ELx_FSC_ADDRSZ_nL(n) : \
						   (ESR_ELx_FSC_ADDRSZ + (n)))

/* Status codes for individual page table levels */
#define ESR_ELx_FSC_ACCESS_L(n)	(ESR_ELx_FSC_ACCESS + (n))
#define ESR_ELx_FSC_PERM_L(n)	(ESR_ELx_FSC_PERM + (n))

#define ESR_ELx_FSC_FAULT_nL	(0x2C)
#define ESR_ELx_FSC_FAULT_L(n)	(((n) < 0 ? ESR_ELx_FSC_FAULT_nL : \
					    ESR_ELx_FSC_FAULT) + (n))

/* ISS field definitions for Data Aborts */
#define ESR_ELx_ISV_SHIFT	(24)
#define ESR_ELx_ISV		(UL(1) << ESR_ELx_ISV_SHIFT)
#define ESR_ELx_SAS_SHIFT	(22)
#define ESR_ELx_SAS		(UL(3) << ESR_ELx_SAS_SHIFT)
#define ESR_ELx_SSE_SHIFT	(21)
#define ESR_ELx_SSE		(UL(1) << ESR_ELx_SSE_SHIFT)
#define ESR_ELx_SRT_SHIFT	(16)
#define ESR_ELx_SRT_MASK	(UL(0x1F) << ESR_ELx_SRT_SHIFT)
#define ESR_ELx_SF_SHIFT	(15)
#define ESR_ELx_SF 		(UL(1) << ESR_ELx_SF_SHIFT)
#define ESR_ELx_AR_SHIFT	(14)
#define ESR_ELx_AR 		(UL(1) << ESR_ELx_AR_SHIFT)
#define ESR_ELx_CM_SHIFT	(8)
#define ESR_ELx_CM 		(UL(1) << ESR_ELx_CM_SHIFT)

/* ISS2 field definitions for Data Aborts */
#define ESR_ELx_TnD_SHIFT	(10)
#define ESR_ELx_TnD 		(UL(1) << ESR_ELx_TnD_SHIFT)
#define ESR_ELx_TagAccess_SHIFT	(9)
#define ESR_ELx_TagAccess	(UL(1) << ESR_ELx_TagAccess_SHIFT)
#define ESR_ELx_GCS_SHIFT	(8)
#define ESR_ELx_GCS 		(UL(1) << ESR_ELx_GCS_SHIFT)
#define ESR_ELx_Overlay_SHIFT	(6)
#define ESR_ELx_Overlay		(UL(1) << ESR_ELx_Overlay_SHIFT)
#define ESR_ELx_DirtyBit_SHIFT	(5)
#define ESR_ELx_DirtyBit	(UL(1) << ESR_ELx_DirtyBit_SHIFT)
#define ESR_ELx_Xs_SHIFT	(0)
#define ESR_ELx_Xs_MASK		(GENMASK_ULL(4, 0))

/* ISS field definitions for exceptions taken in to Hyp */
#define ESR_ELx_CV		(UL(1) << 24)
#define ESR_ELx_COND_SHIFT	(20)
#define ESR_ELx_COND_MASK	(UL(0xF) << ESR_ELx_COND_SHIFT)
#define ESR_ELx_WFx_ISS_RN	(UL(0x1F) << 5)
#define ESR_ELx_WFx_ISS_RV	(UL(1) << 2)
#define ESR_ELx_WFx_ISS_TI	(UL(3) << 0)
#define ESR_ELx_WFx_ISS_WFxT	(UL(2) << 0)
#define ESR_ELx_WFx_ISS_WFI	(UL(0) << 0)
#define ESR_ELx_WFx_ISS_WFE	(UL(1) << 0)
#define ESR_ELx_xVC_IMM_MASK	((UL(1) << 16) - 1)

/* ISS definitions for LD64B/ST64B/{T,P}SBCSYNC instructions */
#define ESR_ELx_ISS_OTHER_ST64BV	(0)
#define ESR_ELx_ISS_OTHER_ST64BV0	(1)
#define ESR_ELx_ISS_OTHER_LDST64B	(2)
#define ESR_ELx_ISS_OTHER_TSBCSYNC	(3)
#define ESR_ELx_ISS_OTHER_PSBCSYNC	(4)

#define DISR_EL1_IDS		(UL(1) << 24)
/*
 * DISR_EL1 and ESR_ELx share the bottom 13 bits, but the RES0 bits may mean
 * different things in the future...
 */
#define DISR_EL1_ESR_MASK	(ESR_ELx_AET | ESR_ELx_EA | ESR_ELx_FSC)

/* ESR value templates for specific events */
#define ESR_ELx_WFx_MASK	(ESR_ELx_EC_MASK |			\
				 (ESR_ELx_WFx_ISS_TI & ~ESR_ELx_WFx_ISS_WFxT))
#define ESR_ELx_WFx_WFI_VAL	((ESR_ELx_EC_WFx << ESR_ELx_EC_SHIFT) |	\
				 ESR_ELx_WFx_ISS_WFI)

/* BRK instruction trap from AArch64 state */
#define ESR_ELx_BRK64_ISS_COMMENT_MASK	0xffff

/* ISS field definitions for System instruction traps */
#define ESR_ELx_SYS64_ISS_RES0_SHIFT	22
#define ESR_ELx_SYS64_ISS_RES0_MASK	(UL(0x7) << ESR_ELx_SYS64_ISS_RES0_SHIFT)
#define ESR_ELx_SYS64_ISS_DIR_MASK	0x1
#define ESR_ELx_SYS64_ISS_DIR_READ	0x1
#define ESR_ELx_SYS64_ISS_DIR_WRITE	0x0

#define ESR_ELx_SYS64_ISS_RT_SHIFT	5
#define ESR_ELx_SYS64_ISS_RT_MASK	(UL(0x1f) << ESR_ELx_SYS64_ISS_RT_SHIFT)
#define ESR_ELx_SYS64_ISS_CRM_SHIFT	1
#define ESR_ELx_SYS64_ISS_CRM_MASK	(UL(0xf) << ESR_ELx_SYS64_ISS_CRM_SHIFT)
#define ESR_ELx_SYS64_ISS_CRN_SHIFT	10
#define ESR_ELx_SYS64_ISS_CRN_MASK	(UL(0xf) << ESR_ELx_SYS64_ISS_CRN_SHIFT)
#define ESR_ELx_SYS64_ISS_OP1_SHIFT	14
#define ESR_ELx_SYS64_ISS_OP1_MASK	(UL(0x7) << ESR_ELx_SYS64_ISS_OP1_SHIFT)
#define ESR_ELx_SYS64_ISS_OP2_SHIFT	17
#define ESR_ELx_SYS64_ISS_OP2_MASK	(UL(0x7) << ESR_ELx_SYS64_ISS_OP2_SHIFT)
#define ESR_ELx_SYS64_ISS_OP0_SHIFT	20
#define ESR_ELx_SYS64_ISS_OP0_MASK	(UL(0x3) << ESR_ELx_SYS64_ISS_OP0_SHIFT)
#define ESR_ELx_SYS64_ISS_SYS_MASK	(ESR_ELx_SYS64_ISS_OP0_MASK | \
					 ESR_ELx_SYS64_ISS_OP1_MASK | \
					 ESR_ELx_SYS64_ISS_OP2_MASK | \
					 ESR_ELx_SYS64_ISS_CRN_MASK | \
					 ESR_ELx_SYS64_ISS_CRM_MASK)
#define ESR_ELx_SYS64_ISS_SYS_VAL(op0, op1, op2, crn, crm) \
					(((op0) << ESR_ELx_SYS64_ISS_OP0_SHIFT) | \
					 ((op1) << ESR_ELx_SYS64_ISS_OP1_SHIFT) | \
					 ((op2) << ESR_ELx_SYS64_ISS_OP2_SHIFT) | \
					 ((crn) << ESR_ELx_SYS64_ISS_CRN_SHIFT) | \
					 ((crm) << ESR_ELx_SYS64_ISS_CRM_SHIFT))

#define ESR_ELx_SYS64_ISS_SYS_OP_MASK	(ESR_ELx_SYS64_ISS_SYS_MASK | \
					 ESR_ELx_SYS64_ISS_DIR_MASK)
#define ESR_ELx_SYS64_ISS_RT(esr) \
	(((esr) & ESR_ELx_SYS64_ISS_RT_MASK) >> ESR_ELx_SYS64_ISS_RT_SHIFT)
/*
 * User space cache operations have the following sysreg encoding
 * in System instructions.
 * op0=1, op1=3, op2=1, crn=7, crm={ 5, 10, 11, 12, 13, 14 }, WRITE (L=0)
 */
#define ESR_ELx_SYS64_ISS_CRM_DC_CIVAC	14
#define ESR_ELx_SYS64_ISS_CRM_DC_CVADP	13
#define ESR_ELx_SYS64_ISS_CRM_DC_CVAP	12
#define ESR_ELx_SYS64_ISS_CRM_DC_CVAU	11
#define ESR_ELx_SYS64_ISS_CRM_DC_CVAC	10
#define ESR_ELx_SYS64_ISS_CRM_IC_IVAU	5

#define ESR_ELx_SYS64_ISS_EL0_CACHE_OP_MASK	(ESR_ELx_SYS64_ISS_OP0_MASK | \
						 ESR_ELx_SYS64_ISS_OP1_MASK | \
						 ESR_ELx_SYS64_ISS_OP2_MASK | \
						 ESR_ELx_SYS64_ISS_CRN_MASK | \
						 ESR_ELx_SYS64_ISS_DIR_MASK)
#define ESR_ELx_SYS64_ISS_EL0_CACHE_OP_VAL \
				(ESR_ELx_SYS64_ISS_SYS_VAL(1, 3, 1, 7, 0) | \
				 ESR_ELx_SYS64_ISS_DIR_WRITE)
/*
 * User space MRS operations which are supported for emulation
 * have the following sysreg encoding in System instructions.
 * op0 = 3, op1= 0, crn = 0, {crm = 0, 4-7}, READ (L = 1)
 */
#define ESR_ELx_SYS64_ISS_SYS_MRS_OP_MASK	(ESR_ELx_SYS64_ISS_OP0_MASK | \
						 ESR_ELx_SYS64_ISS_OP1_MASK | \
						 ESR_ELx_SYS64_ISS_CRN_MASK | \
						 ESR_ELx_SYS64_ISS_DIR_MASK)
#define ESR_ELx_SYS64_ISS_SYS_MRS_OP_VAL \
				(ESR_ELx_SYS64_ISS_SYS_VAL(3, 0, 0, 0, 0) | \
				 ESR_ELx_SYS64_ISS_DIR_READ)

#define ESR_ELx_SYS64_ISS_SYS_CTR	ESR_ELx_SYS64_ISS_SYS_VAL(3, 3, 1, 0, 0)
#define ESR_ELx_SYS64_ISS_SYS_CTR_READ	(ESR_ELx_SYS64_ISS_SYS_CTR | \
					 ESR_ELx_SYS64_ISS_DIR_READ)

#define ESR_ELx_SYS64_ISS_SYS_CNTVCT	(ESR_ELx_SYS64_ISS_SYS_VAL(3, 3, 2, 14, 0) | \
					 ESR_ELx_SYS64_ISS_DIR_READ)

#define ESR_ELx_SYS64_ISS_SYS_CNTVCTSS	(ESR_ELx_SYS64_ISS_SYS_VAL(3, 3, 6, 14, 0) | \
					 ESR_ELx_SYS64_ISS_DIR_READ)

#define ESR_ELx_SYS64_ISS_SYS_CNTFRQ	(ESR_ELx_SYS64_ISS_SYS_VAL(3, 3, 0, 14, 0) | \
					 ESR_ELx_SYS64_ISS_DIR_READ)

#define esr_sys64_to_sysreg(e)					\
	sys_reg((((e) & ESR_ELx_SYS64_ISS_OP0_MASK) >>		\
		 ESR_ELx_SYS64_ISS_OP0_SHIFT),			\
		(((e) & ESR_ELx_SYS64_ISS_OP1_MASK) >>		\
		 ESR_ELx_SYS64_ISS_OP1_SHIFT),			\
		(((e) & ESR_ELx_SYS64_ISS_CRN_MASK) >>		\
		 ESR_ELx_SYS64_ISS_CRN_SHIFT),			\
		(((e) & ESR_ELx_SYS64_ISS_CRM_MASK) >>		\
		 ESR_ELx_SYS64_ISS_CRM_SHIFT),			\
		(((e) & ESR_ELx_SYS64_ISS_OP2_MASK) >>		\
		 ESR_ELx_SYS64_ISS_OP2_SHIFT))

#define esr_cp15_to_sysreg(e)					\
	sys_reg(3,						\
		(((e) & ESR_ELx_SYS64_ISS_OP1_MASK) >>		\
		 ESR_ELx_SYS64_ISS_OP1_SHIFT),			\
		(((e) & ESR_ELx_SYS64_ISS_CRN_MASK) >>		\
		 ESR_ELx_SYS64_ISS_CRN_SHIFT),			\
		(((e) & ESR_ELx_SYS64_ISS_CRM_MASK) >>		\
		 ESR_ELx_SYS64_ISS_CRM_SHIFT),			\
		(((e) & ESR_ELx_SYS64_ISS_OP2_MASK) >>		\
		 ESR_ELx_SYS64_ISS_OP2_SHIFT))

/* ISS field definitions for ERET/ERETAA/ERETAB trapping */
#define ESR_ELx_ERET_ISS_ERET		0x2
#define ESR_ELx_ERET_ISS_ERETA		0x1

/*
 * ISS field definitions for floating-point exception traps
 * (FP_EXC_32/FP_EXC_64).
 *
 * (The FPEXC_* constants are used instead for common bits.)
 */

#define ESR_ELx_FP_EXC_TFV	(UL(1) << 23)

/*
 * ISS field definitions for CP15 accesses
 */
#define ESR_ELx_CP15_32_ISS_DIR_MASK	0x1
#define ESR_ELx_CP15_32_ISS_DIR_READ	0x1
#define ESR_ELx_CP15_32_ISS_DIR_WRITE	0x0

#define ESR_ELx_CP15_32_ISS_RT_SHIFT	5
#define ESR_ELx_CP15_32_ISS_RT_MASK	(UL(0x1f) << ESR_ELx_CP15_32_ISS_RT_SHIFT)
#define ESR_ELx_CP15_32_ISS_CRM_SHIFT	1
#define ESR_ELx_CP15_32_ISS_CRM_MASK	(UL(0xf) << ESR_ELx_CP15_32_ISS_CRM_SHIFT)
#define ESR_ELx_CP15_32_ISS_CRN_SHIFT	10
#define ESR_ELx_CP15_32_ISS_CRN_MASK	(UL(0xf) << ESR_ELx_CP15_32_ISS_CRN_SHIFT)
#define ESR_ELx_CP15_32_ISS_OP1_SHIFT	14
#define ESR_ELx_CP15_32_ISS_OP1_MASK	(UL(0x7) << ESR_ELx_CP15_32_ISS_OP1_SHIFT)
#define ESR_ELx_CP15_32_ISS_OP2_SHIFT	17
#define ESR_ELx_CP15_32_ISS_OP2_MASK	(UL(0x7) << ESR_ELx_CP15_32_ISS_OP2_SHIFT)

#define ESR_ELx_CP15_32_ISS_SYS_MASK	(ESR_ELx_CP15_32_ISS_OP1_MASK | \
					 ESR_ELx_CP15_32_ISS_OP2_MASK | \
					 ESR_ELx_CP15_32_ISS_CRN_MASK | \
					 ESR_ELx_CP15_32_ISS_CRM_MASK | \
					 ESR_ELx_CP15_32_ISS_DIR_MASK)
#define ESR_ELx_CP15_32_ISS_SYS_VAL(op1, op2, crn, crm) \
					(((op1) << ESR_ELx_CP15_32_ISS_OP1_SHIFT) | \
					 ((op2) << ESR_ELx_CP15_32_ISS_OP2_SHIFT) | \
					 ((crn) << ESR_ELx_CP15_32_ISS_CRN_SHIFT) | \
					 ((crm) << ESR_ELx_CP15_32_ISS_CRM_SHIFT))

#define ESR_ELx_CP15_64_ISS_DIR_MASK	0x1
#define ESR_ELx_CP15_64_ISS_DIR_READ	0x1
#define ESR_ELx_CP15_64_ISS_DIR_WRITE	0x0

#define ESR_ELx_CP15_64_ISS_RT_SHIFT	5
#define ESR_ELx_CP15_64_ISS_RT_MASK	(UL(0x1f) << ESR_ELx_CP15_64_ISS_RT_SHIFT)

#define ESR_ELx_CP15_64_ISS_RT2_SHIFT	10
#define ESR_ELx_CP15_64_ISS_RT2_MASK	(UL(0x1f) << ESR_ELx_CP15_64_ISS_RT2_SHIFT)

#define ESR_ELx_CP15_64_ISS_OP1_SHIFT	16
#define ESR_ELx_CP15_64_ISS_OP1_MASK	(UL(0xf) << ESR_ELx_CP15_64_ISS_OP1_SHIFT)
#define ESR_ELx_CP15_64_ISS_CRM_SHIFT	1
#define ESR_ELx_CP15_64_ISS_CRM_MASK	(UL(0xf) << ESR_ELx_CP15_64_ISS_CRM_SHIFT)

#define ESR_ELx_CP15_64_ISS_SYS_VAL(op1, crm) \
					(((op1) << ESR_ELx_CP15_64_ISS_OP1_SHIFT) | \
					 ((crm) << ESR_ELx_CP15_64_ISS_CRM_SHIFT))

#define ESR_ELx_CP15_64_ISS_SYS_MASK	(ESR_ELx_CP15_64_ISS_OP1_MASK |	\
					 ESR_ELx_CP15_64_ISS_CRM_MASK | \
					 ESR_ELx_CP15_64_ISS_DIR_MASK)

#define ESR_ELx_CP15_64_ISS_SYS_CNTVCT	(ESR_ELx_CP15_64_ISS_SYS_VAL(1, 14) | \
					 ESR_ELx_CP15_64_ISS_DIR_READ)

#define ESR_ELx_CP15_64_ISS_SYS_CNTVCTSS (ESR_ELx_CP15_64_ISS_SYS_VAL(9, 14) | \
					 ESR_ELx_CP15_64_ISS_DIR_READ)

#define ESR_ELx_CP15_32_ISS_SYS_CNTFRQ	(ESR_ELx_CP15_32_ISS_SYS_VAL(0, 0, 14, 0) |\
					 ESR_ELx_CP15_32_ISS_DIR_READ)

/*
 * ISS values for SME traps
 */
#define ESR_ELx_SME_ISS_SMTC_MASK		GENMASK(2, 0)
#define ESR_ELx_SME_ISS_SMTC(esr)		((esr) & ESR_ELx_SME_ISS_SMTC_MASK)

#define ESR_ELx_SME_ISS_SMTC_SME_DISABLED	0
#define ESR_ELx_SME_ISS_SMTC_ILL		1
#define ESR_ELx_SME_ISS_SMTC_SM_DISABLED	2
#define ESR_ELx_SME_ISS_SMTC_ZA_DISABLED	3
#define ESR_ELx_SME_ISS_SMTC_ZT_DISABLED	4

/* ISS field definitions for MOPS exceptions */
#define ESR_ELx_MOPS_ISS_MEM_INST	(UL(1) << 24)
#define ESR_ELx_MOPS_ISS_FROM_EPILOGUE	(UL(1) << 18)
#define ESR_ELx_MOPS_ISS_WRONG_OPTION	(UL(1) << 17)
#define ESR_ELx_MOPS_ISS_OPTION_A	(UL(1) << 16)
#define ESR_ELx_MOPS_ISS_DESTREG(esr)	(((esr) & (UL(0x1f) << 10)) >> 10)
#define ESR_ELx_MOPS_ISS_SRCREG(esr)	(((esr) & (UL(0x1f) << 5)) >> 5)
#define ESR_ELx_MOPS_ISS_SIZEREG(esr)	(((esr) & (UL(0x1f) << 0)) >> 0)

/* ISS field definitions for GCS */
#define ESR_ELx_ExType_SHIFT	(20)
#define ESR_ELx_ExType_MASK		GENMASK(23, 20)
#define ESR_ELx_Raddr_SHIFT		(10)
#define ESR_ELx_Raddr_MASK		GENMASK(14, 10)
#define ESR_ELx_Rn_SHIFT		(5)
#define ESR_ELx_Rn_MASK			GENMASK(9, 5)
#define ESR_ELx_Rvalue_SHIFT		5
#define ESR_ELx_Rvalue_MASK		GENMASK(9, 5)
#define ESR_ELx_IT_SHIFT		(0)
#define ESR_ELx_IT_MASK			GENMASK(4, 0)

#define ESR_ELx_ExType_DATA_CHECK	0
#define ESR_ELx_ExType_EXLOCK		1
#define ESR_ELx_ExType_STR		2

#define ESR_ELx_IT_RET			0
#define ESR_ELx_IT_GCSPOPM		1
#define ESR_ELx_IT_RET_KEYA		2
#define ESR_ELx_IT_RET_KEYB		3
#define ESR_ELx_IT_GCSSS1		4
#define ESR_ELx_IT_GCSSS2		5
#define ESR_ELx_IT_GCSPOPCX		6
#define ESR_ELx_IT_GCSPOPX		7

#ifndef __ASSEMBLY__
#include <asm/types.h>

static inline unsigned long esr_brk_comment(unsigned long esr)
{
	return esr & ESR_ELx_BRK64_ISS_COMMENT_MASK;
}

static inline bool esr_is_data_abort(unsigned long esr)
{
	const unsigned long ec = ESR_ELx_EC(esr);

	return ec == ESR_ELx_EC_DABT_LOW || ec == ESR_ELx_EC_DABT_CUR;
}

static inline bool esr_is_cfi_brk(unsigned long esr)
{
	return ESR_ELx_EC(esr) == ESR_ELx_EC_BRK64 &&
	       (esr_brk_comment(esr) & ~CFI_BRK_IMM_MASK) == CFI_BRK_IMM_BASE;
}

static inline bool esr_is_ubsan_brk(unsigned long esr)
{
	return (esr_brk_comment(esr) & ~UBSAN_BRK_MASK) == UBSAN_BRK_IMM;
}

static inline bool esr_fsc_is_translation_fault(unsigned long esr)
{
	esr = esr & ESR_ELx_FSC;

	return (esr == ESR_ELx_FSC_FAULT_L(3)) ||
	       (esr == ESR_ELx_FSC_FAULT_L(2)) ||
	       (esr == ESR_ELx_FSC_FAULT_L(1)) ||
	       (esr == ESR_ELx_FSC_FAULT_L(0)) ||
	       (esr == ESR_ELx_FSC_FAULT_L(-1));
}

static inline bool esr_fsc_is_permission_fault(unsigned long esr)
{
	esr = esr & ESR_ELx_FSC;

	return (esr == ESR_ELx_FSC_PERM_L(3)) ||
	       (esr == ESR_ELx_FSC_PERM_L(2)) ||
	       (esr == ESR_ELx_FSC_PERM_L(1)) ||
	       (esr == ESR_ELx_FSC_PERM_L(0));
}

static inline bool esr_fsc_is_access_flag_fault(unsigned long esr)
{
	esr = esr & ESR_ELx_FSC;

	return (esr == ESR_ELx_FSC_ACCESS_L(3)) ||
	       (esr == ESR_ELx_FSC_ACCESS_L(2)) ||
	       (esr == ESR_ELx_FSC_ACCESS_L(1)) ||
	       (esr == ESR_ELx_FSC_ACCESS_L(0));
}

static inline bool esr_fsc_is_addr_sz_fault(unsigned long esr)
{
	esr &= ESR_ELx_FSC;

	return (esr == ESR_ELx_FSC_ADDRSZ_L(3))	||
	       (esr == ESR_ELx_FSC_ADDRSZ_L(2))	||
	       (esr == ESR_ELx_FSC_ADDRSZ_L(1)) ||
	       (esr == ESR_ELx_FSC_ADDRSZ_L(0))	||
	       (esr == ESR_ELx_FSC_ADDRSZ_L(-1));
}

static inline bool esr_fsc_is_sea_ttw(unsigned long esr)
{
	esr = esr & ESR_ELx_FSC;

	return (esr == ESR_ELx_FSC_SEA_TTW(3)) ||
	       (esr == ESR_ELx_FSC_SEA_TTW(2)) ||
	       (esr == ESR_ELx_FSC_SEA_TTW(1)) ||
	       (esr == ESR_ELx_FSC_SEA_TTW(0)) ||
	       (esr == ESR_ELx_FSC_SEA_TTW(-1));
}

static inline bool esr_fsc_is_secc_ttw(unsigned long esr)
{
	esr = esr & ESR_ELx_FSC;

	return (esr == ESR_ELx_FSC_SECC_TTW(3)) ||
	       (esr == ESR_ELx_FSC_SECC_TTW(2)) ||
	       (esr == ESR_ELx_FSC_SECC_TTW(1)) ||
	       (esr == ESR_ELx_FSC_SECC_TTW(0)) ||
	       (esr == ESR_ELx_FSC_SECC_TTW(-1));
}

/* Indicate whether ESR.EC==0x1A is for an ERETAx instruction */
static inline bool esr_iss_is_eretax(unsigned long esr)
{
	return esr & ESR_ELx_ERET_ISS_ERET;
}

/* Indicate which key is used for ERETAx (false: A-Key, true: B-Key) */
static inline bool esr_iss_is_eretab(unsigned long esr)
{
	return esr & ESR_ELx_ERET_ISS_ERETA;
}

const char *esr_get_class_string(unsigned long esr);
#endif /* __ASSEMBLY */

#endif /* __ASM_ESR_H */

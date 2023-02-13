/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ASM_ESR_H
#define __ASM_ESR_H

#include <asm/memory.h>
#include <asm/sysreg.h>

#define ESR_ELx_EC_UNKNOWN	(0x00)
#define ESR_ELx_EC_WFx		(0x01)
/* Unallocated EC: 0x02 */
#define ESR_ELx_EC_CP15_32	(0x03)
#define ESR_ELx_EC_CP15_64	(0x04)
#define ESR_ELx_EC_CP14_MR	(0x05)
#define ESR_ELx_EC_CP14_LS	(0x06)
#define ESR_ELx_EC_FP_ASIMD	(0x07)
#define ESR_ELx_EC_CP10_ID	(0x08)	/* EL2 only */
#define ESR_ELx_EC_PAC		(0x09)	/* EL2 and above */
/* Unallocated EC: 0x0A - 0x0B */
#define ESR_ELx_EC_CP14_64	(0x0C)
#define ESR_ELx_EC_BTI		(0x0D)
#define ESR_ELx_EC_ILL		(0x0E)
/* Unallocated EC: 0x0F - 0x10 */
#define ESR_ELx_EC_SVC32	(0x11)
#define ESR_ELx_EC_HVC32	(0x12)	/* EL2 only */
#define ESR_ELx_EC_SMC32	(0x13)	/* EL2 and above */
/* Unallocated EC: 0x14 */
#define ESR_ELx_EC_SVC64	(0x15)
#define ESR_ELx_EC_HVC64	(0x16)	/* EL2 and above */
#define ESR_ELx_EC_SMC64	(0x17)	/* EL2 and above */
#define ESR_ELx_EC_SYS64	(0x18)
#define ESR_ELx_EC_SVE		(0x19)
#define ESR_ELx_EC_ERET		(0x1a)	/* EL2 only */
/* Unallocated EC: 0x1B */
#define ESR_ELx_EC_FPAC		(0x1C)	/* EL1 and above */
#define ESR_ELx_EC_SME		(0x1D)
/* Unallocated EC: 0x1E */
#define ESR_ELx_EC_IMP_DEF	(0x1f)	/* EL3 only */
#define ESR_ELx_EC_IABT_LOW	(0x20)
#define ESR_ELx_EC_IABT_CUR	(0x21)
#define ESR_ELx_EC_PC_ALIGN	(0x22)
/* Unallocated EC: 0x23 */
#define ESR_ELx_EC_DABT_LOW	(0x24)
#define ESR_ELx_EC_DABT_CUR	(0x25)
#define ESR_ELx_EC_SP_ALIGN	(0x26)
/* Unallocated EC: 0x27 */
#define ESR_ELx_EC_FP_EXC32	(0x28)
/* Unallocated EC: 0x29 - 0x2B */
#define ESR_ELx_EC_FP_EXC64	(0x2C)
/* Unallocated EC: 0x2D - 0x2E */
#define ESR_ELx_EC_SERROR	(0x2F)
#define ESR_ELx_EC_BREAKPT_LOW	(0x30)
#define ESR_ELx_EC_BREAKPT_CUR	(0x31)
#define ESR_ELx_EC_SOFTSTP_LOW	(0x32)
#define ESR_ELx_EC_SOFTSTP_CUR	(0x33)
#define ESR_ELx_EC_WATCHPT_LOW	(0x34)
#define ESR_ELx_EC_WATCHPT_CUR	(0x35)
/* Unallocated EC: 0x36 - 0x37 */
#define ESR_ELx_EC_BKPT32	(0x38)
/* Unallocated EC: 0x39 */
#define ESR_ELx_EC_VECTOR32	(0x3A)	/* EL2 only */
/* Unallocated EC: 0x3B */
#define ESR_ELx_EC_BRK64	(0x3C)
/* Unallocated EC: 0x3D - 0x3F */
#define ESR_ELx_EC_MAX		(0x3F)

#define ESR_ELx_EC_SHIFT	(26)
#define ESR_ELx_EC_WIDTH	(6)
#define ESR_ELx_EC_MASK		(UL(0x3F) << ESR_ELx_EC_SHIFT)
#define ESR_ELx_EC(esr)		(((esr) & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT)

#define ESR_ELx_IL_SHIFT	(25)
#define ESR_ELx_IL		(UL(1) << ESR_ELx_IL_SHIFT)
#define ESR_ELx_ISS_MASK	(ESR_ELx_IL - 1)
#define ESR_ELx_ISS(esr)	((esr) & ESR_ELx_ISS_MASK)

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
#define ESR_ELx_FSC_SEA_TTW0	(0x14)
#define ESR_ELx_FSC_SEA_TTW1	(0x15)
#define ESR_ELx_FSC_SEA_TTW2	(0x16)
#define ESR_ELx_FSC_SEA_TTW3	(0x17)
#define ESR_ELx_FSC_SECC	(0x18)
#define ESR_ELx_FSC_SECC_TTW0	(0x1c)
#define ESR_ELx_FSC_SECC_TTW1	(0x1d)
#define ESR_ELx_FSC_SECC_TTW2	(0x1e)
#define ESR_ELx_FSC_SECC_TTW3	(0x1f)

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

#define ESR_ELx_SME_ISS_SME_DISABLED	0
#define ESR_ELx_SME_ISS_ILL		1
#define ESR_ELx_SME_ISS_SM_DISABLED	2
#define ESR_ELx_SME_ISS_ZA_DISABLED	3
#define ESR_ELx_SME_ISS_ZT_DISABLED	4

#ifndef __ASSEMBLY__
#include <asm/types.h>

static inline bool esr_is_data_abort(unsigned long esr)
{
	const unsigned long ec = ESR_ELx_EC(esr);

	return ec == ESR_ELx_EC_DABT_LOW || ec == ESR_ELx_EC_DABT_CUR;
}

const char *esr_get_class_string(unsigned long esr);
#endif /* __ASSEMBLY */

#endif /* __ASM_ESR_H */

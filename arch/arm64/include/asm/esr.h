/*
 * Copyright (C) 2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
#define ESR_ELx_EC_CP10_ID	(0x08)
/* Unallocated EC: 0x09 - 0x0B */
#define ESR_ELx_EC_CP14_64	(0x0C)
/* Unallocated EC: 0x0d */
#define ESR_ELx_EC_ILL		(0x0E)
/* Unallocated EC: 0x0F - 0x10 */
#define ESR_ELx_EC_SVC32	(0x11)
#define ESR_ELx_EC_HVC32	(0x12)
#define ESR_ELx_EC_SMC32	(0x13)
/* Unallocated EC: 0x14 */
#define ESR_ELx_EC_SVC64	(0x15)
#define ESR_ELx_EC_HVC64	(0x16)
#define ESR_ELx_EC_SMC64	(0x17)
#define ESR_ELx_EC_SYS64	(0x18)
#define ESR_ELx_EC_SVE		(0x19)
/* Unallocated EC: 0x1A - 0x1E */
#define ESR_ELx_EC_IMP_DEF	(0x1f)
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
#define ESR_ELx_EC_VECTOR32	(0x3A)
/* Unallocted EC: 0x3B */
#define ESR_ELx_EC_BRK64	(0x3C)
/* Unallocated EC: 0x3D - 0x3F */
#define ESR_ELx_EC_MAX		(0x3F)

#define ESR_ELx_EC_SHIFT	(26)
#define ESR_ELx_EC_MASK		(UL(0x3F) << ESR_ELx_EC_SHIFT)
#define ESR_ELx_EC(esr)		(((esr) & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT)

#define ESR_ELx_IL_SHIFT	(25)
#define ESR_ELx_IL		(UL(1) << ESR_ELx_IL_SHIFT)
#define ESR_ELx_ISS_MASK	(ESR_ELx_IL - 1)

/* ISS field definitions shared by different classes */
#define ESR_ELx_WNR_SHIFT	(6)
#define ESR_ELx_WNR		(UL(1) << ESR_ELx_WNR_SHIFT)

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
#define ESR_ELx_FSC_EXTABT	(0x10)
#define ESR_ELx_FSC_ACCESS	(0x08)
#define ESR_ELx_FSC_FAULT	(0x04)
#define ESR_ELx_FSC_PERM	(0x0C)

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
#define ESR_ELx_WFx_ISS_WFE	(UL(1) << 0)
#define ESR_ELx_xVC_IMM_MASK	((1UL << 16) - 1)

/* ESR value templates for specific events */

/* BRK instruction trap from AArch64 state */
#define ESR_ELx_VAL_BRK64(imm)					\
	((ESR_ELx_EC_BRK64 << ESR_ELx_EC_SHIFT) | ESR_ELx_IL |	\
	 ((imm) & 0xffff))

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
/*
 * User space cache operations have the following sysreg encoding
 * in System instructions.
 * op0=1, op1=3, op2=1, crn=7, crm={ 5, 10, 11, 12, 14 }, WRITE (L=0)
 */
#define ESR_ELx_SYS64_ISS_CRM_DC_CIVAC	14
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

#define ESR_ELx_SYS64_ISS_SYS_CTR	ESR_ELx_SYS64_ISS_SYS_VAL(3, 3, 1, 0, 0)
#define ESR_ELx_SYS64_ISS_SYS_CTR_READ	(ESR_ELx_SYS64_ISS_SYS_CTR | \
					 ESR_ELx_SYS64_ISS_DIR_READ)

#define ESR_ELx_SYS64_ISS_SYS_CNTVCT	(ESR_ELx_SYS64_ISS_SYS_VAL(3, 3, 2, 14, 0) | \
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

#ifndef __ASSEMBLY__
#include <asm/types.h>

static inline bool esr_is_data_abort(u32 esr)
{
	const u32 ec = ESR_ELx_EC(esr);

	return ec == ESR_ELx_EC_DABT_LOW || ec == ESR_ELx_EC_DABT_CUR;
}

const char *esr_get_class_string(u32 esr);
#endif /* __ASSEMBLY */

#endif /* __ASM_ESR_H */

/*
 * Copyright (C) 2012,2013 - ARM Ltd
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

#ifndef __ARM64_KVM_ARM_H__
#define __ARM64_KVM_ARM_H__

#include <asm/types.h>

/* Hyp Configuration Register (HCR) bits */
#define HCR_ID		(UL(1) << 33)
#define HCR_CD		(UL(1) << 32)
#define HCR_RW_SHIFT	31
#define HCR_RW		(UL(1) << HCR_RW_SHIFT)
#define HCR_TRVM	(UL(1) << 30)
#define HCR_HCD		(UL(1) << 29)
#define HCR_TDZ		(UL(1) << 28)
#define HCR_TGE		(UL(1) << 27)
#define HCR_TVM		(UL(1) << 26)
#define HCR_TTLB	(UL(1) << 25)
#define HCR_TPU		(UL(1) << 24)
#define HCR_TPC		(UL(1) << 23)
#define HCR_TSW		(UL(1) << 22)
#define HCR_TAC		(UL(1) << 21)
#define HCR_TIDCP	(UL(1) << 20)
#define HCR_TSC		(UL(1) << 19)
#define HCR_TID3	(UL(1) << 18)
#define HCR_TID2	(UL(1) << 17)
#define HCR_TID1	(UL(1) << 16)
#define HCR_TID0	(UL(1) << 15)
#define HCR_TWE		(UL(1) << 14)
#define HCR_TWI		(UL(1) << 13)
#define HCR_DC		(UL(1) << 12)
#define HCR_BSU		(3 << 10)
#define HCR_BSU_IS	(UL(1) << 10)
#define HCR_FB		(UL(1) << 9)
#define HCR_VA		(UL(1) << 8)
#define HCR_VI		(UL(1) << 7)
#define HCR_VF		(UL(1) << 6)
#define HCR_AMO		(UL(1) << 5)
#define HCR_IMO		(UL(1) << 4)
#define HCR_FMO		(UL(1) << 3)
#define HCR_PTW		(UL(1) << 2)
#define HCR_SWIO	(UL(1) << 1)
#define HCR_VM		(UL(1) << 0)

/*
 * The bits we set in HCR:
 * RW:		64bit by default, can be overriden for 32bit VMs
 * TAC:		Trap ACTLR
 * TSC:		Trap SMC
 * TVM:		Trap VM ops (until M+C set in SCTLR_EL1)
 * TSW:		Trap cache operations by set/way
 * TWE:		Trap WFE
 * TWI:		Trap WFI
 * TIDCP:	Trap L2CTLR/L2ECTLR
 * BSU_IS:	Upgrade barriers to the inner shareable domain
 * FB:		Force broadcast of all maintainance operations
 * AMO:		Override CPSR.A and enable signaling with VA
 * IMO:		Override CPSR.I and enable signaling with VI
 * FMO:		Override CPSR.F and enable signaling with VF
 * SWIO:	Turn set/way invalidates into set/way clean+invalidate
 */
#define HCR_GUEST_FLAGS (HCR_TSC | HCR_TSW | HCR_TWE | HCR_TWI | HCR_VM | \
			 HCR_TVM | HCR_BSU_IS | HCR_FB | HCR_TAC | \
			 HCR_AMO | HCR_IMO | HCR_FMO | \
			 HCR_SWIO | HCR_TIDCP | HCR_RW)
#define HCR_VIRT_EXCP_MASK (HCR_VA | HCR_VI | HCR_VF)

/* Hyp System Control Register (SCTLR_EL2) bits */
#define SCTLR_EL2_EE	(1 << 25)
#define SCTLR_EL2_WXN	(1 << 19)
#define SCTLR_EL2_I	(1 << 12)
#define SCTLR_EL2_SA	(1 << 3)
#define SCTLR_EL2_C	(1 << 2)
#define SCTLR_EL2_A	(1 << 1)
#define SCTLR_EL2_M	1
#define SCTLR_EL2_FLAGS	(SCTLR_EL2_M | SCTLR_EL2_A | SCTLR_EL2_C |	\
			 SCTLR_EL2_SA | SCTLR_EL2_I)

/* TCR_EL2 Registers bits */
#define TCR_EL2_TBI	(1 << 20)
#define TCR_EL2_PS	(7 << 16)
#define TCR_EL2_PS_40B	(2 << 16)
#define TCR_EL2_TG0	(1 << 14)
#define TCR_EL2_SH0	(3 << 12)
#define TCR_EL2_ORGN0	(3 << 10)
#define TCR_EL2_IRGN0	(3 << 8)
#define TCR_EL2_T0SZ	0x3f
#define TCR_EL2_MASK	(TCR_EL2_TG0 | TCR_EL2_SH0 | \
			 TCR_EL2_ORGN0 | TCR_EL2_IRGN0 | TCR_EL2_T0SZ)

#define TCR_EL2_FLAGS	(TCR_EL2_PS_40B)

/* VTCR_EL2 Registers bits */
#define VTCR_EL2_PS_MASK	(7 << 16)
#define VTCR_EL2_PS_40B		(2 << 16)
#define VTCR_EL2_TG0_MASK	(1 << 14)
#define VTCR_EL2_TG0_4K		(0 << 14)
#define VTCR_EL2_TG0_64K	(1 << 14)
#define VTCR_EL2_SH0_MASK	(3 << 12)
#define VTCR_EL2_SH0_INNER	(3 << 12)
#define VTCR_EL2_ORGN0_MASK	(3 << 10)
#define VTCR_EL2_ORGN0_WBWA	(1 << 10)
#define VTCR_EL2_IRGN0_MASK	(3 << 8)
#define VTCR_EL2_IRGN0_WBWA	(1 << 8)
#define VTCR_EL2_SL0_MASK	(3 << 6)
#define VTCR_EL2_SL0_LVL1	(1 << 6)
#define VTCR_EL2_T0SZ_MASK	0x3f
#define VTCR_EL2_T0SZ_40B	24

#ifdef CONFIG_ARM64_64K_PAGES
/*
 * Stage2 translation configuration:
 * 40bits output (PS = 2)
 * 40bits input  (T0SZ = 24)
 * 64kB pages (TG0 = 1)
 * 2 level page tables (SL = 1)
 */
#define VTCR_EL2_FLAGS		(VTCR_EL2_PS_40B | VTCR_EL2_TG0_64K | \
				 VTCR_EL2_SH0_INNER | VTCR_EL2_ORGN0_WBWA | \
				 VTCR_EL2_IRGN0_WBWA | VTCR_EL2_SL0_LVL1 | \
				 VTCR_EL2_T0SZ_40B)
#define VTTBR_X		(38 - VTCR_EL2_T0SZ_40B)
#else
/*
 * Stage2 translation configuration:
 * 40bits output (PS = 2)
 * 40bits input  (T0SZ = 24)
 * 4kB pages (TG0 = 0)
 * 3 level page tables (SL = 1)
 */
#define VTCR_EL2_FLAGS		(VTCR_EL2_PS_40B | VTCR_EL2_TG0_4K | \
				 VTCR_EL2_SH0_INNER | VTCR_EL2_ORGN0_WBWA | \
				 VTCR_EL2_IRGN0_WBWA | VTCR_EL2_SL0_LVL1 | \
				 VTCR_EL2_T0SZ_40B)
#define VTTBR_X		(37 - VTCR_EL2_T0SZ_40B)
#endif

#define VTTBR_BADDR_SHIFT (VTTBR_X - 1)
#define VTTBR_BADDR_MASK  (((1LLU << (40 - VTTBR_X)) - 1) << VTTBR_BADDR_SHIFT)
#define VTTBR_VMID_SHIFT  (48LLU)
#define VTTBR_VMID_MASK	  (0xffLLU << VTTBR_VMID_SHIFT)

/* Hyp System Trap Register */
#define HSTR_EL2_TTEE	(1 << 16)
#define HSTR_EL2_T(x)	(1 << x)

/* Hyp Coprocessor Trap Register */
#define CPTR_EL2_TCPAC	(1 << 31)
#define CPTR_EL2_TTA	(1 << 20)
#define CPTR_EL2_TFP	(1 << 10)

/* Hyp Debug Configuration Register bits */
#define MDCR_EL2_TDRA		(1 << 11)
#define MDCR_EL2_TDOSA		(1 << 10)
#define MDCR_EL2_TDA		(1 << 9)
#define MDCR_EL2_TDE		(1 << 8)
#define MDCR_EL2_HPME		(1 << 7)
#define MDCR_EL2_TPM		(1 << 6)
#define MDCR_EL2_TPMCR		(1 << 5)
#define MDCR_EL2_HPMN_MASK	(0x1F)

/* Exception Syndrome Register (ESR) bits */
#define ESR_EL2_EC_SHIFT	(26)
#define ESR_EL2_EC		(0x3fU << ESR_EL2_EC_SHIFT)
#define ESR_EL2_IL		(1U << 25)
#define ESR_EL2_ISS		(ESR_EL2_IL - 1)
#define ESR_EL2_ISV_SHIFT	(24)
#define ESR_EL2_ISV		(1U << ESR_EL2_ISV_SHIFT)
#define ESR_EL2_SAS_SHIFT	(22)
#define ESR_EL2_SAS		(3U << ESR_EL2_SAS_SHIFT)
#define ESR_EL2_SSE		(1 << 21)
#define ESR_EL2_SRT_SHIFT	(16)
#define ESR_EL2_SRT_MASK	(0x1f << ESR_EL2_SRT_SHIFT)
#define ESR_EL2_SF 		(1 << 15)
#define ESR_EL2_AR 		(1 << 14)
#define ESR_EL2_EA 		(1 << 9)
#define ESR_EL2_CM 		(1 << 8)
#define ESR_EL2_S1PTW 		(1 << 7)
#define ESR_EL2_WNR		(1 << 6)
#define ESR_EL2_FSC		(0x3f)
#define ESR_EL2_FSC_TYPE	(0x3c)

#define ESR_EL2_CV_SHIFT	(24)
#define ESR_EL2_CV		(1U << ESR_EL2_CV_SHIFT)
#define ESR_EL2_COND_SHIFT	(20)
#define ESR_EL2_COND		(0xfU << ESR_EL2_COND_SHIFT)


#define FSC_FAULT	(0x04)
#define FSC_PERM	(0x0c)

/* Hyp Prefetch Fault Address Register (HPFAR/HDFAR) */
#define HPFAR_MASK	(~0xFUL)

#define ESR_EL2_EC_UNKNOWN	(0x00)
#define ESR_EL2_EC_WFI		(0x01)
#define ESR_EL2_EC_CP15_32	(0x03)
#define ESR_EL2_EC_CP15_64	(0x04)
#define ESR_EL2_EC_CP14_MR	(0x05)
#define ESR_EL2_EC_CP14_LS	(0x06)
#define ESR_EL2_EC_FP_ASIMD	(0x07)
#define ESR_EL2_EC_CP10_ID	(0x08)
#define ESR_EL2_EC_CP14_64	(0x0C)
#define ESR_EL2_EC_ILL_ISS	(0x0E)
#define ESR_EL2_EC_SVC32	(0x11)
#define ESR_EL2_EC_HVC32	(0x12)
#define ESR_EL2_EC_SMC32	(0x13)
#define ESR_EL2_EC_SVC64	(0x15)
#define ESR_EL2_EC_HVC64	(0x16)
#define ESR_EL2_EC_SMC64	(0x17)
#define ESR_EL2_EC_SYS64	(0x18)
#define ESR_EL2_EC_IABT		(0x20)
#define ESR_EL2_EC_IABT_HYP	(0x21)
#define ESR_EL2_EC_PC_ALIGN	(0x22)
#define ESR_EL2_EC_DABT		(0x24)
#define ESR_EL2_EC_DABT_HYP	(0x25)
#define ESR_EL2_EC_SP_ALIGN	(0x26)
#define ESR_EL2_EC_FP_EXC32	(0x28)
#define ESR_EL2_EC_FP_EXC64	(0x2C)
#define ESR_EL2_EC_SERROR	(0x2F)
#define ESR_EL2_EC_BREAKPT	(0x30)
#define ESR_EL2_EC_BREAKPT_HYP	(0x31)
#define ESR_EL2_EC_SOFTSTP	(0x32)
#define ESR_EL2_EC_SOFTSTP_HYP	(0x33)
#define ESR_EL2_EC_WATCHPT	(0x34)
#define ESR_EL2_EC_WATCHPT_HYP	(0x35)
#define ESR_EL2_EC_BKPT32	(0x38)
#define ESR_EL2_EC_VECTOR32	(0x3A)
#define ESR_EL2_EC_BRK64	(0x3C)

#define ESR_EL2_EC_xABT_xFSR_EXTABT	0x10

#define ESR_EL2_EC_WFI_ISS_WFE	(1 << 0)

#endif /* __ARM64_KVM_ARM_H__ */

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

#include <asm/esr.h>
#include <asm/memory.h>
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
			 HCR_AMO | HCR_SWIO | HCR_TIDCP | HCR_RW)
#define HCR_VIRT_EXCP_MASK (HCR_VA | HCR_VI | HCR_VF)
#define HCR_INT_OVERRIDE   (HCR_FMO | HCR_IMO)


/* TCR_EL2 Registers bits */
#define TCR_EL2_RES1	((1 << 31) | (1 << 23))
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

/* VTCR_EL2 Registers bits */
#define VTCR_EL2_RES1		(1 << 31)
#define VTCR_EL2_PS_MASK	(7 << 16)
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
#define VTCR_EL2_VS		19

/*
 * We configure the Stage-2 page tables to always restrict the IPA space to be
 * 40 bits wide (T0SZ = 24).  Systems with a PARange smaller than 40 bits are
 * not known to exist and will break with this configuration.
 *
 * VTCR_EL2.PS is extracted from ID_AA64MMFR0_EL1.PARange at boot time
 * (see hyp-init.S).
 *
 * Note that when using 4K pages, we concatenate two first level page tables
 * together.
 *
 * The magic numbers used for VTTBR_X in this patch can be found in Tables
 * D4-23 and D4-25 in ARM DDI 0487A.b.
 */
#ifdef CONFIG_ARM64_64K_PAGES
/*
 * Stage2 translation configuration:
 * 40bits input  (T0SZ = 24)
 * 64kB pages (TG0 = 1)
 * 2 level page tables (SL = 1)
 */
#define VTCR_EL2_FLAGS		(VTCR_EL2_TG0_64K | VTCR_EL2_SH0_INNER | \
				 VTCR_EL2_ORGN0_WBWA | VTCR_EL2_IRGN0_WBWA | \
				 VTCR_EL2_SL0_LVL1 | VTCR_EL2_T0SZ_40B | \
				 VTCR_EL2_RES1)
#define VTTBR_X		(38 - VTCR_EL2_T0SZ_40B)
#else
/*
 * Stage2 translation configuration:
 * 40bits input  (T0SZ = 24)
 * 4kB pages (TG0 = 0)
 * 3 level page tables (SL = 1)
 */
#define VTCR_EL2_FLAGS		(VTCR_EL2_TG0_4K | VTCR_EL2_SH0_INNER | \
				 VTCR_EL2_ORGN0_WBWA | VTCR_EL2_IRGN0_WBWA | \
				 VTCR_EL2_SL0_LVL1 | VTCR_EL2_T0SZ_40B | \
				 VTCR_EL2_RES1)
#define VTTBR_X		(37 - VTCR_EL2_T0SZ_40B)
#endif

#define VTTBR_BADDR_SHIFT (VTTBR_X - 1)
#define VTTBR_BADDR_MASK  (((UL(1) << (PHYS_MASK_SHIFT - VTTBR_X)) - 1) << VTTBR_BADDR_SHIFT)
#define VTTBR_VMID_SHIFT  (UL(48))
#define VTTBR_VMID_MASK(size) (_AT(u64, (1 << size) - 1) << VTTBR_VMID_SHIFT)

/* Hyp System Trap Register */
#define HSTR_EL2_T(x)	(1 << x)

/* Hyp Coproccessor Trap Register Shifts */
#define CPTR_EL2_TFP_SHIFT 10

/* Hyp Coprocessor Trap Register */
#define CPTR_EL2_TCPAC	(1 << 31)
#define CPTR_EL2_TTA	(1 << 20)
#define CPTR_EL2_TFP	(1 << CPTR_EL2_TFP_SHIFT)

/* Hyp Debug Configuration Register bits */
#define MDCR_EL2_TDRA		(1 << 11)
#define MDCR_EL2_TDOSA		(1 << 10)
#define MDCR_EL2_TDA		(1 << 9)
#define MDCR_EL2_TDE		(1 << 8)
#define MDCR_EL2_HPME		(1 << 7)
#define MDCR_EL2_TPM		(1 << 6)
#define MDCR_EL2_TPMCR		(1 << 5)
#define MDCR_EL2_HPMN_MASK	(0x1F)

/* For compatibility with fault code shared with 32-bit */
#define FSC_FAULT	ESR_ELx_FSC_FAULT
#define FSC_ACCESS	ESR_ELx_FSC_ACCESS
#define FSC_PERM	ESR_ELx_FSC_PERM

/* Hyp Prefetch Fault Address Register (HPFAR/HDFAR) */
#define HPFAR_MASK	(~UL(0xf))

#define kvm_arm_exception_type	\
	{0, "IRQ" }, 		\
	{1, "TRAP" }

#define ECN(x) { ESR_ELx_EC_##x, #x }

#define kvm_arm_exception_class \
	ECN(UNKNOWN), ECN(WFx), ECN(CP15_32), ECN(CP15_64), ECN(CP14_MR), \
	ECN(CP14_LS), ECN(FP_ASIMD), ECN(CP10_ID), ECN(CP14_64), ECN(SVC64), \
	ECN(HVC64), ECN(SMC64), ECN(SYS64), ECN(IMP_DEF), ECN(IABT_LOW), \
	ECN(IABT_CUR), ECN(PC_ALIGN), ECN(DABT_LOW), ECN(DABT_CUR), \
	ECN(SP_ALIGN), ECN(FP_EXC32), ECN(FP_EXC64), ECN(SERROR), \
	ECN(BREAKPT_LOW), ECN(BREAKPT_CUR), ECN(SOFTSTP_LOW), \
	ECN(SOFTSTP_CUR), ECN(WATCHPT_LOW), ECN(WATCHPT_CUR), \
	ECN(BKPT32), ECN(VECTOR32), ECN(BRK64)

#endif /* __ARM64_KVM_ARM_H__ */

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

#ifndef __ARM_KVM_ASM_H__
#define __ARM_KVM_ASM_H__

#include <asm/virt.h>

/*
 * 0 is reserved as an invalid value.
 * Order *must* be kept in sync with the hyp switch code.
 */
#define	MPIDR_EL1	1	/* MultiProcessor Affinity Register */
#define	CSSELR_EL1	2	/* Cache Size Selection Register */
#define	SCTLR_EL1	3	/* System Control Register */
#define	ACTLR_EL1	4	/* Auxiliary Control Register */
#define	CPACR_EL1	5	/* Coprocessor Access Control */
#define	TTBR0_EL1	6	/* Translation Table Base Register 0 */
#define	TTBR1_EL1	7	/* Translation Table Base Register 1 */
#define	TCR_EL1		8	/* Translation Control Register */
#define	ESR_EL1		9	/* Exception Syndrome Register */
#define	AFSR0_EL1	10	/* Auxilary Fault Status Register 0 */
#define	AFSR1_EL1	11	/* Auxilary Fault Status Register 1 */
#define	FAR_EL1		12	/* Fault Address Register */
#define	MAIR_EL1	13	/* Memory Attribute Indirection Register */
#define	VBAR_EL1	14	/* Vector Base Address Register */
#define	CONTEXTIDR_EL1	15	/* Context ID Register */
#define	TPIDR_EL0	16	/* Thread ID, User R/W */
#define	TPIDRRO_EL0	17	/* Thread ID, User R/O */
#define	TPIDR_EL1	18	/* Thread ID, Privileged */
#define	AMAIR_EL1	19	/* Aux Memory Attribute Indirection Register */
#define	CNTKCTL_EL1	20	/* Timer Control Register (EL1) */
#define	PAR_EL1		21	/* Physical Address Register */
#define MDSCR_EL1	22	/* Monitor Debug System Control Register */
#define MDCCINT_EL1	23	/* Monitor Debug Comms Channel Interrupt Enable Reg */

/* 32bit specific registers. Keep them at the end of the range */
#define	DACR32_EL2	24	/* Domain Access Control Register */
#define	IFSR32_EL2	25	/* Instruction Fault Status Register */
#define	FPEXC32_EL2	26	/* Floating-Point Exception Control Register */
#define	DBGVCR32_EL2	27	/* Debug Vector Catch Register */
#define	NR_SYS_REGS	28

/* 32bit mapping */
#define c0_MPIDR	(MPIDR_EL1 * 2)	/* MultiProcessor ID Register */
#define c0_CSSELR	(CSSELR_EL1 * 2)/* Cache Size Selection Register */
#define c1_SCTLR	(SCTLR_EL1 * 2)	/* System Control Register */
#define c1_ACTLR	(ACTLR_EL1 * 2)	/* Auxiliary Control Register */
#define c1_CPACR	(CPACR_EL1 * 2)	/* Coprocessor Access Control */
#define c2_TTBR0	(TTBR0_EL1 * 2)	/* Translation Table Base Register 0 */
#define c2_TTBR0_high	(c2_TTBR0 + 1)	/* TTBR0 top 32 bits */
#define c2_TTBR1	(TTBR1_EL1 * 2)	/* Translation Table Base Register 1 */
#define c2_TTBR1_high	(c2_TTBR1 + 1)	/* TTBR1 top 32 bits */
#define c2_TTBCR	(TCR_EL1 * 2)	/* Translation Table Base Control R. */
#define c3_DACR		(DACR32_EL2 * 2)/* Domain Access Control Register */
#define c5_DFSR		(ESR_EL1 * 2)	/* Data Fault Status Register */
#define c5_IFSR		(IFSR32_EL2 * 2)/* Instruction Fault Status Register */
#define c5_ADFSR	(AFSR0_EL1 * 2)	/* Auxiliary Data Fault Status R */
#define c5_AIFSR	(AFSR1_EL1 * 2)	/* Auxiliary Instr Fault Status R */
#define c6_DFAR		(FAR_EL1 * 2)	/* Data Fault Address Register */
#define c6_IFAR		(c6_DFAR + 1)	/* Instruction Fault Address Register */
#define c7_PAR		(PAR_EL1 * 2)	/* Physical Address Register */
#define c7_PAR_high	(c7_PAR + 1)	/* PAR top 32 bits */
#define c10_PRRR	(MAIR_EL1 * 2)	/* Primary Region Remap Register */
#define c10_NMRR	(c10_PRRR + 1)	/* Normal Memory Remap Register */
#define c12_VBAR	(VBAR_EL1 * 2)	/* Vector Base Address Register */
#define c13_CID		(CONTEXTIDR_EL1 * 2)	/* Context ID Register */
#define c13_TID_URW	(TPIDR_EL0 * 2)	/* Thread ID, User R/W */
#define c13_TID_URO	(TPIDRRO_EL0 * 2)/* Thread ID, User R/O */
#define c13_TID_PRIV	(TPIDR_EL1 * 2)	/* Thread ID, Privileged */
#define c10_AMAIR0	(AMAIR_EL1 * 2)	/* Aux Memory Attr Indirection Reg */
#define c10_AMAIR1	(c10_AMAIR0 + 1)/* Aux Memory Attr Indirection Reg */
#define c14_CNTKCTL	(CNTKCTL_EL1 * 2) /* Timer Control Register (PL1) */

#define cp14_DBGDSCRext	(MDSCR_EL1 * 2)
#define cp14_DBGBCR0	(DBGBCR0_EL1 * 2)
#define cp14_DBGBVR0	(DBGBVR0_EL1 * 2)
#define cp14_DBGBXVR0	(cp14_DBGBVR0 + 1)
#define cp14_DBGWCR0	(DBGWCR0_EL1 * 2)
#define cp14_DBGWVR0	(DBGWVR0_EL1 * 2)
#define cp14_DBGDCCINT	(MDCCINT_EL1 * 2)

#define NR_COPRO_REGS	(NR_SYS_REGS * 2)

#define ARM_EXCEPTION_IRQ	  0
#define ARM_EXCEPTION_TRAP	  1

#define KVM_ARM64_DEBUG_DIRTY_SHIFT	0
#define KVM_ARM64_DEBUG_DIRTY		(1 << KVM_ARM64_DEBUG_DIRTY_SHIFT)

#define kvm_ksym_ref(sym)		phys_to_virt((u64)&sym - kimage_voffset)

#ifndef __ASSEMBLY__
struct kvm;
struct kvm_vcpu;

extern char __kvm_hyp_init[];
extern char __kvm_hyp_init_end[];

extern char __kvm_hyp_vector[];

#define	__kvm_hyp_code_start	__hyp_text_start
#define	__kvm_hyp_code_end	__hyp_text_end

extern void __kvm_flush_vm_context(void);
extern void __kvm_tlb_flush_vmid_ipa(struct kvm *kvm, phys_addr_t ipa);
extern void __kvm_tlb_flush_vmid(struct kvm *kvm);

extern int __kvm_vcpu_run(struct kvm_vcpu *vcpu);

extern u64 __vgic_v3_get_ich_vtr_el2(void);

extern u32 __kvm_get_mdcr_el2(void);

#endif

#endif /* __ARM_KVM_ASM_H__ */

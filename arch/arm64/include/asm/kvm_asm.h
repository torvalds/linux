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

/*
 * 0 is reserved as an invalid value.
 * Order *must* be kept in sync with the hyp switch code.
 */
#define	MPIDR_EL1	1	/* MultiProcessor Affinity Register */
#define	CSSELR_EL1	2	/* Cache Size Selection Register */
#define	SCTLR_EL1	3	/* System Control Register */
#define	ACTLR_EL1	4	/* Auxilliary Control Register */
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
#define	NR_SYS_REGS	21

#define ARM_EXCEPTION_IRQ	  0
#define ARM_EXCEPTION_TRAP	  1

#ifndef __ASSEMBLY__
struct kvm;
struct kvm_vcpu;

extern char __kvm_hyp_init[];
extern char __kvm_hyp_init_end[];

extern char __kvm_hyp_vector[];

extern char __kvm_hyp_code_start[];
extern char __kvm_hyp_code_end[];

extern void __kvm_flush_vm_context(void);
extern void __kvm_tlb_flush_vmid_ipa(struct kvm *kvm, phys_addr_t ipa);

extern int __kvm_vcpu_run(struct kvm_vcpu *vcpu);
#endif

#endif /* __ARM_KVM_ASM_H__ */

/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARM_KVM_ASM_H__
#define __ARM_KVM_ASM_H__

/* 0 is reserved as an invalid value. */
#define c0_MPIDR	1	/* MultiProcessor ID Register */
#define c0_CSSELR	2	/* Cache Size Selection Register */
#define c1_SCTLR	3	/* System Control Register */
#define c1_ACTLR	4	/* Auxilliary Control Register */
#define c1_CPACR	5	/* Coprocessor Access Control */
#define c2_TTBR0	6	/* Translation Table Base Register 0 */
#define c2_TTBR0_high	7	/* TTBR0 top 32 bits */
#define c2_TTBR1	8	/* Translation Table Base Register 1 */
#define c2_TTBR1_high	9	/* TTBR1 top 32 bits */
#define c2_TTBCR	10	/* Translation Table Base Control R. */
#define c3_DACR		11	/* Domain Access Control Register */
#define c5_DFSR		12	/* Data Fault Status Register */
#define c5_IFSR		13	/* Instruction Fault Status Register */
#define c5_ADFSR	14	/* Auxilary Data Fault Status R */
#define c5_AIFSR	15	/* Auxilary Instrunction Fault Status R */
#define c6_DFAR		16	/* Data Fault Address Register */
#define c6_IFAR		17	/* Instruction Fault Address Register */
#define c7_PAR		18	/* Physical Address Register */
#define c7_PAR_high	19	/* PAR top 32 bits */
#define c9_L2CTLR	20	/* Cortex A15/A7 L2 Control Register */
#define c10_PRRR	21	/* Primary Region Remap Register */
#define c10_NMRR	22	/* Normal Memory Remap Register */
#define c12_VBAR	23	/* Vector Base Address Register */
#define c13_CID		24	/* Context ID Register */
#define c13_TID_URW	25	/* Thread ID, User R/W */
#define c13_TID_URO	26	/* Thread ID, User R/O */
#define c13_TID_PRIV	27	/* Thread ID, Privileged */
#define c14_CNTKCTL	28	/* Timer Control Register (PL1) */
#define c10_AMAIR0	29	/* Auxilary Memory Attribute Indirection Reg0 */
#define c10_AMAIR1	30	/* Auxilary Memory Attribute Indirection Reg1 */
#define NR_CP15_REGS	31	/* Number of regs (incl. invalid) */

#define ARM_EXCEPTION_RESET	  0
#define ARM_EXCEPTION_UNDEFINED   1
#define ARM_EXCEPTION_SOFTWARE    2
#define ARM_EXCEPTION_PREF_ABORT  3
#define ARM_EXCEPTION_DATA_ABORT  4
#define ARM_EXCEPTION_IRQ	  5
#define ARM_EXCEPTION_FIQ	  6
#define ARM_EXCEPTION_HVC	  7

#ifndef __ASSEMBLY__
struct kvm;
struct kvm_vcpu;

extern char __kvm_hyp_init[];
extern char __kvm_hyp_init_end[];

extern char __kvm_hyp_exit[];
extern char __kvm_hyp_exit_end[];

extern char __kvm_hyp_vector[];

extern char __kvm_hyp_code_start[];
extern char __kvm_hyp_code_end[];

extern void __kvm_flush_vm_context(void);
extern void __kvm_tlb_flush_vmid_ipa(struct kvm *kvm, phys_addr_t ipa);

extern int __kvm_vcpu_run(struct kvm_vcpu *vcpu);
#endif

#endif /* __ARM_KVM_ASM_H__ */

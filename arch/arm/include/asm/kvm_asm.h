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

#include <asm/virt.h>

#define ARM_EXIT_WITH_ABORT_BIT  31
#define ARM_EXCEPTION_CODE(x)	  ((x) & ~(1U << ARM_EXIT_WITH_ABORT_BIT))
#define ARM_ABORT_PENDING(x)	  !!((x) & (1U << ARM_EXIT_WITH_ABORT_BIT))

#define ARM_EXCEPTION_RESET	  0
#define ARM_EXCEPTION_UNDEFINED   1
#define ARM_EXCEPTION_SOFTWARE    2
#define ARM_EXCEPTION_PREF_ABORT  3
#define ARM_EXCEPTION_DATA_ABORT  4
#define ARM_EXCEPTION_IRQ	  5
#define ARM_EXCEPTION_FIQ	  6
#define ARM_EXCEPTION_HVC	  7
#define ARM_EXCEPTION_HYP_GONE	  HVC_STUB_ERR
/*
 * The rr_lo_hi macro swaps a pair of registers depending on
 * current endianness. It is used in conjunction with ldrd and strd
 * instructions that load/store a 64-bit value from/to memory to/from
 * a pair of registers which are used with the mrrc and mcrr instructions.
 * If used with the ldrd/strd instructions, the a1 parameter is the first
 * source/destination register and the a2 parameter is the second
 * source/destination register. Note that the ldrd/strd instructions
 * already swap the bytes within the words correctly according to the
 * endianness setting, but the order of the registers need to be effectively
 * swapped when used with the mrrc/mcrr instructions.
 */
#ifdef CONFIG_CPU_ENDIAN_BE8
#define rr_lo_hi(a1, a2) a2, a1
#else
#define rr_lo_hi(a1, a2) a1, a2
#endif

#define kvm_ksym_ref(kva)	(kva)

#ifndef __ASSEMBLY__
struct kvm;
struct kvm_vcpu;

extern char __kvm_hyp_init[];
extern char __kvm_hyp_init_end[];

extern char __kvm_hyp_vector[];

extern void __kvm_flush_vm_context(void);
extern void __kvm_tlb_flush_vmid_ipa(struct kvm *kvm, phys_addr_t ipa);
extern void __kvm_tlb_flush_vmid(struct kvm *kvm);
extern void __kvm_tlb_flush_local_vmid(struct kvm_vcpu *vcpu);

extern int __kvm_vcpu_run(struct kvm_vcpu *vcpu);

extern void __init_stage2_translation(void);

extern u64 __vgic_v3_get_ich_vtr_el2(void);
extern u64 __vgic_v3_read_vmcr(void);
extern void __vgic_v3_write_vmcr(u32 vmcr);
extern void __vgic_v3_init_lrs(void);

#endif

#endif /* __ARM_KVM_ASM_H__ */

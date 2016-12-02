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

#define ARM_EXCEPTION_IRQ	  0
#define ARM_EXCEPTION_TRAP	  1
/* The hyp-stub will return this for any kvm_call_hyp() call */
#define ARM_EXCEPTION_HYP_GONE	  2

#define KVM_ARM64_DEBUG_DIRTY_SHIFT	0
#define KVM_ARM64_DEBUG_DIRTY		(1 << KVM_ARM64_DEBUG_DIRTY_SHIFT)

#define kvm_ksym_ref(sym)		phys_to_virt((u64)&sym - kimage_voffset)

#ifndef __ASSEMBLY__
#if __GNUC__ > 4
#define kvm_ksym_shift			(PAGE_OFFSET - KIMAGE_VADDR)
#else
/*
 * GCC versions 4.9 and older will fold the constant below into the addend of
 * the reference to 'sym' above if kvm_ksym_shift is declared static or if the
 * constant is used directly. However, since we use the small code model for
 * the core kernel, the reference to 'sym' will be emitted as a adrp/add pair,
 * with a +/- 4 GB range, resulting in linker relocation errors if the shift
 * is sufficiently large. So prevent the compiler from folding the shift into
 * the addend, by making the shift a variable with external linkage.
 */
__weak u64 kvm_ksym_shift = PAGE_OFFSET - KIMAGE_VADDR;
#endif

struct kvm;
struct kvm_vcpu;

extern char __kvm_hyp_init[];
extern char __kvm_hyp_init_end[];
extern char __kvm_hyp_reset[];

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

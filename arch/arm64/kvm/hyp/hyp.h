/*
 * Copyright (C) 2015 - ARM Ltd
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

#ifndef __ARM64_KVM_HYP_H__
#define __ARM64_KVM_HYP_H__

#include <linux/compiler.h>
#include <linux/kvm_host.h>
#include <asm/kvm_mmu.h>
#include <asm/sysreg.h>

#define __hyp_text __section(.hyp.text) notrace

#define kern_hyp_va(v) (typeof(v))((unsigned long)(v) & HYP_PAGE_OFFSET_MASK)
#define hyp_kern_va(v) (typeof(v))((unsigned long)(v) - HYP_PAGE_OFFSET \
						      + PAGE_OFFSET)

/**
 * hyp_alternate_select - Generates patchable code sequences that are
 * used to switch between two implementations of a function, depending
 * on the availability of a feature.
 *
 * @fname: a symbol name that will be defined as a function returning a
 * function pointer whose type will match @orig and @alt
 * @orig: A pointer to the default function, as returned by @fname when
 * @cond doesn't hold
 * @alt: A pointer to the alternate function, as returned by @fname
 * when @cond holds
 * @cond: a CPU feature (as described in asm/cpufeature.h)
 */
#define hyp_alternate_select(fname, orig, alt, cond)			\
typeof(orig) * __hyp_text fname(void)					\
{									\
	typeof(alt) *val = orig;					\
	asm volatile(ALTERNATIVE("nop		\n",			\
				 "mov	%0, %1	\n",			\
				 cond)					\
		     : "+r" (val) : "r" (alt));				\
	return val;							\
}

void __vgic_v2_save_state(struct kvm_vcpu *vcpu);
void __vgic_v2_restore_state(struct kvm_vcpu *vcpu);

void __vgic_v3_save_state(struct kvm_vcpu *vcpu);
void __vgic_v3_restore_state(struct kvm_vcpu *vcpu);

void __timer_save_state(struct kvm_vcpu *vcpu);
void __timer_restore_state(struct kvm_vcpu *vcpu);

void __sysreg_save_state(struct kvm_cpu_context *ctxt);
void __sysreg_restore_state(struct kvm_cpu_context *ctxt);
void __sysreg32_save_state(struct kvm_vcpu *vcpu);
void __sysreg32_restore_state(struct kvm_vcpu *vcpu);

void __debug_save_state(struct kvm_vcpu *vcpu,
			struct kvm_guest_debug_arch *dbg,
			struct kvm_cpu_context *ctxt);
void __debug_restore_state(struct kvm_vcpu *vcpu,
			   struct kvm_guest_debug_arch *dbg,
			   struct kvm_cpu_context *ctxt);
void __debug_cond_save_host_state(struct kvm_vcpu *vcpu);
void __debug_cond_restore_host_state(struct kvm_vcpu *vcpu);

void __fpsimd_save_state(struct user_fpsimd_state *fp_regs);
void __fpsimd_restore_state(struct user_fpsimd_state *fp_regs);
static inline bool __fpsimd_enabled(void)
{
	return !(read_sysreg(cptr_el2) & CPTR_EL2_TFP);
}

u64 __guest_enter(struct kvm_vcpu *vcpu, struct kvm_cpu_context *host_ctxt);
void __noreturn __hyp_do_panic(unsigned long, ...);

#endif /* __ARM64_KVM_HYP_H__ */


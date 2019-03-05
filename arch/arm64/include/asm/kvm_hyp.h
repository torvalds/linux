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
#include <asm/alternative.h>
#include <asm/sysreg.h>

#define __hyp_text __section(.hyp.text) notrace

#define read_sysreg_elx(r,nvh,vh)					\
	({								\
		u64 reg;						\
		asm volatile(ALTERNATIVE("mrs %0, " __stringify(r##nvh),\
					 "mrs_s %0, " __stringify(r##vh),\
					 ARM64_HAS_VIRT_HOST_EXTN)	\
			     : "=r" (reg));				\
		reg;							\
	})

#define write_sysreg_elx(v,r,nvh,vh)					\
	do {								\
		u64 __val = (u64)(v);					\
		asm volatile(ALTERNATIVE("msr " __stringify(r##nvh) ", %x0",\
					 "msr_s " __stringify(r##vh) ", %x0",\
					 ARM64_HAS_VIRT_HOST_EXTN)	\
					 : : "rZ" (__val));		\
	} while (0)

/*
 * Unified accessors for registers that have a different encoding
 * between VHE and non-VHE. They must be specified without their "ELx"
 * encoding.
 */
#define read_sysreg_el2(r)						\
	({								\
		u64 reg;						\
		asm volatile(ALTERNATIVE("mrs %0, " __stringify(r##_EL2),\
					 "mrs %0, " __stringify(r##_EL1),\
					 ARM64_HAS_VIRT_HOST_EXTN)	\
			     : "=r" (reg));				\
		reg;							\
	})

#define write_sysreg_el2(v,r)						\
	do {								\
		u64 __val = (u64)(v);					\
		asm volatile(ALTERNATIVE("msr " __stringify(r##_EL2) ", %x0",\
					 "msr " __stringify(r##_EL1) ", %x0",\
					 ARM64_HAS_VIRT_HOST_EXTN)	\
					 : : "rZ" (__val));		\
	} while (0)

#define read_sysreg_el0(r)	read_sysreg_elx(r, _EL0, _EL02)
#define write_sysreg_el0(v,r)	write_sysreg_elx(v, r, _EL0, _EL02)
#define read_sysreg_el1(r)	read_sysreg_elx(r, _EL1, _EL12)
#define write_sysreg_el1(v,r)	write_sysreg_elx(v, r, _EL1, _EL12)

/* The VHE specific system registers and their encoding */
#define sctlr_EL12              sys_reg(3, 5, 1, 0, 0)
#define cpacr_EL12              sys_reg(3, 5, 1, 0, 2)
#define ttbr0_EL12              sys_reg(3, 5, 2, 0, 0)
#define ttbr1_EL12              sys_reg(3, 5, 2, 0, 1)
#define tcr_EL12                sys_reg(3, 5, 2, 0, 2)
#define afsr0_EL12              sys_reg(3, 5, 5, 1, 0)
#define afsr1_EL12              sys_reg(3, 5, 5, 1, 1)
#define esr_EL12                sys_reg(3, 5, 5, 2, 0)
#define far_EL12                sys_reg(3, 5, 6, 0, 0)
#define mair_EL12               sys_reg(3, 5, 10, 2, 0)
#define amair_EL12              sys_reg(3, 5, 10, 3, 0)
#define vbar_EL12               sys_reg(3, 5, 12, 0, 0)
#define contextidr_EL12         sys_reg(3, 5, 13, 0, 1)
#define cntkctl_EL12            sys_reg(3, 5, 14, 1, 0)
#define cntp_tval_EL02          sys_reg(3, 5, 14, 2, 0)
#define cntp_ctl_EL02           sys_reg(3, 5, 14, 2, 1)
#define cntp_cval_EL02          sys_reg(3, 5, 14, 2, 2)
#define cntv_tval_EL02          sys_reg(3, 5, 14, 3, 0)
#define cntv_ctl_EL02           sys_reg(3, 5, 14, 3, 1)
#define cntv_cval_EL02          sys_reg(3, 5, 14, 3, 2)
#define spsr_EL12               sys_reg(3, 5, 4, 0, 0)
#define elr_EL12                sys_reg(3, 5, 4, 0, 1)

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

int __vgic_v2_perform_cpuif_access(struct kvm_vcpu *vcpu);

void __vgic_v3_save_state(struct kvm_vcpu *vcpu);
void __vgic_v3_restore_state(struct kvm_vcpu *vcpu);
void __vgic_v3_activate_traps(struct kvm_vcpu *vcpu);
void __vgic_v3_deactivate_traps(struct kvm_vcpu *vcpu);
void __vgic_v3_save_aprs(struct kvm_vcpu *vcpu);
void __vgic_v3_restore_aprs(struct kvm_vcpu *vcpu);
int __vgic_v3_perform_cpuif_access(struct kvm_vcpu *vcpu);

void __timer_enable_traps(struct kvm_vcpu *vcpu);
void __timer_disable_traps(struct kvm_vcpu *vcpu);

void __sysreg_save_state_nvhe(struct kvm_cpu_context *ctxt);
void __sysreg_restore_state_nvhe(struct kvm_cpu_context *ctxt);
void sysreg_save_host_state_vhe(struct kvm_cpu_context *ctxt);
void sysreg_restore_host_state_vhe(struct kvm_cpu_context *ctxt);
void sysreg_save_guest_state_vhe(struct kvm_cpu_context *ctxt);
void sysreg_restore_guest_state_vhe(struct kvm_cpu_context *ctxt);
void __sysreg32_save_state(struct kvm_vcpu *vcpu);
void __sysreg32_restore_state(struct kvm_vcpu *vcpu);

void __debug_switch_to_guest(struct kvm_vcpu *vcpu);
void __debug_switch_to_host(struct kvm_vcpu *vcpu);

void __fpsimd_save_state(struct user_fpsimd_state *fp_regs);
void __fpsimd_restore_state(struct user_fpsimd_state *fp_regs);
bool __fpsimd_enabled(void);

void activate_traps_vhe_load(struct kvm_vcpu *vcpu);
void deactivate_traps_vhe_put(void);

u64 __guest_enter(struct kvm_vcpu *vcpu, struct kvm_cpu_context *host_ctxt);
void __noreturn __hyp_do_panic(unsigned long, ...);

/*
 * Must be called from hyp code running at EL2 with an updated VTTBR
 * and interrupts disabled.
 */
static __always_inline void __hyp_text __load_guest_stage2(struct kvm *kvm)
{
	write_sysreg(kvm->arch.vtcr, vtcr_el2);
	write_sysreg(kvm->arch.vttbr, vttbr_el2);

	/*
	 * ARM erratum 1165522 requires the actual execution of the above
	 * before we can switch to the EL1/EL0 translation regime used by
	 * the guest.
	 */
	asm(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_1165522));
}

#endif /* __ARM64_KVM_HYP_H__ */


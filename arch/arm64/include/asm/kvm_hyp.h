/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM64_KVM_HYP_H__
#define __ARM64_KVM_HYP_H__

#include <linux/compiler.h>
#include <linux/kvm_host.h>
#include <asm/alternative.h>
#include <asm/sysreg.h>

DECLARE_PER_CPU(struct kvm_cpu_context, kvm_hyp_ctxt);
DECLARE_PER_CPU(unsigned long, kvm_hyp_vector);

#define read_sysreg_elx(r,nvh,vh)					\
	({								\
		u64 reg;						\
		asm volatile(ALTERNATIVE(__mrs_s("%0", r##nvh),	\
					 __mrs_s("%0", r##vh),		\
					 ARM64_HAS_VIRT_HOST_EXTN)	\
			     : "=r" (reg));				\
		reg;							\
	})

#define write_sysreg_elx(v,r,nvh,vh)					\
	do {								\
		u64 __val = (u64)(v);					\
		asm volatile(ALTERNATIVE(__msr_s(r##nvh, "%x0"),	\
					 __msr_s(r##vh, "%x0"),		\
					 ARM64_HAS_VIRT_HOST_EXTN)	\
					 : : "rZ" (__val));		\
	} while (0)

/*
 * Unified accessors for registers that have a different encoding
 * between VHE and non-VHE. They must be specified without their "ELx"
 * encoding, but with the SYS_ prefix, as defined in asm/sysreg.h.
 */

#define read_sysreg_el0(r)	read_sysreg_elx(r, _EL0, _EL02)
#define write_sysreg_el0(v,r)	write_sysreg_elx(v, r, _EL0, _EL02)
#define read_sysreg_el1(r)	read_sysreg_elx(r, _EL1, _EL12)
#define write_sysreg_el1(v,r)	write_sysreg_elx(v, r, _EL1, _EL12)
#define read_sysreg_el2(r)	read_sysreg_elx(r, _EL2, _EL1)
#define write_sysreg_el2(v,r)	write_sysreg_elx(v, r, _EL2, _EL1)

/*
 * Without an __arch_swab32(), we fall back to ___constant_swab32(), but the
 * static inline can allow the compiler to out-of-line this. KVM always wants
 * the macro version as its always inlined.
 */
#define __kvm_swab32(x)	___constant_swab32(x)

int __vgic_v2_perform_cpuif_access(struct kvm_vcpu *vcpu);

void __vgic_v3_save_state(struct vgic_v3_cpu_if *cpu_if);
void __vgic_v3_restore_state(struct vgic_v3_cpu_if *cpu_if);
void __vgic_v3_activate_traps(struct vgic_v3_cpu_if *cpu_if);
void __vgic_v3_deactivate_traps(struct vgic_v3_cpu_if *cpu_if);
void __vgic_v3_save_aprs(struct vgic_v3_cpu_if *cpu_if);
void __vgic_v3_restore_aprs(struct vgic_v3_cpu_if *cpu_if);
int __vgic_v3_perform_cpuif_access(struct kvm_vcpu *vcpu);

#ifdef __KVM_NVHE_HYPERVISOR__
void __timer_enable_traps(struct kvm_vcpu *vcpu);
void __timer_disable_traps(struct kvm_vcpu *vcpu);
#endif

#ifdef __KVM_NVHE_HYPERVISOR__
void __sysreg_save_state_nvhe(struct kvm_cpu_context *ctxt);
void __sysreg_restore_state_nvhe(struct kvm_cpu_context *ctxt);
#else
void sysreg_save_host_state_vhe(struct kvm_cpu_context *ctxt);
void sysreg_restore_host_state_vhe(struct kvm_cpu_context *ctxt);
void sysreg_save_guest_state_vhe(struct kvm_cpu_context *ctxt);
void sysreg_restore_guest_state_vhe(struct kvm_cpu_context *ctxt);
#endif

void __debug_switch_to_guest(struct kvm_vcpu *vcpu);
void __debug_switch_to_host(struct kvm_vcpu *vcpu);

void __fpsimd_save_state(struct user_fpsimd_state *fp_regs);
void __fpsimd_restore_state(struct user_fpsimd_state *fp_regs);

#ifndef __KVM_NVHE_HYPERVISOR__
void activate_traps_vhe_load(struct kvm_vcpu *vcpu);
void deactivate_traps_vhe_put(void);
#endif

u64 __guest_enter(struct kvm_vcpu *vcpu);

void __noreturn hyp_panic(void);
#ifdef __KVM_NVHE_HYPERVISOR__
void __noreturn __hyp_do_panic(bool restore_host, u64 spsr, u64 elr, u64 par);
#endif

#endif /* __ARM64_KVM_HYP_H__ */


/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM_KVM_HYP_H__
#define __ARM_KVM_HYP_H__

#include <linux/compiler.h>
#include <linux/kvm_host.h>
#include <asm/cp15.h>
#include <asm/vfp.h>

#define __hyp_text __section(.hyp.text) notrace

#define __ACCESS_VFP(CRn)			\
	"mrc", "mcr", __stringify(p10, 7, %0, CRn, cr0, 0), u32

#define write_special(v, r)					\
	asm volatile("msr " __stringify(r) ", %0" : : "r" (v))
#define read_special(r) ({					\
	u32 __val;						\
	asm volatile("mrs %0, " __stringify(r) : "=r" (__val));	\
	__val;							\
})

#define TTBR0		__ACCESS_CP15_64(0, c2)
#define TTBR1		__ACCESS_CP15_64(1, c2)
#define VTTBR		__ACCESS_CP15_64(6, c2)
#define PAR		__ACCESS_CP15_64(0, c7)
#define CNTP_CVAL	__ACCESS_CP15_64(2, c14)
#define CNTV_CVAL	__ACCESS_CP15_64(3, c14)
#define CNTVOFF		__ACCESS_CP15_64(4, c14)

#define MIDR		__ACCESS_CP15(c0, 0, c0, 0)
#define CSSELR		__ACCESS_CP15(c0, 2, c0, 0)
#define VPIDR		__ACCESS_CP15(c0, 4, c0, 0)
#define VMPIDR		__ACCESS_CP15(c0, 4, c0, 5)
#define SCTLR		__ACCESS_CP15(c1, 0, c0, 0)
#define CPACR		__ACCESS_CP15(c1, 0, c0, 2)
#define HCR		__ACCESS_CP15(c1, 4, c1, 0)
#define HDCR		__ACCESS_CP15(c1, 4, c1, 1)
#define HCPTR		__ACCESS_CP15(c1, 4, c1, 2)
#define HSTR		__ACCESS_CP15(c1, 4, c1, 3)
#define TTBCR		__ACCESS_CP15(c2, 0, c0, 2)
#define HTCR		__ACCESS_CP15(c2, 4, c0, 2)
#define VTCR		__ACCESS_CP15(c2, 4, c1, 2)
#define DACR		__ACCESS_CP15(c3, 0, c0, 0)
#define DFSR		__ACCESS_CP15(c5, 0, c0, 0)
#define IFSR		__ACCESS_CP15(c5, 0, c0, 1)
#define ADFSR		__ACCESS_CP15(c5, 0, c1, 0)
#define AIFSR		__ACCESS_CP15(c5, 0, c1, 1)
#define HSR		__ACCESS_CP15(c5, 4, c2, 0)
#define DFAR		__ACCESS_CP15(c6, 0, c0, 0)
#define IFAR		__ACCESS_CP15(c6, 0, c0, 2)
#define HDFAR		__ACCESS_CP15(c6, 4, c0, 0)
#define HIFAR		__ACCESS_CP15(c6, 4, c0, 2)
#define HPFAR		__ACCESS_CP15(c6, 4, c0, 4)
#define ICIALLUIS	__ACCESS_CP15(c7, 0, c1, 0)
#define BPIALLIS	__ACCESS_CP15(c7, 0, c1, 6)
#define ICIMVAU		__ACCESS_CP15(c7, 0, c5, 1)
#define ATS1CPR		__ACCESS_CP15(c7, 0, c8, 0)
#define TLBIALLIS	__ACCESS_CP15(c8, 0, c3, 0)
#define TLBIALL		__ACCESS_CP15(c8, 0, c7, 0)
#define TLBIALLNSNHIS	__ACCESS_CP15(c8, 4, c3, 4)
#define PRRR		__ACCESS_CP15(c10, 0, c2, 0)
#define NMRR		__ACCESS_CP15(c10, 0, c2, 1)
#define AMAIR0		__ACCESS_CP15(c10, 0, c3, 0)
#define AMAIR1		__ACCESS_CP15(c10, 0, c3, 1)
#define VBAR		__ACCESS_CP15(c12, 0, c0, 0)
#define CID		__ACCESS_CP15(c13, 0, c0, 1)
#define TID_URW		__ACCESS_CP15(c13, 0, c0, 2)
#define TID_URO		__ACCESS_CP15(c13, 0, c0, 3)
#define TID_PRIV	__ACCESS_CP15(c13, 0, c0, 4)
#define HTPIDR		__ACCESS_CP15(c13, 4, c0, 2)
#define CNTKCTL		__ACCESS_CP15(c14, 0, c1, 0)
#define CNTP_CTL	__ACCESS_CP15(c14, 0, c2, 1)
#define CNTV_CTL	__ACCESS_CP15(c14, 0, c3, 1)
#define CNTHCTL		__ACCESS_CP15(c14, 4, c1, 0)

#define VFP_FPEXC	__ACCESS_VFP(FPEXC)

/* AArch64 compatibility macros, only for the timer so far */
#define read_sysreg_el0(r)		read_sysreg(r##_el0)
#define write_sysreg_el0(v, r)		write_sysreg(v, r##_el0)

#define cntp_ctl_el0			CNTP_CTL
#define cntp_cval_el0			CNTP_CVAL
#define cntv_ctl_el0			CNTV_CTL
#define cntv_cval_el0			CNTV_CVAL
#define cntvoff_el2			CNTVOFF
#define cnthctl_el2			CNTHCTL

void __timer_enable_traps(struct kvm_vcpu *vcpu);
void __timer_disable_traps(struct kvm_vcpu *vcpu);

void __vgic_v2_save_state(struct kvm_vcpu *vcpu);
void __vgic_v2_restore_state(struct kvm_vcpu *vcpu);

void __sysreg_save_state(struct kvm_cpu_context *ctxt);
void __sysreg_restore_state(struct kvm_cpu_context *ctxt);

void __vgic_v3_save_state(struct kvm_vcpu *vcpu);
void __vgic_v3_restore_state(struct kvm_vcpu *vcpu);
void __vgic_v3_activate_traps(struct kvm_vcpu *vcpu);
void __vgic_v3_deactivate_traps(struct kvm_vcpu *vcpu);
void __vgic_v3_save_aprs(struct kvm_vcpu *vcpu);
void __vgic_v3_restore_aprs(struct kvm_vcpu *vcpu);

asmlinkage void __vfp_save_state(struct vfp_hard_struct *vfp);
asmlinkage void __vfp_restore_state(struct vfp_hard_struct *vfp);
static inline bool __vfp_enabled(void)
{
	return !(read_sysreg(HCPTR) & (HCPTR_TCP(11) | HCPTR_TCP(10)));
}

void __hyp_text __banked_save_state(struct kvm_cpu_context *ctxt);
void __hyp_text __banked_restore_state(struct kvm_cpu_context *ctxt);

asmlinkage int __guest_enter(struct kvm_vcpu *vcpu,
			     struct kvm_cpu_context *host);
asmlinkage int __hyp_do_panic(const char *, int, u32);

#endif /* __ARM_KVM_HYP_H__ */

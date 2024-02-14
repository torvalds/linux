/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_NESTED_H
#define __ARM64_KVM_NESTED_H

#include <linux/bitfield.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>

static inline bool vcpu_has_nv(const struct kvm_vcpu *vcpu)
{
	return (!__is_defined(__KVM_NVHE_HYPERVISOR__) &&
		cpus_have_final_cap(ARM64_HAS_NESTED_VIRT) &&
		vcpu_has_feature(vcpu, KVM_ARM_VCPU_HAS_EL2));
}

/* Translation helpers from non-VHE EL2 to EL1 */
static inline u64 tcr_el2_ps_to_tcr_el1_ips(u64 tcr_el2)
{
	return (u64)FIELD_GET(TCR_EL2_PS_MASK, tcr_el2) << TCR_IPS_SHIFT;
}

static inline u64 translate_tcr_el2_to_tcr_el1(u64 tcr)
{
	return TCR_EPD1_MASK |				/* disable TTBR1_EL1 */
	       ((tcr & TCR_EL2_TBI) ? TCR_TBI0 : 0) |
	       tcr_el2_ps_to_tcr_el1_ips(tcr) |
	       (tcr & TCR_EL2_TG0_MASK) |
	       (tcr & TCR_EL2_ORGN0_MASK) |
	       (tcr & TCR_EL2_IRGN0_MASK) |
	       (tcr & TCR_EL2_T0SZ_MASK);
}

static inline u64 translate_cptr_el2_to_cpacr_el1(u64 cptr_el2)
{
	u64 cpacr_el1 = 0;

	if (cptr_el2 & CPTR_EL2_TTA)
		cpacr_el1 |= CPACR_ELx_TTA;
	if (!(cptr_el2 & CPTR_EL2_TFP))
		cpacr_el1 |= CPACR_ELx_FPEN;
	if (!(cptr_el2 & CPTR_EL2_TZ))
		cpacr_el1 |= CPACR_ELx_ZEN;

	return cpacr_el1;
}

static inline u64 translate_sctlr_el2_to_sctlr_el1(u64 val)
{
	/* Only preserve the minimal set of bits we support */
	val &= (SCTLR_ELx_M | SCTLR_ELx_A | SCTLR_ELx_C | SCTLR_ELx_SA |
		SCTLR_ELx_I | SCTLR_ELx_IESB | SCTLR_ELx_WXN | SCTLR_ELx_EE);
	val |= SCTLR_EL1_RES1;

	return val;
}

static inline u64 translate_ttbr0_el2_to_ttbr0_el1(u64 ttbr0)
{
	/* Clear the ASID field */
	return ttbr0 & ~GENMASK_ULL(63, 48);
}


int kvm_init_nv_sysregs(struct kvm *kvm);

#endif /* __ARM64_KVM_NESTED_H */

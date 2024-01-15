/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_NESTED_H
#define __ARM64_KVM_NESTED_H

#include <asm/kvm_emulate.h>
#include <linux/kvm_host.h>

static inline bool vcpu_has_nv(const struct kvm_vcpu *vcpu)
{
	return (!__is_defined(__KVM_NVHE_HYPERVISOR__) &&
		cpus_have_final_cap(ARM64_HAS_NESTED_VIRT) &&
		vcpu_has_feature(vcpu, KVM_ARM_VCPU_HAS_EL2));
}

extern bool __check_nv_sr_forward(struct kvm_vcpu *vcpu);

struct sys_reg_params;
struct sys_reg_desc;

void access_nested_id_reg(struct kvm_vcpu *v, struct sys_reg_params *p,
			  const struct sys_reg_desc *r);

#endif /* __ARM64_KVM_NESTED_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM L1 hypervisor optimizations on Hyper-V.
 */

#ifndef __ARCH_X86_KVM_KVM_ONHYPERV_H__
#define __ARCH_X86_KVM_KVM_ONHYPERV_H__

#if IS_ENABLED(CONFIG_HYPERV)
int hv_remote_flush_tlb_with_range(struct kvm *kvm,
		struct kvm_tlb_range *range);
int hv_remote_flush_tlb(struct kvm *kvm);

static inline void hv_track_root_tdp(struct kvm_vcpu *vcpu, hpa_t root_tdp)
{
	struct kvm_arch *kvm_arch = &vcpu->kvm->arch;

	if (kvm_x86_ops.tlb_remote_flush == hv_remote_flush_tlb) {
		spin_lock(&kvm_arch->hv_root_tdp_lock);
		vcpu->arch.hv_root_tdp = root_tdp;
		if (root_tdp != kvm_arch->hv_root_tdp)
			kvm_arch->hv_root_tdp = INVALID_PAGE;
		spin_unlock(&kvm_arch->hv_root_tdp_lock);
	}
}
#else /* !CONFIG_HYPERV */
static inline void hv_track_root_tdp(struct kvm_vcpu *vcpu, hpa_t root_tdp)
{
}
#endif /* !CONFIG_HYPERV */

#endif

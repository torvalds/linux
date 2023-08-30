/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM L1 hypervisor optimizations on Hyper-V.
 */

#ifndef __ARCH_X86_KVM_KVM_ONHYPERV_H__
#define __ARCH_X86_KVM_KVM_ONHYPERV_H__

#if IS_ENABLED(CONFIG_HYPERV)
int hv_flush_remote_tlbs_range(struct kvm *kvm, gfn_t gfn, gfn_t nr_pages);
int hv_flush_remote_tlbs(struct kvm *kvm);
void hv_track_root_tdp(struct kvm_vcpu *vcpu, hpa_t root_tdp);
#else /* !CONFIG_HYPERV */
static inline int hv_flush_remote_tlbs(struct kvm *kvm)
{
	return -EOPNOTSUPP;
}

static inline void hv_track_root_tdp(struct kvm_vcpu *vcpu, hpa_t root_tdp)
{
}
#endif /* !CONFIG_HYPERV */

#endif

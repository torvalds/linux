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
static inline hpa_t hv_get_partition_assist_page(struct kvm_vcpu *vcpu)
{
	/*
	 * Partition assist page is something which Hyper-V running in L0
	 * requires from KVM running in L1 before direct TLB flush for L2
	 * guests can be enabled. KVM doesn't currently use the page but to
	 * comply with TLFS it still needs to be allocated. For now, this
	 * is a single page shared among all vCPUs.
	 */
	struct hv_partition_assist_pg **p_hv_pa_pg =
		&vcpu->kvm->arch.hv_pa_pg;

	if (!*p_hv_pa_pg)
		*p_hv_pa_pg = kzalloc(PAGE_SIZE, GFP_KERNEL_ACCOUNT);

	if (!*p_hv_pa_pg)
		return INVALID_PAGE;

	return __pa(*p_hv_pa_pg);
}
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

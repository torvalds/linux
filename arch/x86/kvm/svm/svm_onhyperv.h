/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM L1 hypervisor optimizations on Hyper-V for SVM.
 */

#ifndef __ARCH_X86_KVM_SVM_ONHYPERV_H__
#define __ARCH_X86_KVM_SVM_ONHYPERV_H__

#include <asm/mshyperv.h>

#if IS_ENABLED(CONFIG_HYPERV)

#include "kvm_onhyperv.h"
#include "svm/hyperv.h"

__init void svm_hv_hardware_setup(void);

static inline bool svm_hv_is_enlightened_tlb_enabled(struct kvm_vcpu *vcpu)
{
	struct hv_vmcb_enlightenments *hve = &to_svm(vcpu)->vmcb->control.hv_enlightenments;

	return ms_hyperv.nested_features & HV_X64_NESTED_ENLIGHTENED_TLB &&
	       !!hve->hv_enlightenments_control.enlightened_npt_tlb;
}

static inline void svm_hv_init_vmcb(struct vmcb *vmcb)
{
	struct hv_vmcb_enlightenments *hve = &vmcb->control.hv_enlightenments;

	BUILD_BUG_ON(sizeof(vmcb->control.hv_enlightenments) !=
		     sizeof(vmcb->control.reserved_sw));

	if (npt_enabled &&
	    ms_hyperv.nested_features & HV_X64_NESTED_ENLIGHTENED_TLB)
		hve->hv_enlightenments_control.enlightened_npt_tlb = 1;

	if (ms_hyperv.nested_features & HV_X64_NESTED_MSR_BITMAP)
		hve->hv_enlightenments_control.msr_bitmap = 1;
}

static inline void svm_hv_vmcb_dirty_nested_enlightenments(
		struct kvm_vcpu *vcpu)
{
	struct vmcb *vmcb = to_svm(vcpu)->vmcb;
	struct hv_vmcb_enlightenments *hve = &vmcb->control.hv_enlightenments;

	if (hve->hv_enlightenments_control.msr_bitmap)
		vmcb_mark_dirty(vmcb, HV_VMCB_NESTED_ENLIGHTENMENTS);
}

static inline void svm_hv_update_vp_id(struct vmcb *vmcb, struct kvm_vcpu *vcpu)
{
	struct hv_vmcb_enlightenments *hve = &vmcb->control.hv_enlightenments;
	u32 vp_index = kvm_hv_get_vpindex(vcpu);

	if (hve->hv_vp_id != vp_index) {
		hve->hv_vp_id = vp_index;
		vmcb_mark_dirty(vmcb, HV_VMCB_NESTED_ENLIGHTENMENTS);
	}
}
#else

static inline bool svm_hv_is_enlightened_tlb_enabled(struct kvm_vcpu *vcpu)
{
	return false;
}

static inline void svm_hv_init_vmcb(struct vmcb *vmcb)
{
}

static inline __init void svm_hv_hardware_setup(void)
{
}

static inline void svm_hv_vmcb_dirty_nested_enlightenments(
		struct kvm_vcpu *vcpu)
{
}

static inline void svm_hv_update_vp_id(struct vmcb *vmcb,
		struct kvm_vcpu *vcpu)
{
}
#endif /* CONFIG_HYPERV */

#endif /* __ARCH_X86_KVM_SVM_ONHYPERV_H__ */

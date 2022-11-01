/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common Hyper-V on KVM and KVM on Hyper-V definitions (SVM).
 */

#ifndef __ARCH_X86_KVM_SVM_HYPERV_H__
#define __ARCH_X86_KVM_SVM_HYPERV_H__

#include <asm/mshyperv.h>

#include "../hyperv.h"
#include "svm.h"

static inline void nested_svm_hv_update_vm_vp_ids(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct hv_vmcb_enlightenments *hve = &svm->nested.ctl.hv_enlightenments;
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	if (!hv_vcpu)
		return;

	hv_vcpu->nested.pa_page_gpa = hve->partition_assist_page;
	hv_vcpu->nested.vm_id = hve->hv_vm_id;
	hv_vcpu->nested.vp_id = hve->hv_vp_id;
}

void svm_hv_inject_synthetic_vmexit_post_tlb_flush(struct kvm_vcpu *vcpu);

#endif /* __ARCH_X86_KVM_SVM_HYPERV_H__ */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM L1 hypervisor optimizations on Hyper-V for SVM.
 */

#ifndef __ARCH_X86_KVM_SVM_ONHYPERV_H__
#define __ARCH_X86_KVM_SVM_ONHYPERV_H__

#if IS_ENABLED(CONFIG_HYPERV)

#include "kvm_onhyperv.h"
#include "svm/hyperv.h"

static struct kvm_x86_ops svm_x86_ops;

int svm_hv_enable_direct_tlbflush(struct kvm_vcpu *vcpu);

static inline void svm_hv_init_vmcb(struct vmcb *vmcb)
{
	struct hv_enlightenments *hve =
		(struct hv_enlightenments *)vmcb->control.reserved_sw;

	if (npt_enabled &&
	    ms_hyperv.nested_features & HV_X64_NESTED_ENLIGHTENED_TLB)
		hve->hv_enlightenments_control.enlightened_npt_tlb = 1;

	if (ms_hyperv.nested_features & HV_X64_NESTED_MSR_BITMAP)
		hve->hv_enlightenments_control.msr_bitmap = 1;
}

static inline __init void svm_hv_hardware_setup(void)
{
	if (npt_enabled &&
	    ms_hyperv.nested_features & HV_X64_NESTED_ENLIGHTENED_TLB) {
		pr_info("kvm: Hyper-V enlightened NPT TLB flush enabled\n");
		svm_x86_ops.tlb_remote_flush = hv_remote_flush_tlb;
		svm_x86_ops.tlb_remote_flush_with_range =
				hv_remote_flush_tlb_with_range;
	}

	if (ms_hyperv.nested_features & HV_X64_NESTED_DIRECT_FLUSH) {
		int cpu;

		pr_info("kvm: Hyper-V Direct TLB Flush enabled\n");
		for_each_online_cpu(cpu) {
			struct hv_vp_assist_page *vp_ap =
				hv_get_vp_assist_page(cpu);

			if (!vp_ap)
				continue;

			vp_ap->nested_control.features.directhypercall = 1;
		}
		svm_x86_ops.enable_direct_tlbflush =
				svm_hv_enable_direct_tlbflush;
	}
}

static inline void svm_hv_vmcb_dirty_nested_enlightenments(
		struct kvm_vcpu *vcpu)
{
	struct vmcb *vmcb = to_svm(vcpu)->vmcb;
	struct hv_enlightenments *hve =
		(struct hv_enlightenments *)vmcb->control.reserved_sw;

	if (hve->hv_enlightenments_control.msr_bitmap)
		vmcb_mark_dirty(vmcb, VMCB_HV_NESTED_ENLIGHTENMENTS);
}

static inline void svm_hv_update_vp_id(struct vmcb *vmcb,
		struct kvm_vcpu *vcpu)
{
	struct hv_enlightenments *hve =
		(struct hv_enlightenments *)vmcb->control.reserved_sw;
	u32 vp_index = kvm_hv_get_vpindex(vcpu);

	if (hve->hv_vp_id != vp_index) {
		hve->hv_vp_id = vp_index;
		vmcb_mark_dirty(vmcb, VMCB_HV_NESTED_ENLIGHTENMENTS);
	}
}
#else

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

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM L1 hypervisor optimizations on Hyper-V for SVM.
 */

#ifndef __ARCH_X86_KVM_SVM_ONHYPERV_H__
#define __ARCH_X86_KVM_SVM_ONHYPERV_H__

#if IS_ENABLED(CONFIG_HYPERV)
#include <asm/mshyperv.h>

#include "hyperv.h"
#include "kvm_onhyperv.h"

static struct kvm_x86_ops svm_x86_ops;

/*
 * Hyper-V uses the software reserved 32 bytes in VMCB
 * control area to expose SVM enlightenments to guests.
 */
struct hv_enlightenments {
	struct __packed hv_enlightenments_control {
		u32 nested_flush_hypercall:1;
		u32 msr_bitmap:1;
		u32 enlightened_npt_tlb: 1;
		u32 reserved:29;
	} __packed hv_enlightenments_control;
	u32 hv_vp_id;
	u64 hv_vm_id;
	u64 partition_assist_page;
	u64 reserved;
} __packed;

static inline void svm_hv_init_vmcb(struct vmcb *vmcb)
{
	struct hv_enlightenments *hve =
		(struct hv_enlightenments *)vmcb->control.reserved_sw;

	if (npt_enabled &&
	    ms_hyperv.nested_features & HV_X64_NESTED_ENLIGHTENED_TLB)
		hve->hv_enlightenments_control.enlightened_npt_tlb = 1;
}

static inline void svm_hv_hardware_setup(void)
{
	if (npt_enabled &&
	    ms_hyperv.nested_features & HV_X64_NESTED_ENLIGHTENED_TLB) {
		pr_info("kvm: Hyper-V enlightened NPT TLB flush enabled\n");
		svm_x86_ops.tlb_remote_flush = hv_remote_flush_tlb;
		svm_x86_ops.tlb_remote_flush_with_range =
				hv_remote_flush_tlb_with_range;
	}
}

#else

static inline void svm_hv_init_vmcb(struct vmcb *vmcb)
{
}

static inline void svm_hv_hardware_setup(void)
{
}
#endif /* CONFIG_HYPERV */

#endif /* __ARCH_X86_KVM_SVM_ONHYPERV_H__ */

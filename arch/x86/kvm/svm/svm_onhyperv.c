// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM L1 hypervisor optimizations on Hyper-V for SVM.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_host.h>

#include <asm/mshyperv.h>

#include "svm.h"
#include "svm_ops.h"

#include "hyperv.h"
#include "kvm_onhyperv.h"
#include "svm_onhyperv.h"

static int svm_hv_enable_l2_tlb_flush(struct kvm_vcpu *vcpu)
{
	struct hv_vmcb_enlightenments *hve;
	hpa_t partition_assist_page = hv_get_partition_assist_page(vcpu);

	if (partition_assist_page == INVALID_PAGE)
		return -ENOMEM;

	hve = &to_svm(vcpu)->vmcb->control.hv_enlightenments;

	hve->partition_assist_page = partition_assist_page;
	hve->hv_vm_id = (unsigned long)vcpu->kvm;
	if (!hve->hv_enlightenments_control.nested_flush_hypercall) {
		hve->hv_enlightenments_control.nested_flush_hypercall = 1;
		vmcb_mark_dirty(to_svm(vcpu)->vmcb, HV_VMCB_NESTED_ENLIGHTENMENTS);
	}

	return 0;
}

__init void svm_hv_hardware_setup(void)
{
	if (npt_enabled &&
	    ms_hyperv.nested_features & HV_X64_NESTED_ENLIGHTENED_TLB) {
		pr_info(KBUILD_MODNAME ": Hyper-V enlightened NPT TLB flush enabled\n");
		svm_x86_ops.flush_remote_tlbs = hv_flush_remote_tlbs;
		svm_x86_ops.flush_remote_tlbs_range = hv_flush_remote_tlbs_range;
	}

	if (ms_hyperv.nested_features & HV_X64_NESTED_DIRECT_FLUSH) {
		int cpu;

		pr_info(KBUILD_MODNAME ": Hyper-V Direct TLB Flush enabled\n");
		for_each_online_cpu(cpu) {
			struct hv_vp_assist_page *vp_ap =
				hv_get_vp_assist_page(cpu);

			if (!vp_ap)
				continue;

			vp_ap->nested_control.features.directhypercall = 1;
		}
		svm_x86_ops.enable_l2_tlb_flush =
				svm_hv_enable_l2_tlb_flush;
	}
}

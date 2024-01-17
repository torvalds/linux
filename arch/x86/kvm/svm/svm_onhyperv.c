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

int svm_hv_enable_l2_tlb_flush(struct kvm_vcpu *vcpu)
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


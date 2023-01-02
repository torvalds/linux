// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD SVM specific code for Hyper-V on KVM.
 *
 * Copyright 2022 Red Hat, Inc. and/or its affiliates.
 */
#include "hyperv.h"

void svm_hv_inject_synthetic_vmexit_post_tlb_flush(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->control.exit_code = HV_SVM_EXITCODE_ENL;
	svm->vmcb->control.exit_code_hi = 0;
	svm->vmcb->control.exit_info_1 = HV_SVM_ENL_EXITCODE_TRAP_AFTER_FLUSH;
	svm->vmcb->control.exit_info_2 = 0;
	nested_svm_vmexit(svm);
}

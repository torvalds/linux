// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD SVM specific code for Hyper-V on KVM.
 *
 * Copyright 2022 Red Hat, Inc. and/or its affiliates.
 */
#include "hyperv.h"

void svm_hv_inject_synthetic_vmexit_post_tlb_flush(struct kvm_vcpu *vcpu)
{
}

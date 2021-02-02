// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * KVM Xen emulation
 */

#ifndef __ARCH_X86_KVM_XEN_H__
#define __ARCH_X86_KVM_XEN_H__

int kvm_xen_hypercall(struct kvm_vcpu *vcpu);
int kvm_xen_write_hypercall_page(struct kvm_vcpu *vcpu, u64 data);
int kvm_xen_hvm_config(struct kvm *kvm, struct kvm_xen_hvm_config *xhc);

static inline bool kvm_xen_hypercall_enabled(struct kvm *kvm)
{
	return kvm->arch.xen_hvm_config.flags &
		KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL;
}

#endif /* __ARCH_X86_KVM_XEN_H__ */

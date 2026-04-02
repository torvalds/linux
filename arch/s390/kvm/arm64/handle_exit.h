/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KVM_ARM64_HANDLE_EXIT_H
#define KVM_ARM64_HANDLE_EXIT_H

#include <linux/kvm_host.h>

int handle_exit(struct kvm_vcpu *vcpu);

#endif /* KVM_ARM64_HANDLE_EXIT_H */

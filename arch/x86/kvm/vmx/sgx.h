/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_SGX_H
#define __KVM_X86_SGX_H

#include <linux/kvm_host.h>

#ifdef CONFIG_X86_SGX_KVM
extern bool __read_mostly enable_sgx;

int handle_encls(struct kvm_vcpu *vcpu);

void setup_default_sgx_lepubkeyhash(void);
void vcpu_setup_sgx_lepubkeyhash(struct kvm_vcpu *vcpu);
#else
#define enable_sgx 0

static inline void setup_default_sgx_lepubkeyhash(void) { }
static inline void vcpu_setup_sgx_lepubkeyhash(struct kvm_vcpu *vcpu) { }
#endif

#endif /* __KVM_X86_SGX_H */

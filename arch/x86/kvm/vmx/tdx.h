/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_TDX_H
#define __KVM_X86_VMX_TDX_H

#include "tdx_arch.h"

#ifdef CONFIG_KVM_INTEL_TDX
int tdx_bringup(void);
void tdx_cleanup(void);

extern bool enable_tdx;

struct kvm_tdx {
	struct kvm kvm;
	/* TDX specific members follow. */
};

struct vcpu_tdx {
	struct kvm_vcpu	vcpu;
	/* TDX specific members follow. */
};

static inline bool is_td(struct kvm *kvm)
{
	return kvm->arch.vm_type == KVM_X86_TDX_VM;
}

static inline bool is_td_vcpu(struct kvm_vcpu *vcpu)
{
	return is_td(vcpu->kvm);
}

#else
static inline int tdx_bringup(void) { return 0; }
static inline void tdx_cleanup(void) {}

#define enable_tdx	0

struct kvm_tdx {
	struct kvm kvm;
};

struct vcpu_tdx {
	struct kvm_vcpu	vcpu;
};

static inline bool is_td(struct kvm *kvm) { return false; }
static inline bool is_td_vcpu(struct kvm_vcpu *vcpu) { return false; }

#endif

#endif

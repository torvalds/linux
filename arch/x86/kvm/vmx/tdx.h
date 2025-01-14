/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_TDX_H
#define __KVM_X86_VMX_TDX_H

#include "tdx_arch.h"
#include "tdx_errno.h"

#ifdef CONFIG_KVM_INTEL_TDX
int tdx_bringup(void);
void tdx_cleanup(void);

extern bool enable_tdx;

/* TDX module hardware states. These follow the TDX module OP_STATEs. */
enum kvm_tdx_state {
	TD_STATE_UNINITIALIZED = 0,
	TD_STATE_INITIALIZED,
	TD_STATE_RUNNABLE,
};

struct kvm_tdx {
	struct kvm kvm;

	int hkid;
	enum kvm_tdx_state state;

	u64 attributes;
	u64 xfam;

	u64 tsc_offset;
	u64 tsc_multiplier;

	struct tdx_td td;
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

static __always_inline u64 td_tdcs_exec_read64(struct kvm_tdx *kvm_tdx, u32 field)
{
	u64 err, data;

	err = tdh_mng_rd(&kvm_tdx->td, TDCS_EXEC(field), &data);
	if (unlikely(err)) {
		pr_err("TDH_MNG_RD[EXEC.0x%x] failed: 0x%llx\n", field, err);
		return 0;
	}
	return data;
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

// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/smp.h>

#include "x86.h"
#include "../cpuid.h"
#include "hyperv.h"
#include "nested.h"
#include "vmcs.h"
#include "vmx.h"
#include "trace.h"

#define CC KVM_NESTED_VMENTER_CONSISTENCY_CHECK

u64 nested_get_evmptr(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	if (unlikely(kvm_hv_get_assist_page(vcpu)))
		return EVMPTR_INVALID;

	if (unlikely(!hv_vcpu->vp_assist_page.enlighten_vmentry))
		return EVMPTR_INVALID;

	return hv_vcpu->vp_assist_page.current_nested_vmcs;
}

uint16_t nested_get_evmcs_version(struct kvm_vcpu *vcpu)
{
	/*
	 * vmcs_version represents the range of supported Enlightened VMCS
	 * versions: lower 8 bits is the minimal version, higher 8 bits is the
	 * maximum supported version. KVM supports versions from 1 to
	 * KVM_EVMCS_VERSION.
	 *
	 * Note, do not check the Hyper-V is fully enabled in guest CPUID, this
	 * helper is used to _get_ the vCPU's supported CPUID.
	 */
	if (kvm_cpu_cap_get(X86_FEATURE_VMX) &&
	    (!vcpu || to_vmx(vcpu)->nested.enlightened_vmcs_enabled))
		return (KVM_EVMCS_VERSION << 8) | 1;

	return 0;
}

enum evmcs_revision {
	EVMCSv1_LEGACY,
	NR_EVMCS_REVISIONS,
};

enum evmcs_ctrl_type {
	EVMCS_EXIT_CTRLS,
	EVMCS_ENTRY_CTRLS,
	EVMCS_EXEC_CTRL,
	EVMCS_2NDEXEC,
	EVMCS_3RDEXEC,
	EVMCS_PINCTRL,
	EVMCS_VMFUNC,
	NR_EVMCS_CTRLS,
};

static const u32 evmcs_supported_ctrls[NR_EVMCS_CTRLS][NR_EVMCS_REVISIONS] = {
	[EVMCS_EXIT_CTRLS] = {
		[EVMCSv1_LEGACY] = EVMCS1_SUPPORTED_VMEXIT_CTRL,
	},
	[EVMCS_ENTRY_CTRLS] = {
		[EVMCSv1_LEGACY] = EVMCS1_SUPPORTED_VMENTRY_CTRL,
	},
	[EVMCS_EXEC_CTRL] = {
		[EVMCSv1_LEGACY] = EVMCS1_SUPPORTED_EXEC_CTRL,
	},
	[EVMCS_2NDEXEC] = {
		[EVMCSv1_LEGACY] = EVMCS1_SUPPORTED_2NDEXEC & ~SECONDARY_EXEC_TSC_SCALING,
	},
	[EVMCS_3RDEXEC] = {
		[EVMCSv1_LEGACY] = EVMCS1_SUPPORTED_3RDEXEC,
	},
	[EVMCS_PINCTRL] = {
		[EVMCSv1_LEGACY] = EVMCS1_SUPPORTED_PINCTRL,
	},
	[EVMCS_VMFUNC] = {
		[EVMCSv1_LEGACY] = EVMCS1_SUPPORTED_VMFUNC,
	},
};

static u32 evmcs_get_supported_ctls(enum evmcs_ctrl_type ctrl_type)
{
	enum evmcs_revision evmcs_rev = EVMCSv1_LEGACY;

	return evmcs_supported_ctrls[ctrl_type][evmcs_rev];
}

static bool evmcs_has_perf_global_ctrl(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	/*
	 * PERF_GLOBAL_CTRL has a quirk where some Windows guests may fail to
	 * boot if a PV CPUID feature flag is not also set.  Treat the fields
	 * as unsupported if the flag is not set in guest CPUID.  This should
	 * be called only for guest accesses, and all guest accesses should be
	 * gated on Hyper-V being enabled and initialized.
	 */
	if (WARN_ON_ONCE(!hv_vcpu))
		return false;

	return hv_vcpu->cpuid_cache.nested_ebx & HV_X64_NESTED_EVMCS1_PERF_GLOBAL_CTRL;
}

void nested_evmcs_filter_control_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata)
{
	u32 ctl_low = (u32)*pdata;
	u32 ctl_high = (u32)(*pdata >> 32);
	u32 supported_ctrls;

	/*
	 * Hyper-V 2016 and 2019 try using these features even when eVMCS
	 * is enabled but there are no corresponding fields.
	 */
	switch (msr_index) {
	case MSR_IA32_VMX_EXIT_CTLS:
	case MSR_IA32_VMX_TRUE_EXIT_CTLS:
		supported_ctrls = evmcs_get_supported_ctls(EVMCS_EXIT_CTRLS);
		if (!evmcs_has_perf_global_ctrl(vcpu))
			supported_ctrls &= ~VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL;
		ctl_high &= supported_ctrls;
		break;
	case MSR_IA32_VMX_ENTRY_CTLS:
	case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
		supported_ctrls = evmcs_get_supported_ctls(EVMCS_ENTRY_CTRLS);
		if (!evmcs_has_perf_global_ctrl(vcpu))
			supported_ctrls &= ~VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL;
		ctl_high &= supported_ctrls;
		break;
	case MSR_IA32_VMX_PROCBASED_CTLS:
	case MSR_IA32_VMX_TRUE_PROCBASED_CTLS:
		ctl_high &= evmcs_get_supported_ctls(EVMCS_EXEC_CTRL);
		break;
	case MSR_IA32_VMX_PROCBASED_CTLS2:
		ctl_high &= evmcs_get_supported_ctls(EVMCS_2NDEXEC);
		break;
	case MSR_IA32_VMX_TRUE_PINBASED_CTLS:
	case MSR_IA32_VMX_PINBASED_CTLS:
		ctl_high &= evmcs_get_supported_ctls(EVMCS_PINCTRL);
		break;
	case MSR_IA32_VMX_VMFUNC:
		ctl_low &= evmcs_get_supported_ctls(EVMCS_VMFUNC);
		break;
	}

	*pdata = ctl_low | ((u64)ctl_high << 32);
}

static bool nested_evmcs_is_valid_controls(enum evmcs_ctrl_type ctrl_type,
					   u32 val)
{
	return !(val & ~evmcs_get_supported_ctls(ctrl_type));
}

int nested_evmcs_check_controls(struct vmcs12 *vmcs12)
{
	if (CC(!nested_evmcs_is_valid_controls(EVMCS_PINCTRL,
					       vmcs12->pin_based_vm_exec_control)))
		return -EINVAL;

	if (CC(!nested_evmcs_is_valid_controls(EVMCS_EXEC_CTRL,
					       vmcs12->cpu_based_vm_exec_control)))
		return -EINVAL;

	if (CC(!nested_evmcs_is_valid_controls(EVMCS_2NDEXEC,
					       vmcs12->secondary_vm_exec_control)))
		return -EINVAL;

	if (CC(!nested_evmcs_is_valid_controls(EVMCS_EXIT_CTRLS,
					       vmcs12->vm_exit_controls)))
		return -EINVAL;

	if (CC(!nested_evmcs_is_valid_controls(EVMCS_ENTRY_CTRLS,
					       vmcs12->vm_entry_controls)))
		return -EINVAL;

	/*
	 * VM-Func controls are 64-bit, but KVM currently doesn't support any
	 * controls in bits 63:32, i.e. dropping those bits on the consistency
	 * check is intentional.
	 */
	if (WARN_ON_ONCE(vmcs12->vm_function_control >> 32))
		return -EINVAL;

	if (CC(!nested_evmcs_is_valid_controls(EVMCS_VMFUNC,
					       vmcs12->vm_function_control)))
		return -EINVAL;

	return 0;
}

int nested_enable_evmcs(struct kvm_vcpu *vcpu,
			uint16_t *vmcs_version)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	vmx->nested.enlightened_vmcs_enabled = true;

	if (vmcs_version)
		*vmcs_version = nested_get_evmcs_version(vcpu);

	return 0;
}

bool nested_evmcs_l2_tlb_flush_enabled(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct hv_enlightened_vmcs *evmcs = vmx->nested.hv_evmcs;

	if (!hv_vcpu || !evmcs)
		return false;

	if (!evmcs->hv_enlightenments_control.nested_flush_hypercall)
		return false;

	return hv_vcpu->vp_assist_page.nested_control.features.directhypercall;
}

void vmx_hv_inject_synthetic_vmexit_post_tlb_flush(struct kvm_vcpu *vcpu)
{
	nested_vmx_vmexit(vcpu, HV_VMX_SYNTHETIC_EXIT_REASON_TRAP_AFTER_FLUSH, 0, 0);
}

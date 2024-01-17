// SPDX-License-Identifier: GPL-2.0-only

#include "capabilities.h"
#include "vmx_onhyperv.h"

DEFINE_STATIC_KEY_FALSE(__kvm_is_using_evmcs);

/*
 * KVM on Hyper-V always uses the latest known eVMCSv1 revision, the assumption
 * is: in case a feature has corresponding fields in eVMCS described and it was
 * exposed in VMX feature MSRs, KVM is free to use it. Warn if KVM meets a
 * feature which has no corresponding eVMCS field, this likely means that KVM
 * needs to be updated.
 */
#define evmcs_check_vmcs_conf(field, ctrl)					\
	do {									\
		typeof(vmcs_conf->field) unsupported;				\
										\
		unsupported = vmcs_conf->field & ~EVMCS1_SUPPORTED_ ## ctrl;	\
		if (unsupported) {						\
			pr_warn_once(#field " unsupported with eVMCS: 0x%llx\n",\
				     (u64)unsupported);				\
			vmcs_conf->field &= EVMCS1_SUPPORTED_ ## ctrl;		\
		}								\
	}									\
	while (0)

void evmcs_sanitize_exec_ctrls(struct vmcs_config *vmcs_conf)
{
	evmcs_check_vmcs_conf(cpu_based_exec_ctrl, EXEC_CTRL);
	evmcs_check_vmcs_conf(pin_based_exec_ctrl, PINCTRL);
	evmcs_check_vmcs_conf(cpu_based_2nd_exec_ctrl, 2NDEXEC);
	evmcs_check_vmcs_conf(cpu_based_3rd_exec_ctrl, 3RDEXEC);
	evmcs_check_vmcs_conf(vmentry_ctrl, VMENTRY_CTRL);
	evmcs_check_vmcs_conf(vmexit_ctrl, VMEXIT_CTRL);
}

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_CAPS_H
#define __KVM_X86_VMX_CAPS_H

#include <asm/vmx.h>

#include "lapic.h"

extern bool __read_mostly enable_vpid;
extern bool __read_mostly flexpriority_enabled;
extern bool __read_mostly enable_ept;
extern bool __read_mostly enable_unrestricted_guest;
extern bool __read_mostly enable_ept_ad_bits;
extern bool __read_mostly enable_pml;
extern int __read_mostly pt_mode;

#define PT_MODE_SYSTEM		0
#define PT_MODE_HOST_GUEST	1

struct nested_vmx_msrs {
	/*
	 * We only store the "true" versions of the VMX capability MSRs. We
	 * generate the "non-true" versions by setting the must-be-1 bits
	 * according to the SDM.
	 */
	u32 procbased_ctls_low;
	u32 procbased_ctls_high;
	u32 secondary_ctls_low;
	u32 secondary_ctls_high;
	u32 pinbased_ctls_low;
	u32 pinbased_ctls_high;
	u32 exit_ctls_low;
	u32 exit_ctls_high;
	u32 entry_ctls_low;
	u32 entry_ctls_high;
	u32 misc_low;
	u32 misc_high;
	u32 ept_caps;
	u32 vpid_caps;
	u64 basic;
	u64 cr0_fixed0;
	u64 cr0_fixed1;
	u64 cr4_fixed0;
	u64 cr4_fixed1;
	u64 vmcs_enum;
	u64 vmfunc_controls;
};

struct vmcs_config {
	int size;
	int order;
	u32 basic_cap;
	u32 revision_id;
	u32 pin_based_exec_ctrl;
	u32 cpu_based_exec_ctrl;
	u32 cpu_based_2nd_exec_ctrl;
	u32 vmexit_ctrl;
	u32 vmentry_ctrl;
	struct nested_vmx_msrs nested;
};
extern struct vmcs_config vmcs_config;

struct vmx_capability {
	u32 ept;
	u32 vpid;
};
extern struct vmx_capability vmx_capability;

static inline bool cpu_has_vmx_basic_inout(void)
{
	return	(((u64)vmcs_config.basic_cap << 32) & VMX_BASIC_INOUT);
}

static inline bool cpu_has_virtual_nmis(void)
{
	return vmcs_config.pin_based_exec_ctrl & PIN_BASED_VIRTUAL_NMIS;
}

static inline bool cpu_has_vmx_preemption_timer(void)
{
	return vmcs_config.pin_based_exec_ctrl &
		PIN_BASED_VMX_PREEMPTION_TIMER;
}

static inline bool cpu_has_vmx_posted_intr(void)
{
	return IS_ENABLED(CONFIG_X86_LOCAL_APIC) &&
		vmcs_config.pin_based_exec_ctrl & PIN_BASED_POSTED_INTR;
}

static inline bool cpu_has_load_ia32_efer(void)
{
	return (vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_IA32_EFER) &&
	       (vmcs_config.vmexit_ctrl & VM_EXIT_LOAD_IA32_EFER);
}

static inline bool cpu_has_load_perf_global_ctrl(void)
{
	return (vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL) &&
	       (vmcs_config.vmexit_ctrl & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL);
}

static inline bool vmx_mpx_supported(void)
{
	return (vmcs_config.vmexit_ctrl & VM_EXIT_CLEAR_BNDCFGS) &&
		(vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_BNDCFGS);
}

static inline bool cpu_has_vmx_tpr_shadow(void)
{
	return vmcs_config.cpu_based_exec_ctrl & CPU_BASED_TPR_SHADOW;
}

static inline bool cpu_need_tpr_shadow(struct kvm_vcpu *vcpu)
{
	return cpu_has_vmx_tpr_shadow() && lapic_in_kernel(vcpu);
}

static inline bool cpu_has_vmx_msr_bitmap(void)
{
	return vmcs_config.cpu_based_exec_ctrl & CPU_BASED_USE_MSR_BITMAPS;
}

static inline bool cpu_has_secondary_exec_ctrls(void)
{
	return vmcs_config.cpu_based_exec_ctrl &
		CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
}

static inline bool cpu_has_vmx_virtualize_apic_accesses(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES;
}

static inline bool cpu_has_vmx_ept(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_EPT;
}

static inline bool vmx_umip_emulated(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_DESC;
}

static inline bool cpu_has_vmx_rdtscp(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_RDTSCP;
}

static inline bool cpu_has_vmx_virtualize_x2apic_mode(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE;
}

static inline bool cpu_has_vmx_vpid(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_VPID;
}

static inline bool cpu_has_vmx_wbinvd_exit(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_WBINVD_EXITING;
}

static inline bool cpu_has_vmx_unrestricted_guest(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_UNRESTRICTED_GUEST;
}

static inline bool cpu_has_vmx_apic_register_virt(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_APIC_REGISTER_VIRT;
}

static inline bool cpu_has_vmx_virtual_intr_delivery(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY;
}

static inline bool cpu_has_vmx_ple(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_PAUSE_LOOP_EXITING;
}

static inline bool vmx_rdrand_supported(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_RDRAND_EXITING;
}

static inline bool cpu_has_vmx_invpcid(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_INVPCID;
}

static inline bool cpu_has_vmx_vmfunc(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_VMFUNC;
}

static inline bool cpu_has_vmx_shadow_vmcs(void)
{
	u64 vmx_msr;

	/* check if the cpu supports writing r/o exit information fields */
	rdmsrl(MSR_IA32_VMX_MISC, vmx_msr);
	if (!(vmx_msr & MSR_IA32_VMX_MISC_VMWRITE_SHADOW_RO_FIELDS))
		return false;

	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_SHADOW_VMCS;
}

static inline bool cpu_has_vmx_encls_vmexit(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENCLS_EXITING;
}

static inline bool vmx_rdseed_supported(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_RDSEED_EXITING;
}

static inline bool cpu_has_vmx_pml(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl & SECONDARY_EXEC_ENABLE_PML;
}

static inline bool vmx_xsaves_supported(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_XSAVES;
}

static inline bool vmx_waitpkg_supported(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE;
}

static inline bool cpu_has_vmx_tsc_scaling(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_TSC_SCALING;
}

static inline bool cpu_has_vmx_apicv(void)
{
	return cpu_has_vmx_apic_register_virt() &&
		cpu_has_vmx_virtual_intr_delivery() &&
		cpu_has_vmx_posted_intr();
}

static inline bool cpu_has_vmx_flexpriority(void)
{
	return cpu_has_vmx_tpr_shadow() &&
		cpu_has_vmx_virtualize_apic_accesses();
}

static inline bool cpu_has_vmx_ept_execute_only(void)
{
	return vmx_capability.ept & VMX_EPT_EXECUTE_ONLY_BIT;
}

static inline bool cpu_has_vmx_ept_4levels(void)
{
	return vmx_capability.ept & VMX_EPT_PAGE_WALK_4_BIT;
}

static inline bool cpu_has_vmx_ept_5levels(void)
{
	return vmx_capability.ept & VMX_EPT_PAGE_WALK_5_BIT;
}

static inline bool cpu_has_vmx_ept_mt_wb(void)
{
	return vmx_capability.ept & VMX_EPTP_WB_BIT;
}

static inline bool cpu_has_vmx_ept_2m_page(void)
{
	return vmx_capability.ept & VMX_EPT_2MB_PAGE_BIT;
}

static inline bool cpu_has_vmx_ept_1g_page(void)
{
	return vmx_capability.ept & VMX_EPT_1GB_PAGE_BIT;
}

static inline bool cpu_has_vmx_ept_ad_bits(void)
{
	return vmx_capability.ept & VMX_EPT_AD_BIT;
}

static inline bool cpu_has_vmx_invept_context(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_invept_global(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_GLOBAL_BIT;
}

static inline bool cpu_has_vmx_invvpid(void)
{
	return vmx_capability.vpid & VMX_VPID_INVVPID_BIT;
}

static inline bool cpu_has_vmx_invvpid_individual_addr(void)
{
	return vmx_capability.vpid & VMX_VPID_EXTENT_INDIVIDUAL_ADDR_BIT;
}

static inline bool cpu_has_vmx_invvpid_single(void)
{
	return vmx_capability.vpid & VMX_VPID_EXTENT_SINGLE_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_invvpid_global(void)
{
	return vmx_capability.vpid & VMX_VPID_EXTENT_GLOBAL_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_intel_pt(void)
{
	u64 vmx_msr;

	rdmsrl(MSR_IA32_VMX_MISC, vmx_msr);
	return (vmx_msr & MSR_IA32_VMX_MISC_INTEL_PT) &&
		(vmcs_config.cpu_based_2nd_exec_ctrl & SECONDARY_EXEC_PT_USE_GPA) &&
		(vmcs_config.vmexit_ctrl & VM_EXIT_CLEAR_IA32_RTIT_CTL) &&
		(vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_IA32_RTIT_CTL);
}

#endif /* __KVM_X86_VMX_CAPS_H */

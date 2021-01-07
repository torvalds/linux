// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/smp.h>

#include "../hyperv.h"
#include "../cpuid.h"
#include "evmcs.h"
#include "vmcs.h"
#include "vmx.h"
#include "trace.h"

DEFINE_STATIC_KEY_FALSE(enable_evmcs);

#if IS_ENABLED(CONFIG_HYPERV)

#define ROL16(val, n) ((u16)(((u16)(val) << (n)) | ((u16)(val) >> (16 - (n)))))
#define EVMCS1_OFFSET(x) offsetof(struct hv_enlightened_vmcs, x)
#define EVMCS1_FIELD(number, name, clean_field)[ROL16(number, 6)] = \
		{EVMCS1_OFFSET(name), clean_field}

const struct evmcs_field vmcs_field_to_evmcs_1[] = {
	/* 64 bit rw */
	EVMCS1_FIELD(GUEST_RIP, guest_rip,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(GUEST_RSP, guest_rsp,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_BASIC),
	EVMCS1_FIELD(GUEST_RFLAGS, guest_rflags,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_BASIC),
	EVMCS1_FIELD(HOST_IA32_PAT, host_ia32_pat,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_IA32_EFER, host_ia32_efer,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_CR0, host_cr0,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_CR3, host_cr3,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_CR4, host_cr4,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_IA32_SYSENTER_ESP, host_ia32_sysenter_esp,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_IA32_SYSENTER_EIP, host_ia32_sysenter_eip,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_RIP, host_rip,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(IO_BITMAP_A, io_bitmap_a,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_IO_BITMAP),
	EVMCS1_FIELD(IO_BITMAP_B, io_bitmap_b,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_IO_BITMAP),
	EVMCS1_FIELD(MSR_BITMAP, msr_bitmap,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_MSR_BITMAP),
	EVMCS1_FIELD(GUEST_ES_BASE, guest_es_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_CS_BASE, guest_cs_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_SS_BASE, guest_ss_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_DS_BASE, guest_ds_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_FS_BASE, guest_fs_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_GS_BASE, guest_gs_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_LDTR_BASE, guest_ldtr_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_TR_BASE, guest_tr_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_GDTR_BASE, guest_gdtr_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_IDTR_BASE, guest_idtr_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(TSC_OFFSET, tsc_offset,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP2),
	EVMCS1_FIELD(VIRTUAL_APIC_PAGE_ADDR, virtual_apic_page_addr,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP2),
	EVMCS1_FIELD(VMCS_LINK_POINTER, vmcs_link_pointer,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_IA32_DEBUGCTL, guest_ia32_debugctl,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_IA32_PAT, guest_ia32_pat,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_IA32_EFER, guest_ia32_efer,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_PDPTR0, guest_pdptr0,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_PDPTR1, guest_pdptr1,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_PDPTR2, guest_pdptr2,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_PDPTR3, guest_pdptr3,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_PENDING_DBG_EXCEPTIONS, guest_pending_dbg_exceptions,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_SYSENTER_ESP, guest_sysenter_esp,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_SYSENTER_EIP, guest_sysenter_eip,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(CR0_GUEST_HOST_MASK, cr0_guest_host_mask,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(CR4_GUEST_HOST_MASK, cr4_guest_host_mask,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(CR0_READ_SHADOW, cr0_read_shadow,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(CR4_READ_SHADOW, cr4_read_shadow,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(GUEST_CR0, guest_cr0,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(GUEST_CR3, guest_cr3,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(GUEST_CR4, guest_cr4,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(GUEST_DR7, guest_dr7,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR),
	EVMCS1_FIELD(HOST_FS_BASE, host_fs_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER),
	EVMCS1_FIELD(HOST_GS_BASE, host_gs_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER),
	EVMCS1_FIELD(HOST_TR_BASE, host_tr_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER),
	EVMCS1_FIELD(HOST_GDTR_BASE, host_gdtr_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER),
	EVMCS1_FIELD(HOST_IDTR_BASE, host_idtr_base,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER),
	EVMCS1_FIELD(HOST_RSP, host_rsp,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER),
	EVMCS1_FIELD(EPT_POINTER, ept_pointer,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_XLAT),
	EVMCS1_FIELD(GUEST_BNDCFGS, guest_bndcfgs,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(XSS_EXIT_BITMAP, xss_exit_bitmap,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP2),

	/* 64 bit read only */
	EVMCS1_FIELD(GUEST_PHYSICAL_ADDRESS, guest_physical_address,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(EXIT_QUALIFICATION, exit_qualification,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	/*
	 * Not defined in KVM:
	 *
	 * EVMCS1_FIELD(0x00006402, exit_io_instruction_ecx,
	 *		HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE);
	 * EVMCS1_FIELD(0x00006404, exit_io_instruction_esi,
	 *		HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE);
	 * EVMCS1_FIELD(0x00006406, exit_io_instruction_esi,
	 *		HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE);
	 * EVMCS1_FIELD(0x00006408, exit_io_instruction_eip,
	 *		HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE);
	 */
	EVMCS1_FIELD(GUEST_LINEAR_ADDRESS, guest_linear_address,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),

	/*
	 * No mask defined in the spec as Hyper-V doesn't currently support
	 * these. Future proof by resetting the whole clean field mask on
	 * access.
	 */
	EVMCS1_FIELD(VM_EXIT_MSR_STORE_ADDR, vm_exit_msr_store_addr,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),
	EVMCS1_FIELD(VM_EXIT_MSR_LOAD_ADDR, vm_exit_msr_load_addr,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),
	EVMCS1_FIELD(VM_ENTRY_MSR_LOAD_ADDR, vm_entry_msr_load_addr,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),

	/* 32 bit rw */
	EVMCS1_FIELD(TPR_THRESHOLD, tpr_threshold,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(GUEST_INTERRUPTIBILITY_INFO, guest_interruptibility_info,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_BASIC),
	EVMCS1_FIELD(CPU_BASED_VM_EXEC_CONTROL, cpu_based_vm_exec_control,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_PROC),
	EVMCS1_FIELD(EXCEPTION_BITMAP, exception_bitmap,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EXCPN),
	EVMCS1_FIELD(VM_ENTRY_CONTROLS, vm_entry_controls,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_ENTRY),
	EVMCS1_FIELD(VM_ENTRY_INTR_INFO_FIELD, vm_entry_intr_info_field,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EVENT),
	EVMCS1_FIELD(VM_ENTRY_EXCEPTION_ERROR_CODE,
		     vm_entry_exception_error_code,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EVENT),
	EVMCS1_FIELD(VM_ENTRY_INSTRUCTION_LEN, vm_entry_instruction_len,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EVENT),
	EVMCS1_FIELD(HOST_IA32_SYSENTER_CS, host_ia32_sysenter_cs,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(PIN_BASED_VM_EXEC_CONTROL, pin_based_vm_exec_control,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP1),
	EVMCS1_FIELD(VM_EXIT_CONTROLS, vm_exit_controls,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP1),
	EVMCS1_FIELD(SECONDARY_VM_EXEC_CONTROL, secondary_vm_exec_control,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP1),
	EVMCS1_FIELD(GUEST_ES_LIMIT, guest_es_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_CS_LIMIT, guest_cs_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_SS_LIMIT, guest_ss_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_DS_LIMIT, guest_ds_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_FS_LIMIT, guest_fs_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_GS_LIMIT, guest_gs_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_LDTR_LIMIT, guest_ldtr_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_TR_LIMIT, guest_tr_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_GDTR_LIMIT, guest_gdtr_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_IDTR_LIMIT, guest_idtr_limit,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_ES_AR_BYTES, guest_es_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_CS_AR_BYTES, guest_cs_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_SS_AR_BYTES, guest_ss_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_DS_AR_BYTES, guest_ds_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_FS_AR_BYTES, guest_fs_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_GS_AR_BYTES, guest_gs_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_LDTR_AR_BYTES, guest_ldtr_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_TR_AR_BYTES, guest_tr_ar_bytes,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_ACTIVITY_STATE, guest_activity_state,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	EVMCS1_FIELD(GUEST_SYSENTER_CS, guest_sysenter_cs,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),

	/* 32 bit read only */
	EVMCS1_FIELD(VM_INSTRUCTION_ERROR, vm_instruction_error,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(VM_EXIT_REASON, vm_exit_reason,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(VM_EXIT_INTR_INFO, vm_exit_intr_info,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(VM_EXIT_INTR_ERROR_CODE, vm_exit_intr_error_code,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(IDT_VECTORING_INFO_FIELD, idt_vectoring_info_field,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(IDT_VECTORING_ERROR_CODE, idt_vectoring_error_code,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(VM_EXIT_INSTRUCTION_LEN, vm_exit_instruction_len,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),
	EVMCS1_FIELD(VMX_INSTRUCTION_INFO, vmx_instruction_info,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE),

	/* No mask defined in the spec (not used) */
	EVMCS1_FIELD(PAGE_FAULT_ERROR_CODE_MASK, page_fault_error_code_mask,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),
	EVMCS1_FIELD(PAGE_FAULT_ERROR_CODE_MATCH, page_fault_error_code_match,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),
	EVMCS1_FIELD(CR3_TARGET_COUNT, cr3_target_count,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),
	EVMCS1_FIELD(VM_EXIT_MSR_STORE_COUNT, vm_exit_msr_store_count,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),
	EVMCS1_FIELD(VM_EXIT_MSR_LOAD_COUNT, vm_exit_msr_load_count,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),
	EVMCS1_FIELD(VM_ENTRY_MSR_LOAD_COUNT, vm_entry_msr_load_count,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL),

	/* 16 bit rw */
	EVMCS1_FIELD(HOST_ES_SELECTOR, host_es_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_CS_SELECTOR, host_cs_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_SS_SELECTOR, host_ss_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_DS_SELECTOR, host_ds_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_FS_SELECTOR, host_fs_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_GS_SELECTOR, host_gs_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(HOST_TR_SELECTOR, host_tr_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	EVMCS1_FIELD(GUEST_ES_SELECTOR, guest_es_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_CS_SELECTOR, guest_cs_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_SS_SELECTOR, guest_ss_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_DS_SELECTOR, guest_ds_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_FS_SELECTOR, guest_fs_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_GS_SELECTOR, guest_gs_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_LDTR_SELECTOR, guest_ldtr_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(GUEST_TR_SELECTOR, guest_tr_selector,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2),
	EVMCS1_FIELD(VIRTUAL_PROCESSOR_ID, virtual_processor_id,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_XLAT),
};
const unsigned int nr_evmcs_1_fields = ARRAY_SIZE(vmcs_field_to_evmcs_1);

__init void evmcs_sanitize_exec_ctrls(struct vmcs_config *vmcs_conf)
{
	vmcs_conf->pin_based_exec_ctrl &= ~EVMCS1_UNSUPPORTED_PINCTRL;
	vmcs_conf->cpu_based_2nd_exec_ctrl &= ~EVMCS1_UNSUPPORTED_2NDEXEC;

	vmcs_conf->vmexit_ctrl &= ~EVMCS1_UNSUPPORTED_VMEXIT_CTRL;
	vmcs_conf->vmentry_ctrl &= ~EVMCS1_UNSUPPORTED_VMENTRY_CTRL;
}
#endif

bool nested_enlightened_vmentry(struct kvm_vcpu *vcpu, u64 *evmcs_gpa)
{
	struct hv_vp_assist_page assist_page;

	*evmcs_gpa = -1ull;

	if (unlikely(!kvm_hv_get_assist_page(vcpu, &assist_page)))
		return false;

	if (unlikely(!assist_page.enlighten_vmentry))
		return false;

	*evmcs_gpa = assist_page.current_nested_vmcs;

	return true;
}

uint16_t nested_get_evmcs_version(struct kvm_vcpu *vcpu)
{
	/*
	 * vmcs_version represents the range of supported Enlightened VMCS
	 * versions: lower 8 bits is the minimal version, higher 8 bits is the
	 * maximum supported version. KVM supports versions from 1 to
	 * KVM_EVMCS_VERSION.
	 */
	if (kvm_cpu_cap_get(X86_FEATURE_VMX) &&
	    (!vcpu || to_vmx(vcpu)->nested.enlightened_vmcs_enabled))
		return (KVM_EVMCS_VERSION << 8) | 1;

	return 0;
}

void nested_evmcs_filter_control_msr(u32 msr_index, u64 *pdata)
{
	u32 ctl_low = (u32)*pdata;
	u32 ctl_high = (u32)(*pdata >> 32);

	/*
	 * Hyper-V 2016 and 2019 try using these features even when eVMCS
	 * is enabled but there are no corresponding fields.
	 */
	switch (msr_index) {
	case MSR_IA32_VMX_EXIT_CTLS:
	case MSR_IA32_VMX_TRUE_EXIT_CTLS:
		ctl_high &= ~VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL;
		break;
	case MSR_IA32_VMX_ENTRY_CTLS:
	case MSR_IA32_VMX_TRUE_ENTRY_CTLS:
		ctl_high &= ~VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL;
		break;
	case MSR_IA32_VMX_PROCBASED_CTLS2:
		ctl_high &= ~SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES;
		break;
	}

	*pdata = ctl_low | ((u64)ctl_high << 32);
}

int nested_evmcs_check_controls(struct vmcs12 *vmcs12)
{
	int ret = 0;
	u32 unsupp_ctl;

	unsupp_ctl = vmcs12->pin_based_vm_exec_control &
		EVMCS1_UNSUPPORTED_PINCTRL;
	if (unsupp_ctl) {
		trace_kvm_nested_vmenter_failed(
			"eVMCS: unsupported pin-based VM-execution controls",
			unsupp_ctl);
		ret = -EINVAL;
	}

	unsupp_ctl = vmcs12->secondary_vm_exec_control &
		EVMCS1_UNSUPPORTED_2NDEXEC;
	if (unsupp_ctl) {
		trace_kvm_nested_vmenter_failed(
			"eVMCS: unsupported secondary VM-execution controls",
			unsupp_ctl);
		ret = -EINVAL;
	}

	unsupp_ctl = vmcs12->vm_exit_controls &
		EVMCS1_UNSUPPORTED_VMEXIT_CTRL;
	if (unsupp_ctl) {
		trace_kvm_nested_vmenter_failed(
			"eVMCS: unsupported VM-exit controls",
			unsupp_ctl);
		ret = -EINVAL;
	}

	unsupp_ctl = vmcs12->vm_entry_controls &
		EVMCS1_UNSUPPORTED_VMENTRY_CTRL;
	if (unsupp_ctl) {
		trace_kvm_nested_vmenter_failed(
			"eVMCS: unsupported VM-entry controls",
			unsupp_ctl);
		ret = -EINVAL;
	}

	unsupp_ctl = vmcs12->vm_function_control & EVMCS1_UNSUPPORTED_VMFUNC;
	if (unsupp_ctl) {
		trace_kvm_nested_vmenter_failed(
			"eVMCS: unsupported VM-function controls",
			unsupp_ctl);
		ret = -EINVAL;
	}

	return ret;
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

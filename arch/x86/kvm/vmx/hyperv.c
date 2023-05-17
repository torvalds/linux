// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/smp.h>

#include "../cpuid.h"
#include "hyperv.h"
#include "nested.h"
#include "vmcs.h"
#include "vmx.h"
#include "trace.h"

#define CC KVM_NESTED_VMENTER_CONSISTENCY_CHECK

/*
 * Enlightened VMCSv1 doesn't support these:
 *
 *	POSTED_INTR_NV                  = 0x00000002,
 *	GUEST_INTR_STATUS               = 0x00000810,
 *	APIC_ACCESS_ADDR		= 0x00002014,
 *	POSTED_INTR_DESC_ADDR           = 0x00002016,
 *	EOI_EXIT_BITMAP0                = 0x0000201c,
 *	EOI_EXIT_BITMAP1                = 0x0000201e,
 *	EOI_EXIT_BITMAP2                = 0x00002020,
 *	EOI_EXIT_BITMAP3                = 0x00002022,
 *	GUEST_PML_INDEX			= 0x00000812,
 *	PML_ADDRESS			= 0x0000200e,
 *	VM_FUNCTION_CONTROL             = 0x00002018,
 *	EPTP_LIST_ADDRESS               = 0x00002024,
 *	VMREAD_BITMAP                   = 0x00002026,
 *	VMWRITE_BITMAP                  = 0x00002028,
 *
 *	TSC_MULTIPLIER                  = 0x00002032,
 *	PLE_GAP                         = 0x00004020,
 *	PLE_WINDOW                      = 0x00004022,
 *	VMX_PREEMPTION_TIMER_VALUE      = 0x0000482E,
 *
 * Currently unsupported in KVM:
 *	GUEST_IA32_RTIT_CTL		= 0x00002814,
 */
#define EVMCS1_SUPPORTED_PINCTRL					\
	(PIN_BASED_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 PIN_BASED_EXT_INTR_MASK |					\
	 PIN_BASED_NMI_EXITING |					\
	 PIN_BASED_VIRTUAL_NMIS)

#define EVMCS1_SUPPORTED_EXEC_CTRL					\
	(CPU_BASED_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 CPU_BASED_HLT_EXITING |					\
	 CPU_BASED_CR3_LOAD_EXITING |					\
	 CPU_BASED_CR3_STORE_EXITING |					\
	 CPU_BASED_UNCOND_IO_EXITING |					\
	 CPU_BASED_MOV_DR_EXITING |					\
	 CPU_BASED_USE_TSC_OFFSETTING |					\
	 CPU_BASED_MWAIT_EXITING |					\
	 CPU_BASED_MONITOR_EXITING |					\
	 CPU_BASED_INVLPG_EXITING |					\
	 CPU_BASED_RDPMC_EXITING |					\
	 CPU_BASED_INTR_WINDOW_EXITING |				\
	 CPU_BASED_CR8_LOAD_EXITING |					\
	 CPU_BASED_CR8_STORE_EXITING |					\
	 CPU_BASED_RDTSC_EXITING |					\
	 CPU_BASED_TPR_SHADOW |						\
	 CPU_BASED_USE_IO_BITMAPS |					\
	 CPU_BASED_MONITOR_TRAP_FLAG |					\
	 CPU_BASED_USE_MSR_BITMAPS |					\
	 CPU_BASED_NMI_WINDOW_EXITING |					\
	 CPU_BASED_PAUSE_EXITING |					\
	 CPU_BASED_ACTIVATE_SECONDARY_CONTROLS)

#define EVMCS1_SUPPORTED_2NDEXEC					\
	(SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |			\
	 SECONDARY_EXEC_WBINVD_EXITING |				\
	 SECONDARY_EXEC_ENABLE_VPID |					\
	 SECONDARY_EXEC_ENABLE_EPT |					\
	 SECONDARY_EXEC_UNRESTRICTED_GUEST |				\
	 SECONDARY_EXEC_DESC |						\
	 SECONDARY_EXEC_ENABLE_RDTSCP |					\
	 SECONDARY_EXEC_ENABLE_INVPCID |				\
	 SECONDARY_EXEC_XSAVES |					\
	 SECONDARY_EXEC_RDSEED_EXITING |				\
	 SECONDARY_EXEC_RDRAND_EXITING |				\
	 SECONDARY_EXEC_TSC_SCALING |					\
	 SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE |				\
	 SECONDARY_EXEC_PT_USE_GPA |					\
	 SECONDARY_EXEC_PT_CONCEAL_VMX |				\
	 SECONDARY_EXEC_BUS_LOCK_DETECTION |				\
	 SECONDARY_EXEC_NOTIFY_VM_EXITING |				\
	 SECONDARY_EXEC_ENCLS_EXITING)

#define EVMCS1_SUPPORTED_3RDEXEC (0ULL)

#define EVMCS1_SUPPORTED_VMEXIT_CTRL					\
	(VM_EXIT_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 VM_EXIT_SAVE_DEBUG_CONTROLS |					\
	 VM_EXIT_ACK_INTR_ON_EXIT |					\
	 VM_EXIT_HOST_ADDR_SPACE_SIZE |					\
	 VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |				\
	 VM_EXIT_SAVE_IA32_PAT |					\
	 VM_EXIT_LOAD_IA32_PAT |					\
	 VM_EXIT_SAVE_IA32_EFER |					\
	 VM_EXIT_LOAD_IA32_EFER |					\
	 VM_EXIT_CLEAR_BNDCFGS |					\
	 VM_EXIT_PT_CONCEAL_PIP |					\
	 VM_EXIT_CLEAR_IA32_RTIT_CTL)

#define EVMCS1_SUPPORTED_VMENTRY_CTRL					\
	(VM_ENTRY_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 VM_ENTRY_LOAD_DEBUG_CONTROLS |					\
	 VM_ENTRY_IA32E_MODE |						\
	 VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL |				\
	 VM_ENTRY_LOAD_IA32_PAT |					\
	 VM_ENTRY_LOAD_IA32_EFER |					\
	 VM_ENTRY_LOAD_BNDCFGS |					\
	 VM_ENTRY_PT_CONCEAL_PIP |					\
	 VM_ENTRY_LOAD_IA32_RTIT_CTL)

#define EVMCS1_SUPPORTED_VMFUNC (0)

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
	EVMCS1_FIELD(HOST_IA32_PERF_GLOBAL_CTRL, host_ia32_perf_global_ctrl,
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
	EVMCS1_FIELD(GUEST_IA32_PERF_GLOBAL_CTRL, guest_ia32_perf_global_ctrl,
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
	EVMCS1_FIELD(ENCLS_EXITING_BITMAP, encls_exiting_bitmap,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP2),
	EVMCS1_FIELD(TSC_MULTIPLIER, tsc_multiplier,
		     HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP2),
	/*
	 * Not used by KVM:
	 *
	 * EVMCS1_FIELD(0x00006828, guest_ia32_s_cet,
	 *	     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	 * EVMCS1_FIELD(0x0000682A, guest_ssp,
	 *	     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_BASIC),
	 * EVMCS1_FIELD(0x0000682C, guest_ia32_int_ssp_table_addr,
	 *	     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	 * EVMCS1_FIELD(0x00002816, guest_ia32_lbr_ctl,
	 *	     HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1),
	 * EVMCS1_FIELD(0x00006C18, host_ia32_s_cet,
	 *	     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	 * EVMCS1_FIELD(0x00006C1A, host_ssp,
	 *	     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	 * EVMCS1_FIELD(0x00006C1C, host_ia32_int_ssp_table_addr,
	 *	     HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1),
	 */

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

#if IS_ENABLED(CONFIG_HYPERV)
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
#endif

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

/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file contains definitions from Hyper-V Hypervisor Top-Level Functional
 * Specification (TLFS):
 * https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/reference/tlfs
 */

#ifndef _ASM_X86_HYPERV_TLFS_H
#define _ASM_X86_HYPERV_TLFS_H

#include <linux/types.h>
#include <asm/page.h>
/*
 * The below CPUID leaves are present if VersionAndFeatures.HypervisorPresent
 * is set by CPUID(HvCpuIdFunctionVersionAndFeatures).
 */
#define HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS	0x40000000
#define HYPERV_CPUID_INTERFACE			0x40000001
#define HYPERV_CPUID_VERSION			0x40000002
#define HYPERV_CPUID_FEATURES			0x40000003
#define HYPERV_CPUID_ENLIGHTMENT_INFO		0x40000004
#define HYPERV_CPUID_IMPLEMENT_LIMITS		0x40000005
#define HYPERV_CPUID_CPU_MANAGEMENT_FEATURES	0x40000007
#define HYPERV_CPUID_NESTED_FEATURES		0x4000000A
#define HYPERV_CPUID_ISOLATION_CONFIG		0x4000000C

#define HYPERV_CPUID_VIRT_STACK_INTERFACE	0x40000081
#define HYPERV_VS_INTERFACE_EAX_SIGNATURE	0x31235356  /* "VS#1" */

#define HYPERV_CPUID_VIRT_STACK_PROPERTIES	0x40000082
/* Support for the extended IOAPIC RTE format */
#define HYPERV_VS_PROPERTIES_EAX_EXTENDED_IOAPIC_RTE	BIT(2)

#define HYPERV_HYPERVISOR_PRESENT_BIT		0x80000000
#define HYPERV_CPUID_MIN			0x40000005
#define HYPERV_CPUID_MAX			0x4000ffff

/*
 * Group D Features.  The bit assignments are custom to each architecture.
 * On x86/x64 these are HYPERV_CPUID_FEATURES.EDX bits.
 */
/* The MWAIT instruction is available (per section MONITOR / MWAIT) */
#define HV_X64_MWAIT_AVAILABLE				BIT(0)
/* Guest debugging support is available */
#define HV_X64_GUEST_DEBUGGING_AVAILABLE		BIT(1)
/* Performance Monitor support is available*/
#define HV_X64_PERF_MONITOR_AVAILABLE			BIT(2)
/* Support for physical CPU dynamic partitioning events is available*/
#define HV_X64_CPU_DYNAMIC_PARTITIONING_AVAILABLE	BIT(3)
/*
 * Support for passing hypercall input parameter block via XMM
 * registers is available
 */
#define HV_X64_HYPERCALL_XMM_INPUT_AVAILABLE		BIT(4)
/* Support for a virtual guest idle state is available */
#define HV_X64_GUEST_IDLE_STATE_AVAILABLE		BIT(5)
/* Frequency MSRs available */
#define HV_FEATURE_FREQUENCY_MSRS_AVAILABLE		BIT(8)
/* Crash MSR available */
#define HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE		BIT(10)
/* Support for debug MSRs available */
#define HV_FEATURE_DEBUG_MSRS_AVAILABLE			BIT(11)
/*
 * Support for returning hypercall output block via XMM
 * registers is available
 */
#define HV_X64_HYPERCALL_XMM_OUTPUT_AVAILABLE		BIT(15)
/* stimer Direct Mode is available */
#define HV_STIMER_DIRECT_MODE_AVAILABLE			BIT(19)

/*
 * Implementation recommendations. Indicates which behaviors the hypervisor
 * recommends the OS implement for optimal performance.
 * These are HYPERV_CPUID_ENLIGHTMENT_INFO.EAX bits.
 */
/*
 * Recommend using hypercall for address space switches rather
 * than MOV to CR3 instruction
 */
#define HV_X64_AS_SWITCH_RECOMMENDED			BIT(0)
/* Recommend using hypercall for local TLB flushes rather
 * than INVLPG or MOV to CR3 instructions */
#define HV_X64_LOCAL_TLB_FLUSH_RECOMMENDED		BIT(1)
/*
 * Recommend using hypercall for remote TLB flushes rather
 * than inter-processor interrupts
 */
#define HV_X64_REMOTE_TLB_FLUSH_RECOMMENDED		BIT(2)
/*
 * Recommend using MSRs for accessing APIC registers
 * EOI, ICR and TPR rather than their memory-mapped counterparts
 */
#define HV_X64_APIC_ACCESS_RECOMMENDED			BIT(3)
/* Recommend using the hypervisor-provided MSR to initiate a system RESET */
#define HV_X64_SYSTEM_RESET_RECOMMENDED			BIT(4)
/*
 * Recommend using relaxed timing for this partition. If used,
 * the VM should disable any watchdog timeouts that rely on the
 * timely delivery of external interrupts
 */
#define HV_X64_RELAXED_TIMING_RECOMMENDED		BIT(5)

/*
 * Recommend not using Auto End-Of-Interrupt feature
 */
#define HV_DEPRECATING_AEOI_RECOMMENDED			BIT(9)

/*
 * Recommend using cluster IPI hypercalls.
 */
#define HV_X64_CLUSTER_IPI_RECOMMENDED			BIT(10)

/* Recommend using the newer ExProcessorMasks interface */
#define HV_X64_EX_PROCESSOR_MASKS_RECOMMENDED		BIT(11)

/* Recommend using enlightened VMCS */
#define HV_X64_ENLIGHTENED_VMCS_RECOMMENDED		BIT(14)

/*
 * CPU management features identification.
 * These are HYPERV_CPUID_CPU_MANAGEMENT_FEATURES.EAX bits.
 */
#define HV_X64_START_LOGICAL_PROCESSOR			BIT(0)
#define HV_X64_CREATE_ROOT_VIRTUAL_PROCESSOR		BIT(1)
#define HV_X64_PERFORMANCE_COUNTER_SYNC			BIT(2)
#define HV_X64_RESERVED_IDENTITY_BIT			BIT(31)

/*
 * Virtual processor will never share a physical core with another virtual
 * processor, except for virtual processors that are reported as sibling SMT
 * threads.
 */
#define HV_X64_NO_NONARCH_CORESHARING			BIT(18)

/* Nested features. These are HYPERV_CPUID_NESTED_FEATURES.EAX bits. */
#define HV_X64_NESTED_DIRECT_FLUSH			BIT(17)
#define HV_X64_NESTED_GUEST_MAPPING_FLUSH		BIT(18)
#define HV_X64_NESTED_MSR_BITMAP			BIT(19)

/*
 * This is specific to AMD and specifies that enlightened TLB flush is
 * supported. If guest opts in to this feature, ASID invalidations only
 * flushes gva -> hpa mapping entries. To flush the TLB entries derived
 * from NPT, hypercalls should be used (HvFlushGuestPhysicalAddressSpace
 * or HvFlushGuestPhysicalAddressList).
 */
#define HV_X64_NESTED_ENLIGHTENED_TLB			BIT(22)

/* HYPERV_CPUID_ISOLATION_CONFIG.EAX bits. */
#define HV_PARAVISOR_PRESENT				BIT(0)

/* HYPERV_CPUID_ISOLATION_CONFIG.EBX bits. */
#define HV_ISOLATION_TYPE				GENMASK(3, 0)
#define HV_SHARED_GPA_BOUNDARY_ACTIVE			BIT(5)
#define HV_SHARED_GPA_BOUNDARY_BITS			GENMASK(11, 6)

enum hv_isolation_type {
	HV_ISOLATION_TYPE_NONE	= 0,
	HV_ISOLATION_TYPE_VBS	= 1,
	HV_ISOLATION_TYPE_SNP	= 2
};

/* Hyper-V specific model specific registers (MSRs) */

/* MSR used to identify the guest OS. */
#define HV_X64_MSR_GUEST_OS_ID			0x40000000

/* MSR used to setup pages used to communicate with the hypervisor. */
#define HV_X64_MSR_HYPERCALL			0x40000001

/* MSR used to provide vcpu index */
#define HV_REGISTER_VP_INDEX			0x40000002

/* MSR used to reset the guest OS. */
#define HV_X64_MSR_RESET			0x40000003

/* MSR used to provide vcpu runtime in 100ns units */
#define HV_X64_MSR_VP_RUNTIME			0x40000010

/* MSR used to read the per-partition time reference counter */
#define HV_REGISTER_TIME_REF_COUNT		0x40000020

/* A partition's reference time stamp counter (TSC) page */
#define HV_REGISTER_REFERENCE_TSC		0x40000021

/* MSR used to retrieve the TSC frequency */
#define HV_X64_MSR_TSC_FREQUENCY		0x40000022

/* MSR used to retrieve the local APIC timer frequency */
#define HV_X64_MSR_APIC_FREQUENCY		0x40000023

/* Define the virtual APIC registers */
#define HV_X64_MSR_EOI				0x40000070
#define HV_X64_MSR_ICR				0x40000071
#define HV_X64_MSR_TPR				0x40000072
#define HV_X64_MSR_VP_ASSIST_PAGE		0x40000073

/* Define synthetic interrupt controller model specific registers. */
#define HV_REGISTER_SCONTROL			0x40000080
#define HV_REGISTER_SVERSION			0x40000081
#define HV_REGISTER_SIEFP			0x40000082
#define HV_REGISTER_SIMP			0x40000083
#define HV_REGISTER_EOM				0x40000084
#define HV_REGISTER_SINT0			0x40000090
#define HV_REGISTER_SINT1			0x40000091
#define HV_REGISTER_SINT2			0x40000092
#define HV_REGISTER_SINT3			0x40000093
#define HV_REGISTER_SINT4			0x40000094
#define HV_REGISTER_SINT5			0x40000095
#define HV_REGISTER_SINT6			0x40000096
#define HV_REGISTER_SINT7			0x40000097
#define HV_REGISTER_SINT8			0x40000098
#define HV_REGISTER_SINT9			0x40000099
#define HV_REGISTER_SINT10			0x4000009A
#define HV_REGISTER_SINT11			0x4000009B
#define HV_REGISTER_SINT12			0x4000009C
#define HV_REGISTER_SINT13			0x4000009D
#define HV_REGISTER_SINT14			0x4000009E
#define HV_REGISTER_SINT15			0x4000009F

/*
 * Synthetic Timer MSRs. Four timers per vcpu.
 */
#define HV_REGISTER_STIMER0_CONFIG		0x400000B0
#define HV_REGISTER_STIMER0_COUNT		0x400000B1
#define HV_REGISTER_STIMER1_CONFIG		0x400000B2
#define HV_REGISTER_STIMER1_COUNT		0x400000B3
#define HV_REGISTER_STIMER2_CONFIG		0x400000B4
#define HV_REGISTER_STIMER2_COUNT		0x400000B5
#define HV_REGISTER_STIMER3_CONFIG		0x400000B6
#define HV_REGISTER_STIMER3_COUNT		0x400000B7

/* Hyper-V guest idle MSR */
#define HV_X64_MSR_GUEST_IDLE			0x400000F0

/* Hyper-V guest crash notification MSR's */
#define HV_REGISTER_CRASH_P0			0x40000100
#define HV_REGISTER_CRASH_P1			0x40000101
#define HV_REGISTER_CRASH_P2			0x40000102
#define HV_REGISTER_CRASH_P3			0x40000103
#define HV_REGISTER_CRASH_P4			0x40000104
#define HV_REGISTER_CRASH_CTL			0x40000105

/* TSC emulation after migration */
#define HV_X64_MSR_REENLIGHTENMENT_CONTROL	0x40000106
#define HV_X64_MSR_TSC_EMULATION_CONTROL	0x40000107
#define HV_X64_MSR_TSC_EMULATION_STATUS		0x40000108

/* TSC invariant control */
#define HV_X64_MSR_TSC_INVARIANT_CONTROL	0x40000118

/* Register name aliases for temporary compatibility */
#define HV_X64_MSR_STIMER0_COUNT	HV_REGISTER_STIMER0_COUNT
#define HV_X64_MSR_STIMER0_CONFIG	HV_REGISTER_STIMER0_CONFIG
#define HV_X64_MSR_STIMER1_COUNT	HV_REGISTER_STIMER1_COUNT
#define HV_X64_MSR_STIMER1_CONFIG	HV_REGISTER_STIMER1_CONFIG
#define HV_X64_MSR_STIMER2_COUNT	HV_REGISTER_STIMER2_COUNT
#define HV_X64_MSR_STIMER2_CONFIG	HV_REGISTER_STIMER2_CONFIG
#define HV_X64_MSR_STIMER3_COUNT	HV_REGISTER_STIMER3_COUNT
#define HV_X64_MSR_STIMER3_CONFIG	HV_REGISTER_STIMER3_CONFIG
#define HV_X64_MSR_SCONTROL		HV_REGISTER_SCONTROL
#define HV_X64_MSR_SVERSION		HV_REGISTER_SVERSION
#define HV_X64_MSR_SIMP			HV_REGISTER_SIMP
#define HV_X64_MSR_SIEFP		HV_REGISTER_SIEFP
#define HV_X64_MSR_VP_INDEX		HV_REGISTER_VP_INDEX
#define HV_X64_MSR_EOM			HV_REGISTER_EOM
#define HV_X64_MSR_SINT0		HV_REGISTER_SINT0
#define HV_X64_MSR_SINT15		HV_REGISTER_SINT15
#define HV_X64_MSR_CRASH_P0		HV_REGISTER_CRASH_P0
#define HV_X64_MSR_CRASH_P1		HV_REGISTER_CRASH_P1
#define HV_X64_MSR_CRASH_P2		HV_REGISTER_CRASH_P2
#define HV_X64_MSR_CRASH_P3		HV_REGISTER_CRASH_P3
#define HV_X64_MSR_CRASH_P4		HV_REGISTER_CRASH_P4
#define HV_X64_MSR_CRASH_CTL		HV_REGISTER_CRASH_CTL
#define HV_X64_MSR_TIME_REF_COUNT	HV_REGISTER_TIME_REF_COUNT
#define HV_X64_MSR_REFERENCE_TSC	HV_REGISTER_REFERENCE_TSC

/*
 * Declare the MSR used to setup pages used to communicate with the hypervisor.
 */
union hv_x64_msr_hypercall_contents {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 reserved:11;
		u64 guest_physical_address:52;
	} __packed;
};

union hv_vp_assist_msr_contents {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 reserved:11;
		u64 pfn:52;
	} __packed;
};

struct hv_reenlightenment_control {
	__u64 vector:8;
	__u64 reserved1:8;
	__u64 enabled:1;
	__u64 reserved2:15;
	__u64 target_vp:32;
}  __packed;

struct hv_tsc_emulation_control {
	__u64 enabled:1;
	__u64 reserved:63;
} __packed;

struct hv_tsc_emulation_status {
	__u64 inprogress:1;
	__u64 reserved:63;
} __packed;

#define HV_X64_MSR_HYPERCALL_ENABLE		0x00000001
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT) - 1))

#define HV_X64_MSR_CRASH_PARAMS		\
		(1 + (HV_X64_MSR_CRASH_P4 - HV_X64_MSR_CRASH_P0))

#define HV_IPI_LOW_VECTOR	0x10
#define HV_IPI_HIGH_VECTOR	0xff

#define HV_X64_MSR_VP_ASSIST_PAGE_ENABLE	0x00000001
#define HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_SHIFT) - 1))

/* Hyper-V Enlightened VMCS version mask in nested features CPUID */
#define HV_X64_ENLIGHTENED_VMCS_VERSION		0xff

#define HV_X64_MSR_TSC_REFERENCE_ENABLE		0x00000001
#define HV_X64_MSR_TSC_REFERENCE_ADDRESS_SHIFT	12

/* Number of XMM registers used in hypercall input/output */
#define HV_HYPERCALL_MAX_XMM_REGISTERS		6

struct hv_nested_enlightenments_control {
	struct {
		__u32 directhypercall:1;
		__u32 reserved:31;
	} features;
	struct {
		__u32 reserved;
	} hypercallControls;
} __packed;

/* Define virtual processor assist page structure. */
struct hv_vp_assist_page {
	__u32 apic_assist;
	__u32 reserved1;
	__u64 vtl_control[3];
	struct hv_nested_enlightenments_control nested_control;
	__u8 enlighten_vmentry;
	__u8 reserved2[7];
	__u64 current_nested_vmcs;
} __packed;

struct hv_enlightened_vmcs {
	u32 revision_id;
	u32 abort;

	u16 host_es_selector;
	u16 host_cs_selector;
	u16 host_ss_selector;
	u16 host_ds_selector;
	u16 host_fs_selector;
	u16 host_gs_selector;
	u16 host_tr_selector;

	u16 padding16_1;

	u64 host_ia32_pat;
	u64 host_ia32_efer;

	u64 host_cr0;
	u64 host_cr3;
	u64 host_cr4;

	u64 host_ia32_sysenter_esp;
	u64 host_ia32_sysenter_eip;
	u64 host_rip;
	u32 host_ia32_sysenter_cs;

	u32 pin_based_vm_exec_control;
	u32 vm_exit_controls;
	u32 secondary_vm_exec_control;

	u64 io_bitmap_a;
	u64 io_bitmap_b;
	u64 msr_bitmap;

	u16 guest_es_selector;
	u16 guest_cs_selector;
	u16 guest_ss_selector;
	u16 guest_ds_selector;
	u16 guest_fs_selector;
	u16 guest_gs_selector;
	u16 guest_ldtr_selector;
	u16 guest_tr_selector;

	u32 guest_es_limit;
	u32 guest_cs_limit;
	u32 guest_ss_limit;
	u32 guest_ds_limit;
	u32 guest_fs_limit;
	u32 guest_gs_limit;
	u32 guest_ldtr_limit;
	u32 guest_tr_limit;
	u32 guest_gdtr_limit;
	u32 guest_idtr_limit;

	u32 guest_es_ar_bytes;
	u32 guest_cs_ar_bytes;
	u32 guest_ss_ar_bytes;
	u32 guest_ds_ar_bytes;
	u32 guest_fs_ar_bytes;
	u32 guest_gs_ar_bytes;
	u32 guest_ldtr_ar_bytes;
	u32 guest_tr_ar_bytes;

	u64 guest_es_base;
	u64 guest_cs_base;
	u64 guest_ss_base;
	u64 guest_ds_base;
	u64 guest_fs_base;
	u64 guest_gs_base;
	u64 guest_ldtr_base;
	u64 guest_tr_base;
	u64 guest_gdtr_base;
	u64 guest_idtr_base;

	u64 padding64_1[3];

	u64 vm_exit_msr_store_addr;
	u64 vm_exit_msr_load_addr;
	u64 vm_entry_msr_load_addr;

	u64 cr3_target_value0;
	u64 cr3_target_value1;
	u64 cr3_target_value2;
	u64 cr3_target_value3;

	u32 page_fault_error_code_mask;
	u32 page_fault_error_code_match;

	u32 cr3_target_count;
	u32 vm_exit_msr_store_count;
	u32 vm_exit_msr_load_count;
	u32 vm_entry_msr_load_count;

	u64 tsc_offset;
	u64 virtual_apic_page_addr;
	u64 vmcs_link_pointer;

	u64 guest_ia32_debugctl;
	u64 guest_ia32_pat;
	u64 guest_ia32_efer;

	u64 guest_pdptr0;
	u64 guest_pdptr1;
	u64 guest_pdptr2;
	u64 guest_pdptr3;

	u64 guest_pending_dbg_exceptions;
	u64 guest_sysenter_esp;
	u64 guest_sysenter_eip;

	u32 guest_activity_state;
	u32 guest_sysenter_cs;

	u64 cr0_guest_host_mask;
	u64 cr4_guest_host_mask;
	u64 cr0_read_shadow;
	u64 cr4_read_shadow;
	u64 guest_cr0;
	u64 guest_cr3;
	u64 guest_cr4;
	u64 guest_dr7;

	u64 host_fs_base;
	u64 host_gs_base;
	u64 host_tr_base;
	u64 host_gdtr_base;
	u64 host_idtr_base;
	u64 host_rsp;

	u64 ept_pointer;

	u16 virtual_processor_id;
	u16 padding16_2[3];

	u64 padding64_2[5];
	u64 guest_physical_address;

	u32 vm_instruction_error;
	u32 vm_exit_reason;
	u32 vm_exit_intr_info;
	u32 vm_exit_intr_error_code;
	u32 idt_vectoring_info_field;
	u32 idt_vectoring_error_code;
	u32 vm_exit_instruction_len;
	u32 vmx_instruction_info;

	u64 exit_qualification;
	u64 exit_io_instruction_ecx;
	u64 exit_io_instruction_esi;
	u64 exit_io_instruction_edi;
	u64 exit_io_instruction_eip;

	u64 guest_linear_address;
	u64 guest_rsp;
	u64 guest_rflags;

	u32 guest_interruptibility_info;
	u32 cpu_based_vm_exec_control;
	u32 exception_bitmap;
	u32 vm_entry_controls;
	u32 vm_entry_intr_info_field;
	u32 vm_entry_exception_error_code;
	u32 vm_entry_instruction_len;
	u32 tpr_threshold;

	u64 guest_rip;

	u32 hv_clean_fields;
	u32 padding32_1;
	u32 hv_synthetic_controls;
	struct {
		u32 nested_flush_hypercall:1;
		u32 msr_bitmap:1;
		u32 reserved:30;
	}  __packed hv_enlightenments_control;
	u32 hv_vp_id;
	u32 padding32_2;
	u64 hv_vm_id;
	u64 partition_assist_page;
	u64 padding64_4[4];
	u64 guest_bndcfgs;
	u64 padding64_5[7];
	u64 xss_exit_bitmap;
	u64 padding64_6[7];
} __packed;

#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_NONE			0
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_IO_BITMAP		BIT(0)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_MSR_BITMAP		BIT(1)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP2		BIT(2)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_GRP1		BIT(3)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_PROC		BIT(4)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EVENT		BIT(5)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_ENTRY		BIT(6)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_EXCPN		BIT(7)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CRDR			BIT(8)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_CONTROL_XLAT		BIT(9)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_BASIC		BIT(10)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP1		BIT(11)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_GUEST_GRP2		BIT(12)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_POINTER		BIT(13)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_HOST_GRP1		BIT(14)
#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_ENLIGHTENMENTSCONTROL	BIT(15)

#define HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL			0xFFFF

struct hv_partition_assist_pg {
	u32 tlb_lock_count;
};

enum hv_interrupt_type {
	HV_X64_INTERRUPT_TYPE_FIXED             = 0x0000,
	HV_X64_INTERRUPT_TYPE_LOWESTPRIORITY    = 0x0001,
	HV_X64_INTERRUPT_TYPE_SMI               = 0x0002,
	HV_X64_INTERRUPT_TYPE_REMOTEREAD        = 0x0003,
	HV_X64_INTERRUPT_TYPE_NMI               = 0x0004,
	HV_X64_INTERRUPT_TYPE_INIT              = 0x0005,
	HV_X64_INTERRUPT_TYPE_SIPI              = 0x0006,
	HV_X64_INTERRUPT_TYPE_EXTINT            = 0x0007,
	HV_X64_INTERRUPT_TYPE_LOCALINT0         = 0x0008,
	HV_X64_INTERRUPT_TYPE_LOCALINT1         = 0x0009,
	HV_X64_INTERRUPT_TYPE_MAXIMUM           = 0x000A,
};

#include <asm-generic/hyperv-tlfs.h>

#endif

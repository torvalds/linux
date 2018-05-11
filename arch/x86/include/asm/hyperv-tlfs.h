/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

/*
 * This file contains definitions from Hyper-V Hypervisor Top-Level Functional
 * Specification (TLFS):
 * https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/reference/tlfs
 */

#ifndef _ASM_X86_HYPERV_TLFS_H
#define _ASM_X86_HYPERV_TLFS_H

#include <linux/types.h>

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
#define HYPERV_CPUID_NESTED_FEATURES		0x4000000A

#define HYPERV_HYPERVISOR_PRESENT_BIT		0x80000000
#define HYPERV_CPUID_MIN			0x40000005
#define HYPERV_CPUID_MAX			0x4000ffff

/*
 * Feature identification. EAX indicates which features are available
 * to the partition based upon the current partition privileges.
 */

/* VP Runtime (HV_X64_MSR_VP_RUNTIME) available */
#define HV_X64_MSR_VP_RUNTIME_AVAILABLE		(1 << 0)
/* Partition Reference Counter (HV_X64_MSR_TIME_REF_COUNT) available*/
#define HV_X64_MSR_TIME_REF_COUNT_AVAILABLE	(1 << 1)
/* Partition reference TSC MSR is available */
#define HV_X64_MSR_REFERENCE_TSC_AVAILABLE              (1 << 9)

/* A partition's reference time stamp counter (TSC) page */
#define HV_X64_MSR_REFERENCE_TSC		0x40000021

/*
 * There is a single feature flag that signifies if the partition has access
 * to MSRs with local APIC and TSC frequencies.
 */
#define HV_X64_ACCESS_FREQUENCY_MSRS		(1 << 11)

/* AccessReenlightenmentControls privilege */
#define HV_X64_ACCESS_REENLIGHTENMENT		BIT(13)

/*
 * Basic SynIC MSRs (HV_X64_MSR_SCONTROL through HV_X64_MSR_EOM
 * and HV_X64_MSR_SINT0 through HV_X64_MSR_SINT15) available
 */
#define HV_X64_MSR_SYNIC_AVAILABLE		(1 << 2)
/*
 * Synthetic Timer MSRs (HV_X64_MSR_STIMER0_CONFIG through
 * HV_X64_MSR_STIMER3_COUNT) available
 */
#define HV_X64_MSR_SYNTIMER_AVAILABLE		(1 << 3)
/*
 * APIC access MSRs (HV_X64_MSR_EOI, HV_X64_MSR_ICR and HV_X64_MSR_TPR)
 * are available
 */
#define HV_X64_MSR_APIC_ACCESS_AVAILABLE	(1 << 4)
/* Hypercall MSRs (HV_X64_MSR_GUEST_OS_ID and HV_X64_MSR_HYPERCALL) available*/
#define HV_X64_MSR_HYPERCALL_AVAILABLE		(1 << 5)
/* Access virtual processor index MSR (HV_X64_MSR_VP_INDEX) available*/
#define HV_X64_MSR_VP_INDEX_AVAILABLE		(1 << 6)
/* Virtual system reset MSR (HV_X64_MSR_RESET) is available*/
#define HV_X64_MSR_RESET_AVAILABLE		(1 << 7)
 /*
  * Access statistics pages MSRs (HV_X64_MSR_STATS_PARTITION_RETAIL_PAGE,
  * HV_X64_MSR_STATS_PARTITION_INTERNAL_PAGE, HV_X64_MSR_STATS_VP_RETAIL_PAGE,
  * HV_X64_MSR_STATS_VP_INTERNAL_PAGE) available
  */
#define HV_X64_MSR_STAT_PAGES_AVAILABLE		(1 << 8)

/* Frequency MSRs available */
#define HV_FEATURE_FREQUENCY_MSRS_AVAILABLE	(1 << 8)

/* Crash MSR available */
#define HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE (1 << 10)

/* stimer Direct Mode is available */
#define HV_X64_STIMER_DIRECT_MODE_AVAILABLE	(1 << 19)

/*
 * Feature identification: EBX indicates which flags were specified at
 * partition creation. The format is the same as the partition creation
 * flag structure defined in section Partition Creation Flags.
 */
#define HV_X64_CREATE_PARTITIONS		(1 << 0)
#define HV_X64_ACCESS_PARTITION_ID		(1 << 1)
#define HV_X64_ACCESS_MEMORY_POOL		(1 << 2)
#define HV_X64_ADJUST_MESSAGE_BUFFERS		(1 << 3)
#define HV_X64_POST_MESSAGES			(1 << 4)
#define HV_X64_SIGNAL_EVENTS			(1 << 5)
#define HV_X64_CREATE_PORT			(1 << 6)
#define HV_X64_CONNECT_PORT			(1 << 7)
#define HV_X64_ACCESS_STATS			(1 << 8)
#define HV_X64_DEBUGGING			(1 << 11)
#define HV_X64_CPU_POWER_MANAGEMENT		(1 << 12)
#define HV_X64_CONFIGURE_PROFILER		(1 << 13)

/*
 * Feature identification. EDX indicates which miscellaneous features
 * are available to the partition.
 */
/* The MWAIT instruction is available (per section MONITOR / MWAIT) */
#define HV_X64_MWAIT_AVAILABLE				(1 << 0)
/* Guest debugging support is available */
#define HV_X64_GUEST_DEBUGGING_AVAILABLE		(1 << 1)
/* Performance Monitor support is available*/
#define HV_X64_PERF_MONITOR_AVAILABLE			(1 << 2)
/* Support for physical CPU dynamic partitioning events is available*/
#define HV_X64_CPU_DYNAMIC_PARTITIONING_AVAILABLE	(1 << 3)
/*
 * Support for passing hypercall input parameter block via XMM
 * registers is available
 */
#define HV_X64_HYPERCALL_PARAMS_XMM_AVAILABLE		(1 << 4)
/* Support for a virtual guest idle state is available */
#define HV_X64_GUEST_IDLE_STATE_AVAILABLE		(1 << 5)
/* Guest crash data handler available */
#define HV_X64_GUEST_CRASH_MSR_AVAILABLE		(1 << 10)

/*
 * Implementation recommendations. Indicates which behaviors the hypervisor
 * recommends the OS implement for optimal performance.
 */
 /*
  * Recommend using hypercall for address space switches rather
  * than MOV to CR3 instruction
  */
#define HV_X64_AS_SWITCH_RECOMMENDED		(1 << 0)
/* Recommend using hypercall for local TLB flushes rather
 * than INVLPG or MOV to CR3 instructions */
#define HV_X64_LOCAL_TLB_FLUSH_RECOMMENDED	(1 << 1)
/*
 * Recommend using hypercall for remote TLB flushes rather
 * than inter-processor interrupts
 */
#define HV_X64_REMOTE_TLB_FLUSH_RECOMMENDED	(1 << 2)
/*
 * Recommend using MSRs for accessing APIC registers
 * EOI, ICR and TPR rather than their memory-mapped counterparts
 */
#define HV_X64_APIC_ACCESS_RECOMMENDED		(1 << 3)
/* Recommend using the hypervisor-provided MSR to initiate a system RESET */
#define HV_X64_SYSTEM_RESET_RECOMMENDED		(1 << 4)
/*
 * Recommend using relaxed timing for this partition. If used,
 * the VM should disable any watchdog timeouts that rely on the
 * timely delivery of external interrupts
 */
#define HV_X64_RELAXED_TIMING_RECOMMENDED	(1 << 5)

/*
 * Virtual APIC support
 */
#define HV_X64_DEPRECATING_AEOI_RECOMMENDED	(1 << 9)

/* Recommend using the newer ExProcessorMasks interface */
#define HV_X64_EX_PROCESSOR_MASKS_RECOMMENDED	(1 << 11)

/* Recommend using enlightened VMCS */
#define HV_X64_ENLIGHTENED_VMCS_RECOMMENDED    (1 << 14)

/*
 * Crash notification flag.
 */
#define HV_CRASH_CTL_CRASH_NOTIFY (1ULL << 63)

/* MSR used to identify the guest OS. */
#define HV_X64_MSR_GUEST_OS_ID			0x40000000

/* MSR used to setup pages used to communicate with the hypervisor. */
#define HV_X64_MSR_HYPERCALL			0x40000001

/* MSR used to provide vcpu index */
#define HV_X64_MSR_VP_INDEX			0x40000002

/* MSR used to reset the guest OS. */
#define HV_X64_MSR_RESET			0x40000003

/* MSR used to provide vcpu runtime in 100ns units */
#define HV_X64_MSR_VP_RUNTIME			0x40000010

/* MSR used to read the per-partition time reference counter */
#define HV_X64_MSR_TIME_REF_COUNT		0x40000020

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
#define HV_X64_MSR_SCONTROL			0x40000080
#define HV_X64_MSR_SVERSION			0x40000081
#define HV_X64_MSR_SIEFP			0x40000082
#define HV_X64_MSR_SIMP				0x40000083
#define HV_X64_MSR_EOM				0x40000084
#define HV_X64_MSR_SINT0			0x40000090
#define HV_X64_MSR_SINT1			0x40000091
#define HV_X64_MSR_SINT2			0x40000092
#define HV_X64_MSR_SINT3			0x40000093
#define HV_X64_MSR_SINT4			0x40000094
#define HV_X64_MSR_SINT5			0x40000095
#define HV_X64_MSR_SINT6			0x40000096
#define HV_X64_MSR_SINT7			0x40000097
#define HV_X64_MSR_SINT8			0x40000098
#define HV_X64_MSR_SINT9			0x40000099
#define HV_X64_MSR_SINT10			0x4000009A
#define HV_X64_MSR_SINT11			0x4000009B
#define HV_X64_MSR_SINT12			0x4000009C
#define HV_X64_MSR_SINT13			0x4000009D
#define HV_X64_MSR_SINT14			0x4000009E
#define HV_X64_MSR_SINT15			0x4000009F

/*
 * Synthetic Timer MSRs. Four timers per vcpu.
 */
#define HV_X64_MSR_STIMER0_CONFIG		0x400000B0
#define HV_X64_MSR_STIMER0_COUNT		0x400000B1
#define HV_X64_MSR_STIMER1_CONFIG		0x400000B2
#define HV_X64_MSR_STIMER1_COUNT		0x400000B3
#define HV_X64_MSR_STIMER2_CONFIG		0x400000B4
#define HV_X64_MSR_STIMER2_COUNT		0x400000B5
#define HV_X64_MSR_STIMER3_CONFIG		0x400000B6
#define HV_X64_MSR_STIMER3_COUNT		0x400000B7

/* Hyper-V guest crash notification MSR's */
#define HV_X64_MSR_CRASH_P0			0x40000100
#define HV_X64_MSR_CRASH_P1			0x40000101
#define HV_X64_MSR_CRASH_P2			0x40000102
#define HV_X64_MSR_CRASH_P3			0x40000103
#define HV_X64_MSR_CRASH_P4			0x40000104
#define HV_X64_MSR_CRASH_CTL			0x40000105
#define HV_X64_MSR_CRASH_CTL_NOTIFY		(1ULL << 63)
#define HV_X64_MSR_CRASH_PARAMS		\
		(1 + (HV_X64_MSR_CRASH_P4 - HV_X64_MSR_CRASH_P0))

/*
 * Declare the MSR used to setup pages used to communicate with the hypervisor.
 */
union hv_x64_msr_hypercall_contents {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 reserved:11;
		u64 guest_physical_address:52;
	};
};

/*
 * TSC page layout.
 */
struct ms_hyperv_tsc_page {
	volatile u32 tsc_sequence;
	u32 reserved1;
	volatile u64 tsc_scale;
	volatile s64 tsc_offset;
	u64 reserved2[509];
};

/*
 * The guest OS needs to register the guest ID with the hypervisor.
 * The guest ID is a 64 bit entity and the structure of this ID is
 * specified in the Hyper-V specification:
 *
 * msdn.microsoft.com/en-us/library/windows/hardware/ff542653%28v=vs.85%29.aspx
 *
 * While the current guideline does not specify how Linux guest ID(s)
 * need to be generated, our plan is to publish the guidelines for
 * Linux and other guest operating systems that currently are hosted
 * on Hyper-V. The implementation here conforms to this yet
 * unpublished guidelines.
 *
 *
 * Bit(s)
 * 63 - Indicates if the OS is Open Source or not; 1 is Open Source
 * 62:56 - Os Type; Linux is 0x100
 * 55:48 - Distro specific identification
 * 47:16 - Linux kernel version number
 * 15:0  - Distro specific identification
 *
 *
 */

#define HV_LINUX_VENDOR_ID              0x8100

/* TSC emulation after migration */
#define HV_X64_MSR_REENLIGHTENMENT_CONTROL	0x40000106

struct hv_reenlightenment_control {
	__u64 vector:8;
	__u64 reserved1:8;
	__u64 enabled:1;
	__u64 reserved2:15;
	__u64 target_vp:32;
};

#define HV_X64_MSR_TSC_EMULATION_CONTROL	0x40000107
#define HV_X64_MSR_TSC_EMULATION_STATUS		0x40000108

struct hv_tsc_emulation_control {
	__u64 enabled:1;
	__u64 reserved:63;
};

struct hv_tsc_emulation_status {
	__u64 inprogress:1;
	__u64 reserved:63;
};

#define HV_X64_MSR_HYPERCALL_ENABLE		0x00000001
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT) - 1))

/* Declare the various hypercall operations. */
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE	0x0002
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST	0x0003
#define HVCALL_NOTIFY_LONG_SPIN_WAIT		0x0008
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX  0x0013
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST_EX   0x0014
#define HVCALL_POST_MESSAGE			0x005c
#define HVCALL_SIGNAL_EVENT			0x005d

#define HV_X64_MSR_VP_ASSIST_PAGE_ENABLE	0x00000001
#define HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_SHIFT) - 1))

/* Hyper-V Enlightened VMCS version mask in nested features CPUID */
#define HV_X64_ENLIGHTENED_VMCS_VERSION		0xff

#define HV_X64_MSR_TSC_REFERENCE_ENABLE		0x00000001
#define HV_X64_MSR_TSC_REFERENCE_ADDRESS_SHIFT	12

#define HV_PROCESSOR_POWER_STATE_C0		0
#define HV_PROCESSOR_POWER_STATE_C1		1
#define HV_PROCESSOR_POWER_STATE_C2		2
#define HV_PROCESSOR_POWER_STATE_C3		3

#define HV_FLUSH_ALL_PROCESSORS			BIT(0)
#define HV_FLUSH_ALL_VIRTUAL_ADDRESS_SPACES	BIT(1)
#define HV_FLUSH_NON_GLOBAL_MAPPINGS_ONLY	BIT(2)
#define HV_FLUSH_USE_EXTENDED_RANGE_FORMAT	BIT(3)

enum HV_GENERIC_SET_FORMAT {
	HV_GENERIC_SET_SPARCE_4K,
	HV_GENERIC_SET_ALL,
};

#define HV_HYPERCALL_RESULT_MASK	GENMASK_ULL(15, 0)
#define HV_HYPERCALL_FAST_BIT		BIT(16)
#define HV_HYPERCALL_VARHEAD_OFFSET	17
#define HV_HYPERCALL_REP_COMP_OFFSET	32
#define HV_HYPERCALL_REP_COMP_MASK	GENMASK_ULL(43, 32)
#define HV_HYPERCALL_REP_START_OFFSET	48
#define HV_HYPERCALL_REP_START_MASK	GENMASK_ULL(59, 48)

/* hypercall status code */
#define HV_STATUS_SUCCESS			0
#define HV_STATUS_INVALID_HYPERCALL_CODE	2
#define HV_STATUS_INVALID_HYPERCALL_INPUT	3
#define HV_STATUS_INVALID_ALIGNMENT		4
#define HV_STATUS_INVALID_PARAMETER		5
#define HV_STATUS_INSUFFICIENT_MEMORY		11
#define HV_STATUS_INVALID_PORT_ID		17
#define HV_STATUS_INVALID_CONNECTION_ID		18
#define HV_STATUS_INSUFFICIENT_BUFFERS		19

typedef struct _HV_REFERENCE_TSC_PAGE {
	__u32 tsc_sequence;
	__u32 res1;
	__u64 tsc_scale;
	__s64 tsc_offset;
} HV_REFERENCE_TSC_PAGE, *PHV_REFERENCE_TSC_PAGE;

/* Define the number of synthetic interrupt sources. */
#define HV_SYNIC_SINT_COUNT		(16)
/* Define the expected SynIC version. */
#define HV_SYNIC_VERSION_1		(0x1)
/* Valid SynIC vectors are 16-255. */
#define HV_SYNIC_FIRST_VALID_VECTOR	(16)

#define HV_SYNIC_CONTROL_ENABLE		(1ULL << 0)
#define HV_SYNIC_SIMP_ENABLE		(1ULL << 0)
#define HV_SYNIC_SIEFP_ENABLE		(1ULL << 0)
#define HV_SYNIC_SINT_MASKED		(1ULL << 16)
#define HV_SYNIC_SINT_AUTO_EOI		(1ULL << 17)
#define HV_SYNIC_SINT_VECTOR_MASK	(0xFF)

#define HV_SYNIC_STIMER_COUNT		(4)

/* Define synthetic interrupt controller message constants. */
#define HV_MESSAGE_SIZE			(256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT	(240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT	(30)

/* Define hypervisor message types. */
enum hv_message_type {
	HVMSG_NONE			= 0x00000000,

	/* Memory access messages. */
	HVMSG_UNMAPPED_GPA		= 0x80000000,
	HVMSG_GPA_INTERCEPT		= 0x80000001,

	/* Timer notification messages. */
	HVMSG_TIMER_EXPIRED			= 0x80000010,

	/* Error messages. */
	HVMSG_INVALID_VP_REGISTER_VALUE	= 0x80000020,
	HVMSG_UNRECOVERABLE_EXCEPTION	= 0x80000021,
	HVMSG_UNSUPPORTED_FEATURE		= 0x80000022,

	/* Trace buffer complete messages. */
	HVMSG_EVENTLOG_BUFFERCOMPLETE	= 0x80000040,

	/* Platform-specific processor intercept messages. */
	HVMSG_X64_IOPORT_INTERCEPT		= 0x80010000,
	HVMSG_X64_MSR_INTERCEPT		= 0x80010001,
	HVMSG_X64_CPUID_INTERCEPT		= 0x80010002,
	HVMSG_X64_EXCEPTION_INTERCEPT	= 0x80010003,
	HVMSG_X64_APIC_EOI			= 0x80010004,
	HVMSG_X64_LEGACY_FP_ERROR		= 0x80010005
};

/* Define synthetic interrupt controller message flags. */
union hv_message_flags {
	__u8 asu8;
	struct {
		__u8 msg_pending:1;
		__u8 reserved:7;
	};
};

/* Define port identifier type. */
union hv_port_id {
	__u32 asu32;
	struct {
		__u32 id:24;
		__u32 reserved:8;
	} u;
};

/* Define synthetic interrupt controller message header. */
struct hv_message_header {
	__u32 message_type;
	__u8 payload_size;
	union hv_message_flags message_flags;
	__u8 reserved[2];
	union {
		__u64 sender;
		union hv_port_id port;
	};
};

/* Define synthetic interrupt controller message format. */
struct hv_message {
	struct hv_message_header header;
	union {
		__u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
	} u;
};

/* Define the synthetic interrupt message page layout. */
struct hv_message_page {
	struct hv_message sint_message[HV_SYNIC_SINT_COUNT];
};

/* Define timer message payload structure. */
struct hv_timer_message_payload {
	__u32 timer_index;
	__u32 reserved;
	__u64 expiration_time;	/* When the timer expired */
	__u64 delivery_time;	/* When the message was delivered */
};

/* Define virtual processor assist page structure. */
struct hv_vp_assist_page {
	__u32 apic_assist;
	__u32 reserved;
	__u64 vtl_control[2];
	__u64 nested_enlightenments_control[2];
	__u32 enlighten_vmentry;
	__u64 current_nested_vmcs;
};

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
	u16 padding16[3];

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
	u32 hv_padding_32;
	u32 hv_synthetic_controls;
	u32 hv_enlightenments_control;
	u32 hv_vp_id;

	u64 hv_vm_id;
	u64 partition_assist_page;
	u64 padding64_4[4];
	u64 guest_bndcfgs;
	u64 padding64_5[7];
	u64 xss_exit_bitmap;
	u64 padding64_6[7];
};

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

#define HV_STIMER_ENABLE		(1ULL << 0)
#define HV_STIMER_PERIODIC		(1ULL << 1)
#define HV_STIMER_LAZY			(1ULL << 2)
#define HV_STIMER_AUTOENABLE		(1ULL << 3)
#define HV_STIMER_SINT(config)		(__u8)(((config) >> 16) & 0x0F)

#endif

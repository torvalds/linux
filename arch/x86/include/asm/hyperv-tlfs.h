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
#define HV_X64_MSR_APIC_ASSIST_PAGE		0x40000073

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

#define HV_X64_MSR_APIC_ASSIST_PAGE_ENABLE		0x00000001
#define HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_SHIFT) - 1))

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

#define HV_STIMER_ENABLE		(1ULL << 0)
#define HV_STIMER_PERIODIC		(1ULL << 1)
#define HV_STIMER_LAZY			(1ULL << 2)
#define HV_STIMER_AUTOENABLE		(1ULL << 3)
#define HV_STIMER_SINT(config)		(__u8)(((config) >> 16) & 0x0F)

#endif

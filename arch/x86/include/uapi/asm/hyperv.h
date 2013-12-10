#ifndef _ASM_X86_HYPERV_H
#define _ASM_X86_HYPERV_H

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

/*
 * There is a single feature flag that signifies the presence of the MSR
 * that can be used to retrieve both the local APIC Timer frequency as
 * well as the TSC frequency.
 */

/* Local APIC timer frequency MSR (HV_X64_MSR_APIC_FREQUENCY) is available */
#define HV_X64_MSR_APIC_FREQUENCY_AVAILABLE (1 << 11)

/* TSC frequency MSR (HV_X64_MSR_TSC_FREQUENCY) is available */
#define HV_X64_MSR_TSC_FREQUENCY_AVAILABLE (1 << 11)

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

/*
 * Implementation recommendations. Indicates which behaviors the hypervisor
 * recommends the OS implement for optimal performance.
 */
 /*
  * Recommend using hypercall for address space switches rather
  * than MOV to CR3 instruction
  */
#define HV_X64_MWAIT_RECOMMENDED		(1 << 0)
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

/* MSR used to identify the guest OS. */
#define HV_X64_MSR_GUEST_OS_ID			0x40000000

/* MSR used to setup pages used to communicate with the hypervisor. */
#define HV_X64_MSR_HYPERCALL			0x40000001

/* MSR used to provide vcpu index */
#define HV_X64_MSR_VP_INDEX			0x40000002

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


#define HV_X64_MSR_HYPERCALL_ENABLE		0x00000001
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT) - 1))

/* Declare the various hypercall operations. */
#define HV_X64_HV_NOTIFY_LONG_SPIN_WAIT		0x0008

#define HV_X64_MSR_APIC_ASSIST_PAGE_ENABLE		0x00000001
#define HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_SHIFT) - 1))

#define HV_PROCESSOR_POWER_STATE_C0		0
#define HV_PROCESSOR_POWER_STATE_C1		1
#define HV_PROCESSOR_POWER_STATE_C2		2
#define HV_PROCESSOR_POWER_STATE_C3		3

/* hypercall status code */
#define HV_STATUS_SUCCESS			0
#define HV_STATUS_INVALID_HYPERCALL_CODE	2
#define HV_STATUS_INVALID_HYPERCALL_INPUT	3
#define HV_STATUS_INVALID_ALIGNMENT		4
#define HV_STATUS_INSUFFICIENT_BUFFERS		19

#endif

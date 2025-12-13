/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PERF_EVENT_H
#define _ASM_X86_PERF_EVENT_H

#include <linux/static_call.h>

/*
 * Performance event hw details:
 */

#define INTEL_PMC_MAX_GENERIC				       32
#define INTEL_PMC_MAX_FIXED				       16
#define INTEL_PMC_IDX_FIXED				       32

#define X86_PMC_IDX_MAX					       64

#define MSR_ARCH_PERFMON_PERFCTR0			      0xc1
#define MSR_ARCH_PERFMON_PERFCTR1			      0xc2

#define MSR_ARCH_PERFMON_EVENTSEL0			     0x186
#define MSR_ARCH_PERFMON_EVENTSEL1			     0x187

#define ARCH_PERFMON_EVENTSEL_EVENT			0x000000FFULL
#define ARCH_PERFMON_EVENTSEL_UMASK			0x0000FF00ULL
#define ARCH_PERFMON_EVENTSEL_USR			(1ULL << 16)
#define ARCH_PERFMON_EVENTSEL_OS			(1ULL << 17)
#define ARCH_PERFMON_EVENTSEL_EDGE			(1ULL << 18)
#define ARCH_PERFMON_EVENTSEL_PIN_CONTROL		(1ULL << 19)
#define ARCH_PERFMON_EVENTSEL_INT			(1ULL << 20)
#define ARCH_PERFMON_EVENTSEL_ANY			(1ULL << 21)
#define ARCH_PERFMON_EVENTSEL_ENABLE			(1ULL << 22)
#define ARCH_PERFMON_EVENTSEL_INV			(1ULL << 23)
#define ARCH_PERFMON_EVENTSEL_CMASK			0xFF000000ULL
#define ARCH_PERFMON_EVENTSEL_BR_CNTR			(1ULL << 35)
#define ARCH_PERFMON_EVENTSEL_EQ			(1ULL << 36)
#define ARCH_PERFMON_EVENTSEL_UMASK2			(0xFFULL << 40)

#define INTEL_FIXED_BITS_STRIDE			4
#define INTEL_FIXED_0_KERNEL				(1ULL << 0)
#define INTEL_FIXED_0_USER				(1ULL << 1)
#define INTEL_FIXED_0_ANYTHREAD			(1ULL << 2)
#define INTEL_FIXED_0_ENABLE_PMI			(1ULL << 3)
#define INTEL_FIXED_3_METRICS_CLEAR			(1ULL << 2)

#define HSW_IN_TX					(1ULL << 32)
#define HSW_IN_TX_CHECKPOINTED				(1ULL << 33)
#define ICL_EVENTSEL_ADAPTIVE				(1ULL << 34)
#define ICL_FIXED_0_ADAPTIVE				(1ULL << 32)

#define INTEL_FIXED_BITS_MASK					\
	(INTEL_FIXED_0_KERNEL | INTEL_FIXED_0_USER |		\
	 INTEL_FIXED_0_ANYTHREAD | INTEL_FIXED_0_ENABLE_PMI |	\
	 ICL_FIXED_0_ADAPTIVE)

#define intel_fixed_bits_by_idx(_idx, _bits)			\
	((_bits) << ((_idx) * INTEL_FIXED_BITS_STRIDE))

#define AMD64_EVENTSEL_INT_CORE_ENABLE			(1ULL << 36)
#define AMD64_EVENTSEL_GUESTONLY			(1ULL << 40)
#define AMD64_EVENTSEL_HOSTONLY				(1ULL << 41)

#define AMD64_EVENTSEL_INT_CORE_SEL_SHIFT		37
#define AMD64_EVENTSEL_INT_CORE_SEL_MASK		\
	(0xFULL << AMD64_EVENTSEL_INT_CORE_SEL_SHIFT)

#define AMD64_EVENTSEL_EVENT	\
	(ARCH_PERFMON_EVENTSEL_EVENT | (0x0FULL << 32))
#define INTEL_ARCH_EVENT_MASK	\
	(ARCH_PERFMON_EVENTSEL_UMASK | ARCH_PERFMON_EVENTSEL_EVENT)

#define AMD64_L3_SLICE_SHIFT				48
#define AMD64_L3_SLICE_MASK				\
	(0xFULL << AMD64_L3_SLICE_SHIFT)
#define AMD64_L3_SLICEID_MASK				\
	(0x7ULL << AMD64_L3_SLICE_SHIFT)

#define AMD64_L3_THREAD_SHIFT				56
#define AMD64_L3_THREAD_MASK				\
	(0xFFULL << AMD64_L3_THREAD_SHIFT)
#define AMD64_L3_F19H_THREAD_MASK			\
	(0x3ULL << AMD64_L3_THREAD_SHIFT)

#define AMD64_L3_EN_ALL_CORES				BIT_ULL(47)
#define AMD64_L3_EN_ALL_SLICES				BIT_ULL(46)

#define AMD64_L3_COREID_SHIFT				42
#define AMD64_L3_COREID_MASK				\
	(0x7ULL << AMD64_L3_COREID_SHIFT)

#define X86_RAW_EVENT_MASK		\
	(ARCH_PERFMON_EVENTSEL_EVENT |	\
	 ARCH_PERFMON_EVENTSEL_UMASK |	\
	 ARCH_PERFMON_EVENTSEL_EDGE  |	\
	 ARCH_PERFMON_EVENTSEL_INV   |	\
	 ARCH_PERFMON_EVENTSEL_CMASK)
#define X86_ALL_EVENT_FLAGS  			\
	(ARCH_PERFMON_EVENTSEL_EDGE |  		\
	 ARCH_PERFMON_EVENTSEL_INV | 		\
	 ARCH_PERFMON_EVENTSEL_CMASK | 		\
	 ARCH_PERFMON_EVENTSEL_ANY | 		\
	 ARCH_PERFMON_EVENTSEL_PIN_CONTROL | 	\
	 HSW_IN_TX | 				\
	 HSW_IN_TX_CHECKPOINTED)
#define AMD64_RAW_EVENT_MASK		\
	(X86_RAW_EVENT_MASK          |  \
	 AMD64_EVENTSEL_EVENT)
#define AMD64_RAW_EVENT_MASK_NB		\
	(AMD64_EVENTSEL_EVENT        |  \
	 ARCH_PERFMON_EVENTSEL_UMASK)

#define AMD64_PERFMON_V2_EVENTSEL_EVENT_NB	\
	(AMD64_EVENTSEL_EVENT	|		\
	 GENMASK_ULL(37, 36))

#define AMD64_PERFMON_V2_EVENTSEL_UMASK_NB	\
	(ARCH_PERFMON_EVENTSEL_UMASK	|	\
	 GENMASK_ULL(27, 24))

#define AMD64_PERFMON_V2_RAW_EVENT_MASK_NB		\
	(AMD64_PERFMON_V2_EVENTSEL_EVENT_NB	|	\
	 AMD64_PERFMON_V2_EVENTSEL_UMASK_NB)

#define AMD64_PERFMON_V2_ENABLE_UMC			BIT_ULL(31)
#define AMD64_PERFMON_V2_EVENTSEL_EVENT_UMC		GENMASK_ULL(7, 0)
#define AMD64_PERFMON_V2_EVENTSEL_RDWRMASK_UMC		GENMASK_ULL(9, 8)
#define AMD64_PERFMON_V2_RAW_EVENT_MASK_UMC		\
	(AMD64_PERFMON_V2_EVENTSEL_EVENT_UMC	|	\
	 AMD64_PERFMON_V2_EVENTSEL_RDWRMASK_UMC)

#define AMD64_NUM_COUNTERS				4
#define AMD64_NUM_COUNTERS_CORE				6
#define AMD64_NUM_COUNTERS_NB				4

#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_SEL		0x3c
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_UMASK		(0x00 << 8)
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX		0
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_PRESENT \
		(1 << (ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX))

#define ARCH_PERFMON_BRANCH_MISSES_RETIRED		6
#define ARCH_PERFMON_EVENTS_COUNT			7

#define PEBS_DATACFG_MEMINFO	BIT_ULL(0)
#define PEBS_DATACFG_GP	BIT_ULL(1)
#define PEBS_DATACFG_XMMS	BIT_ULL(2)
#define PEBS_DATACFG_LBRS	BIT_ULL(3)
#define PEBS_DATACFG_LBR_SHIFT	24
#define PEBS_DATACFG_CNTR	BIT_ULL(4)
#define PEBS_DATACFG_CNTR_SHIFT	32
#define PEBS_DATACFG_CNTR_MASK	GENMASK_ULL(15, 0)
#define PEBS_DATACFG_FIX_SHIFT	48
#define PEBS_DATACFG_FIX_MASK	GENMASK_ULL(7, 0)
#define PEBS_DATACFG_METRICS	BIT_ULL(5)

/* Steal the highest bit of pebs_data_cfg for SW usage */
#define PEBS_UPDATE_DS_SW	BIT_ULL(63)

/*
 * Intel "Architectural Performance Monitoring" CPUID
 * detection/enumeration details:
 */
union cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_counters:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};

union cpuid10_ebx {
	struct {
		unsigned int no_unhalted_core_cycles:1;
		unsigned int no_instructions_retired:1;
		unsigned int no_unhalted_reference_cycles:1;
		unsigned int no_llc_reference:1;
		unsigned int no_llc_misses:1;
		unsigned int no_branch_instruction_retired:1;
		unsigned int no_branch_misses_retired:1;
	} split;
	unsigned int full;
};

union cpuid10_edx {
	struct {
		unsigned int num_counters_fixed:5;
		unsigned int bit_width_fixed:8;
		unsigned int reserved1:2;
		unsigned int anythread_deprecated:1;
		unsigned int reserved2:16;
	} split;
	unsigned int full;
};

/*
 * Intel "Architectural Performance Monitoring extension" CPUID
 * detection/enumeration details:
 */
#define ARCH_PERFMON_EXT_LEAF			0x00000023
#define ARCH_PERFMON_NUM_COUNTER_LEAF		0x1
#define ARCH_PERFMON_ACR_LEAF			0x2

union cpuid35_eax {
	struct {
		unsigned int	leaf0:1;
		/* Counters Sub-Leaf */
		unsigned int    cntr_subleaf:1;
		/* Auto Counter Reload Sub-Leaf */
		unsigned int    acr_subleaf:1;
		/* Events Sub-Leaf */
		unsigned int    events_subleaf:1;
		unsigned int	reserved:28;
	} split;
	unsigned int            full;
};

union cpuid35_ebx {
	struct {
		/* UnitMask2 Supported */
		unsigned int    umask2:1;
		/* EQ-bit Supported */
		unsigned int    eq:1;
		unsigned int	reserved:30;
	} split;
	unsigned int            full;
};

/*
 * Intel Architectural LBR CPUID detection/enumeration details:
 */
union cpuid28_eax {
	struct {
		/* Supported LBR depth values */
		unsigned int	lbr_depth_mask:8;
		unsigned int	reserved:22;
		/* Deep C-state Reset */
		unsigned int	lbr_deep_c_reset:1;
		/* IP values contain LIP */
		unsigned int	lbr_lip:1;
	} split;
	unsigned int		full;
};

union cpuid28_ebx {
	struct {
		/* CPL Filtering Supported */
		unsigned int    lbr_cpl:1;
		/* Branch Filtering Supported */
		unsigned int    lbr_filter:1;
		/* Call-stack Mode Supported */
		unsigned int    lbr_call_stack:1;
	} split;
	unsigned int            full;
};

union cpuid28_ecx {
	struct {
		/* Mispredict Bit Supported */
		unsigned int    lbr_mispred:1;
		/* Timed LBRs Supported */
		unsigned int    lbr_timed_lbr:1;
		/* Branch Type Field Supported */
		unsigned int    lbr_br_type:1;
		unsigned int	reserved:13;
		/* Branch counters (Event Logging) Supported */
		unsigned int	lbr_counters:4;
	} split;
	unsigned int            full;
};

/*
 * AMD "Extended Performance Monitoring and Debug" CPUID
 * detection/enumeration details:
 */
union cpuid_0x80000022_ebx {
	struct {
		/* Number of Core Performance Counters */
		unsigned int	num_core_pmc:4;
		/* Number of available LBR Stack Entries */
		unsigned int	lbr_v2_stack_sz:6;
		/* Number of Data Fabric Counters */
		unsigned int	num_df_pmc:6;
		/* Number of Unified Memory Controller Counters */
		unsigned int	num_umc_pmc:6;
	} split;
	unsigned int		full;
};

struct x86_pmu_capability {
	int		version;
	int		num_counters_gp;
	int		num_counters_fixed;
	int		bit_width_gp;
	int		bit_width_fixed;
	unsigned int	events_mask;
	int		events_mask_len;
	unsigned int	pebs_ept	:1;
};

/*
 * Fixed-purpose performance events:
 */

/* RDPMC offset for Fixed PMCs */
#define INTEL_PMC_FIXED_RDPMC_BASE		(1 << 30)
#define INTEL_PMC_FIXED_RDPMC_METRICS		(1 << 29)

/*
 * All the fixed-mode PMCs are configured via this single MSR:
 */
#define MSR_ARCH_PERFMON_FIXED_CTR_CTRL	0x38d

/*
 * There is no event-code assigned to the fixed-mode PMCs.
 *
 * For a fixed-mode PMC, which has an equivalent event on a general-purpose
 * PMC, the event-code of the equivalent event is used for the fixed-mode PMC,
 * e.g., Instr_Retired.Any and CPU_CLK_Unhalted.Core.
 *
 * For a fixed-mode PMC, which doesn't have an equivalent event, a
 * pseudo-encoding is used, e.g., CPU_CLK_Unhalted.Ref and TOPDOWN.SLOTS.
 * The pseudo event-code for a fixed-mode PMC must be 0x00.
 * The pseudo umask-code is 0xX. The X equals the index of the fixed
 * counter + 1, e.g., the fixed counter 2 has the pseudo-encoding 0x0300.
 *
 * The counts are available in separate MSRs:
 */

/* Instr_Retired.Any: */
#define MSR_ARCH_PERFMON_FIXED_CTR0	0x309
#define INTEL_PMC_IDX_FIXED_INSTRUCTIONS	(INTEL_PMC_IDX_FIXED + 0)

/* CPU_CLK_Unhalted.Core: */
#define MSR_ARCH_PERFMON_FIXED_CTR1	0x30a
#define INTEL_PMC_IDX_FIXED_CPU_CYCLES	(INTEL_PMC_IDX_FIXED + 1)

/* CPU_CLK_Unhalted.Ref: event=0x00,umask=0x3 (pseudo-encoding) */
#define MSR_ARCH_PERFMON_FIXED_CTR2	0x30b
#define INTEL_PMC_IDX_FIXED_REF_CYCLES	(INTEL_PMC_IDX_FIXED + 2)
#define INTEL_PMC_MSK_FIXED_REF_CYCLES	(1ULL << INTEL_PMC_IDX_FIXED_REF_CYCLES)

/* TOPDOWN.SLOTS: event=0x00,umask=0x4 (pseudo-encoding) */
#define MSR_ARCH_PERFMON_FIXED_CTR3	0x30c
#define INTEL_PMC_IDX_FIXED_SLOTS	(INTEL_PMC_IDX_FIXED + 3)
#define INTEL_PMC_MSK_FIXED_SLOTS	(1ULL << INTEL_PMC_IDX_FIXED_SLOTS)

/* TOPDOWN_BAD_SPECULATION.ALL: fixed counter 4 (Atom only) */
/* TOPDOWN_FE_BOUND.ALL: fixed counter 5 (Atom only) */
/* TOPDOWN_RETIRING.ALL: fixed counter 6 (Atom only) */

static inline bool use_fixed_pseudo_encoding(u64 code)
{
	return !(code & 0xff);
}

/*
 * We model BTS tracing as another fixed-mode PMC.
 *
 * We choose the value 47 for the fixed index of BTS, since lower
 * values are used by actual fixed events and higher values are used
 * to indicate other overflow conditions in the PERF_GLOBAL_STATUS msr.
 */
#define INTEL_PMC_IDX_FIXED_BTS			(INTEL_PMC_IDX_FIXED + 15)

/*
 * The PERF_METRICS MSR is modeled as several magic fixed-mode PMCs, one for
 * each TopDown metric event.
 *
 * Internally the TopDown metric events are mapped to the FxCtr 3 (SLOTS).
 */
#define INTEL_PMC_IDX_METRIC_BASE		(INTEL_PMC_IDX_FIXED + 16)
#define INTEL_PMC_IDX_TD_RETIRING		(INTEL_PMC_IDX_METRIC_BASE + 0)
#define INTEL_PMC_IDX_TD_BAD_SPEC		(INTEL_PMC_IDX_METRIC_BASE + 1)
#define INTEL_PMC_IDX_TD_FE_BOUND		(INTEL_PMC_IDX_METRIC_BASE + 2)
#define INTEL_PMC_IDX_TD_BE_BOUND		(INTEL_PMC_IDX_METRIC_BASE + 3)
#define INTEL_PMC_IDX_TD_HEAVY_OPS		(INTEL_PMC_IDX_METRIC_BASE + 4)
#define INTEL_PMC_IDX_TD_BR_MISPREDICT		(INTEL_PMC_IDX_METRIC_BASE + 5)
#define INTEL_PMC_IDX_TD_FETCH_LAT		(INTEL_PMC_IDX_METRIC_BASE + 6)
#define INTEL_PMC_IDX_TD_MEM_BOUND		(INTEL_PMC_IDX_METRIC_BASE + 7)
#define INTEL_PMC_IDX_METRIC_END		INTEL_PMC_IDX_TD_MEM_BOUND
#define INTEL_PMC_MSK_TOPDOWN			((0xffull << INTEL_PMC_IDX_METRIC_BASE) | \
						INTEL_PMC_MSK_FIXED_SLOTS)

/*
 * There is no event-code assigned to the TopDown events.
 *
 * For the slots event, use the pseudo code of the fixed counter 3.
 *
 * For the metric events, the pseudo event-code is 0x00.
 * The pseudo umask-code starts from the middle of the pseudo event
 * space, 0x80.
 */
#define INTEL_TD_SLOTS				0x0400	/* TOPDOWN.SLOTS */
/* Level 1 metrics */
#define INTEL_TD_METRIC_RETIRING		0x8000	/* Retiring metric */
#define INTEL_TD_METRIC_BAD_SPEC		0x8100	/* Bad speculation metric */
#define INTEL_TD_METRIC_FE_BOUND		0x8200	/* FE bound metric */
#define INTEL_TD_METRIC_BE_BOUND		0x8300	/* BE bound metric */
/* Level 2 metrics */
#define INTEL_TD_METRIC_HEAVY_OPS		0x8400  /* Heavy Operations metric */
#define INTEL_TD_METRIC_BR_MISPREDICT		0x8500  /* Branch Mispredict metric */
#define INTEL_TD_METRIC_FETCH_LAT		0x8600  /* Fetch Latency metric */
#define INTEL_TD_METRIC_MEM_BOUND		0x8700  /* Memory bound metric */

#define INTEL_TD_METRIC_MAX			INTEL_TD_METRIC_MEM_BOUND
#define INTEL_TD_METRIC_NUM			8

#define INTEL_TD_CFG_METRIC_CLEAR_BIT		0
#define INTEL_TD_CFG_METRIC_CLEAR		BIT_ULL(INTEL_TD_CFG_METRIC_CLEAR_BIT)

static inline bool is_metric_idx(int idx)
{
	return (unsigned)(idx - INTEL_PMC_IDX_METRIC_BASE) < INTEL_TD_METRIC_NUM;
}

static inline bool is_topdown_idx(int idx)
{
	return is_metric_idx(idx) || idx == INTEL_PMC_IDX_FIXED_SLOTS;
}

#define INTEL_PMC_OTHER_TOPDOWN_BITS(bit)	\
			(~(0x1ull << bit) & INTEL_PMC_MSK_TOPDOWN)

#define GLOBAL_STATUS_COND_CHG			BIT_ULL(63)
#define GLOBAL_STATUS_BUFFER_OVF_BIT		62
#define GLOBAL_STATUS_BUFFER_OVF		BIT_ULL(GLOBAL_STATUS_BUFFER_OVF_BIT)
#define GLOBAL_STATUS_UNC_OVF			BIT_ULL(61)
#define GLOBAL_STATUS_ASIF			BIT_ULL(60)
#define GLOBAL_STATUS_COUNTERS_FROZEN		BIT_ULL(59)
#define GLOBAL_STATUS_LBRS_FROZEN_BIT		58
#define GLOBAL_STATUS_LBRS_FROZEN		BIT_ULL(GLOBAL_STATUS_LBRS_FROZEN_BIT)
#define GLOBAL_STATUS_TRACE_TOPAPMI_BIT		55
#define GLOBAL_STATUS_TRACE_TOPAPMI		BIT_ULL(GLOBAL_STATUS_TRACE_TOPAPMI_BIT)
#define GLOBAL_STATUS_PERF_METRICS_OVF_BIT	48

#define GLOBAL_CTRL_EN_PERF_METRICS		BIT_ULL(48)
/*
 * We model guest LBR event tracing as another fixed-mode PMC like BTS.
 *
 * We choose bit 58 because it's used to indicate LBR stack frozen state
 * for architectural perfmon v4, also we unconditionally mask that bit in
 * the handle_pmi_common(), so it'll never be set in the overflow handling.
 *
 * With this fake counter assigned, the guest LBR event user (such as KVM),
 * can program the LBR registers on its own, and we don't actually do anything
 * with then in the host context.
 */
#define INTEL_PMC_IDX_FIXED_VLBR	(GLOBAL_STATUS_LBRS_FROZEN_BIT)

/*
 * Pseudo-encoding the guest LBR event as event=0x00,umask=0x1b,
 * since it would claim bit 58 which is effectively Fixed26.
 */
#define INTEL_FIXED_VLBR_EVENT	0x1b00

/*
 * Adaptive PEBS v4
 */

struct pebs_basic {
	u64 format_group:32,
	    retire_latency:16,
	    format_size:16;
	u64 ip;
	u64 applicable_counters;
	u64 tsc;
};

struct pebs_meminfo {
	u64 address;
	u64 aux;
	union {
		/* pre Alder Lake */
		u64 mem_latency;
		/* Alder Lake and later */
		struct {
			u64 instr_latency:16;
			u64 pad2:16;
			u64 cache_latency:16;
			u64 pad3:16;
		};
	};
	u64 tsx_tuning;
};

struct pebs_gprs {
	u64 flags, ip, ax, cx, dx, bx, sp, bp, si, di;
	u64 r8, r9, r10, r11, r12, r13, r14, r15;
};

struct pebs_xmm {
	u64 xmm[16*2];	/* two entries for each register */
};

struct pebs_cntr_header {
	u32 cntr;
	u32 fixed;
	u32 metrics;
	u32 reserved;
};

#define INTEL_CNTR_METRICS		0x3

/*
 * AMD Extended Performance Monitoring and Debug cpuid feature detection
 */
#define EXT_PERFMON_DEBUG_FEATURES		0x80000022

/*
 * IBS cpuid feature detection
 */

#define IBS_CPUID_FEATURES		0x8000001b

/*
 * Same bit mask as for IBS cpuid feature flags (Fn8000_001B_EAX), but
 * bit 0 is used to indicate the existence of IBS.
 */
#define IBS_CAPS_AVAIL			(1U<<0)
#define IBS_CAPS_FETCHSAM		(1U<<1)
#define IBS_CAPS_OPSAM			(1U<<2)
#define IBS_CAPS_RDWROPCNT		(1U<<3)
#define IBS_CAPS_OPCNT			(1U<<4)
#define IBS_CAPS_BRNTRGT		(1U<<5)
#define IBS_CAPS_OPCNTEXT		(1U<<6)
#define IBS_CAPS_RIPINVALIDCHK		(1U<<7)
#define IBS_CAPS_OPBRNFUSE		(1U<<8)
#define IBS_CAPS_FETCHCTLEXTD		(1U<<9)
#define IBS_CAPS_OPDATA4		(1U<<10)
#define IBS_CAPS_ZEN4			(1U<<11)
#define IBS_CAPS_OPLDLAT		(1U<<12)
#define IBS_CAPS_OPDTLBPGSIZE		(1U<<19)

#define IBS_CAPS_DEFAULT		(IBS_CAPS_AVAIL		\
					 | IBS_CAPS_FETCHSAM	\
					 | IBS_CAPS_OPSAM)

/*
 * IBS APIC setup
 */
#define IBSCTL				0x1cc
#define IBSCTL_LVT_OFFSET_VALID		(1ULL<<8)
#define IBSCTL_LVT_OFFSET_MASK		0x0F

/* IBS fetch bits/masks */
#define IBS_FETCH_L3MISSONLY	(1ULL<<59)
#define IBS_FETCH_RAND_EN	(1ULL<<57)
#define IBS_FETCH_VAL		(1ULL<<49)
#define IBS_FETCH_ENABLE	(1ULL<<48)
#define IBS_FETCH_CNT		0xFFFF0000ULL
#define IBS_FETCH_MAX_CNT	0x0000FFFFULL

/*
 * IBS op bits/masks
 * The lower 7 bits of the current count are random bits
 * preloaded by hardware and ignored in software
 */
#define IBS_OP_LDLAT_EN		(1ULL<<63)
#define IBS_OP_LDLAT_THRSH	(0xFULL<<59)
#define IBS_OP_CUR_CNT		(0xFFF80ULL<<32)
#define IBS_OP_CUR_CNT_RAND	(0x0007FULL<<32)
#define IBS_OP_CUR_CNT_EXT_MASK	(0x7FULL<<52)
#define IBS_OP_CNT_CTL		(1ULL<<19)
#define IBS_OP_VAL		(1ULL<<18)
#define IBS_OP_ENABLE		(1ULL<<17)
#define IBS_OP_L3MISSONLY	(1ULL<<16)
#define IBS_OP_MAX_CNT		0x0000FFFFULL
#define IBS_OP_MAX_CNT_EXT	0x007FFFFFULL	/* not a register bit mask */
#define IBS_OP_MAX_CNT_EXT_MASK	(0x7FULL<<20)	/* separate upper 7 bits */
#define IBS_RIP_INVALID		(1ULL<<38)

#ifdef CONFIG_X86_LOCAL_APIC
extern u32 get_ibs_caps(void);
extern int forward_event_to_ibs(struct perf_event *event);
#else
static inline u32 get_ibs_caps(void) { return 0; }
static inline int forward_event_to_ibs(struct perf_event *event) { return -ENOENT; }
#endif

#ifdef CONFIG_PERF_EVENTS
extern void perf_events_lapic_init(void);

/*
 * Abuse bits {3,5} of the cpu eflags register. These flags are otherwise
 * unused and ABI specified to be 0, so nobody should care what we do with
 * them.
 *
 * EXACT - the IP points to the exact instruction that triggered the
 *         event (HW bugs exempt).
 * VM    - original X86_VM_MASK; see set_linear_ip().
 */
#define PERF_EFLAGS_EXACT	(1UL << 3)
#define PERF_EFLAGS_VM		(1UL << 5)

struct pt_regs;
struct x86_perf_regs {
	struct pt_regs	regs;
	u64		*xmm_regs;
};

extern unsigned long perf_arch_instruction_pointer(struct pt_regs *regs);
extern unsigned long perf_arch_misc_flags(struct pt_regs *regs);
extern unsigned long perf_arch_guest_misc_flags(struct pt_regs *regs);
#define perf_arch_misc_flags(regs)	perf_arch_misc_flags(regs)
#define perf_arch_guest_misc_flags(regs)	perf_arch_guest_misc_flags(regs)

#include <asm/stacktrace.h>

/*
 * We abuse bit 3 from flags to pass exact information, see
 * perf_arch_misc_flags() and the comment with PERF_EFLAGS_EXACT.
 */
#define perf_arch_fetch_caller_regs(regs, __ip)		{	\
	(regs)->ip = (__ip);					\
	(regs)->sp = (unsigned long)__builtin_frame_address(0);	\
	(regs)->cs = __KERNEL_CS;				\
	regs->flags = 0;					\
}

struct perf_guest_switch_msr {
	unsigned msr;
	u64 host, guest;
};

struct x86_pmu_lbr {
	unsigned int	nr;
	unsigned int	from;
	unsigned int	to;
	unsigned int	info;
	bool		has_callstack;
};

extern void perf_get_x86_pmu_capability(struct x86_pmu_capability *cap);
extern u64 perf_get_hw_event_config(int hw_event);
extern void perf_check_microcode(void);
extern void perf_clear_dirty_counters(void);
extern int x86_perf_rdpmc_index(struct perf_event *event);
#else
static inline void perf_get_x86_pmu_capability(struct x86_pmu_capability *cap)
{
	memset(cap, 0, sizeof(*cap));
}

static inline u64 perf_get_hw_event_config(int hw_event)
{
	return 0;
}

static inline void perf_events_lapic_init(void)	{ }
static inline void perf_check_microcode(void) { }
#endif

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_INTEL)
extern struct perf_guest_switch_msr *perf_guest_get_msrs(int *nr, void *data);
extern void x86_perf_get_lbr(struct x86_pmu_lbr *lbr);
#else
struct perf_guest_switch_msr *perf_guest_get_msrs(int *nr, void *data);
static inline void x86_perf_get_lbr(struct x86_pmu_lbr *lbr)
{
	memset(lbr, 0, sizeof(*lbr));
}
#endif

#ifdef CONFIG_CPU_SUP_INTEL
 extern void intel_pt_handle_vmx(int on);
#else
static inline void intel_pt_handle_vmx(int on)
{

}
#endif

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD)
 extern void amd_pmu_enable_virt(void);
 extern void amd_pmu_disable_virt(void);

#if defined(CONFIG_PERF_EVENTS_AMD_BRS)

#define PERF_NEEDS_LOPWR_CB 1

/*
 * architectural low power callback impacts
 * drivers/acpi/processor_idle.c
 * drivers/acpi/acpi_pad.c
 */
extern void perf_amd_brs_lopwr_cb(bool lopwr_in);

DECLARE_STATIC_CALL(perf_lopwr_cb, perf_amd_brs_lopwr_cb);

static __always_inline void perf_lopwr_cb(bool lopwr_in)
{
	static_call_mod(perf_lopwr_cb)(lopwr_in);
}

#endif /* PERF_NEEDS_LOPWR_CB */

#else
 static inline void amd_pmu_enable_virt(void) { }
 static inline void amd_pmu_disable_virt(void) { }
#endif

#define arch_perf_out_copy_user copy_from_user_nmi

#endif /* _ASM_X86_PERF_EVENT_H */

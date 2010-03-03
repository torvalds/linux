#ifndef _ASM_X86_PERF_EVENT_H
#define _ASM_X86_PERF_EVENT_H

/*
 * Performance event hw details:
 */

#define X86_PMC_MAX_GENERIC					8
#define X86_PMC_MAX_FIXED					3

#define X86_PMC_IDX_GENERIC				        0
#define X86_PMC_IDX_FIXED				       32
#define X86_PMC_IDX_MAX					       64

#define MSR_ARCH_PERFMON_PERFCTR0			      0xc1
#define MSR_ARCH_PERFMON_PERFCTR1			      0xc2

#define MSR_ARCH_PERFMON_EVENTSEL0			     0x186
#define MSR_ARCH_PERFMON_EVENTSEL1			     0x187

#define ARCH_PERFMON_EVENTSEL_ENABLE			  (1 << 22)
#define ARCH_PERFMON_EVENTSEL_ANY			  (1 << 21)
#define ARCH_PERFMON_EVENTSEL_INT			  (1 << 20)
#define ARCH_PERFMON_EVENTSEL_OS			  (1 << 17)
#define ARCH_PERFMON_EVENTSEL_USR			  (1 << 16)

/*
 * Includes eventsel and unit mask as well:
 */


#define INTEL_ARCH_EVTSEL_MASK		0x000000FFULL
#define INTEL_ARCH_UNIT_MASK		0x0000FF00ULL
#define INTEL_ARCH_EDGE_MASK		0x00040000ULL
#define INTEL_ARCH_INV_MASK		0x00800000ULL
#define INTEL_ARCH_CNT_MASK		0xFF000000ULL
#define INTEL_ARCH_EVENT_MASK	(INTEL_ARCH_UNIT_MASK|INTEL_ARCH_EVTSEL_MASK)

/*
 * filter mask to validate fixed counter events.
 * the following filters disqualify for fixed counters:
 *  - inv
 *  - edge
 *  - cnt-mask
 *  The other filters are supported by fixed counters.
 *  The any-thread option is supported starting with v3.
 */
#define INTEL_ARCH_FIXED_MASK \
	(INTEL_ARCH_CNT_MASK| \
	 INTEL_ARCH_INV_MASK| \
	 INTEL_ARCH_EDGE_MASK|\
	 INTEL_ARCH_UNIT_MASK|\
	 INTEL_ARCH_EVENT_MASK)

#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_SEL		      0x3c
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_UMASK		(0x00 << 8)
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX			 0
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_PRESENT \
		(1 << (ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX))

#define ARCH_PERFMON_BRANCH_MISSES_RETIRED			 6

/*
 * Intel "Architectural Performance Monitoring" CPUID
 * detection/enumeration details:
 */
union cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_events:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};

union cpuid10_edx {
	struct {
		unsigned int num_events_fixed:4;
		unsigned int reserved:28;
	} split;
	unsigned int full;
};


/*
 * Fixed-purpose performance events:
 */

/*
 * All 3 fixed-mode PMCs are configured via this single MSR:
 */
#define MSR_ARCH_PERFMON_FIXED_CTR_CTRL			0x38d

/*
 * The counts are available in three separate MSRs:
 */

/* Instr_Retired.Any: */
#define MSR_ARCH_PERFMON_FIXED_CTR0			0x309
#define X86_PMC_IDX_FIXED_INSTRUCTIONS			(X86_PMC_IDX_FIXED + 0)

/* CPU_CLK_Unhalted.Core: */
#define MSR_ARCH_PERFMON_FIXED_CTR1			0x30a
#define X86_PMC_IDX_FIXED_CPU_CYCLES			(X86_PMC_IDX_FIXED + 1)

/* CPU_CLK_Unhalted.Ref: */
#define MSR_ARCH_PERFMON_FIXED_CTR2			0x30b
#define X86_PMC_IDX_FIXED_BUS_CYCLES			(X86_PMC_IDX_FIXED + 2)

/*
 * We model BTS tracing as another fixed-mode PMC.
 *
 * We choose a value in the middle of the fixed event range, since lower
 * values are used by actual fixed events and higher values are used
 * to indicate other overflow conditions in the PERF_GLOBAL_STATUS msr.
 */
#define X86_PMC_IDX_FIXED_BTS				(X86_PMC_IDX_FIXED + 16)

/* IbsFetchCtl bits/masks */
#define IBS_FETCH_RAND_EN		(1ULL<<57)
#define IBS_FETCH_VAL			(1ULL<<49)
#define IBS_FETCH_ENABLE		(1ULL<<48)
#define IBS_FETCH_CNT			0xFFFF0000ULL
#define IBS_FETCH_MAX_CNT		0x0000FFFFULL

/* IbsOpCtl bits */
#define IBS_OP_CNT_CTL			(1ULL<<19)
#define IBS_OP_VAL			(1ULL<<18)
#define IBS_OP_ENABLE			(1ULL<<17)
#define IBS_OP_MAX_CNT			0x0000FFFFULL

#ifdef CONFIG_PERF_EVENTS
extern void init_hw_perf_events(void);
extern void perf_events_lapic_init(void);

#define PERF_EVENT_INDEX_OFFSET			0

/*
 * Abuse bit 3 of the cpu eflags register to indicate proper PEBS IP fixups.
 * This flag is otherwise unused and ABI specified to be 0, so nobody should
 * care what we do with it.
 */
#define PERF_EFLAGS_EXACT	(1UL << 3)

#define perf_misc_flags(regs)				\
({	int misc = 0;					\
	if (user_mode(regs))				\
		misc |= PERF_RECORD_MISC_USER;		\
	else						\
		misc |= PERF_RECORD_MISC_KERNEL;	\
	if (regs->flags & PERF_EFLAGS_EXACT)		\
		misc |= PERF_RECORD_MISC_EXACT;		\
	misc; })

#define perf_instruction_pointer(regs)	((regs)->ip)

#else
static inline void init_hw_perf_events(void)		{ }
static inline void perf_events_lapic_init(void)	{ }
#endif

#endif /* _ASM_X86_PERF_EVENT_H */

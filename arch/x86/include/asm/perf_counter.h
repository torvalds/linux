#ifndef _ASM_X86_PERF_COUNTER_H
#define _ASM_X86_PERF_COUNTER_H

/*
 * Performance counter hw details:
 */

#define X86_PMC_MAX_GENERIC					8
#define X86_PMC_MAX_FIXED					3

#define MSR_ARCH_PERFMON_PERFCTR0			      0xc1
#define MSR_ARCH_PERFMON_PERFCTR1			      0xc2

#define MSR_ARCH_PERFMON_EVENTSEL0			     0x186
#define MSR_ARCH_PERFMON_EVENTSEL1			     0x187

#define ARCH_PERFMON_EVENTSEL0_ENABLE			  (1 << 22)
#define ARCH_PERFMON_EVENTSEL_INT			  (1 << 20)
#define ARCH_PERFMON_EVENTSEL_OS			  (1 << 17)
#define ARCH_PERFMON_EVENTSEL_USR			  (1 << 16)

#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_SEL		      0x3c
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_UMASK		(0x00 << 8)
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX 		 0
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
		unsigned int num_counters:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};

union cpuid10_edx {
	struct {
		unsigned int num_counters_fixed:4;
		unsigned int reserved:28;
	} split;
	unsigned int full;
};


/*
 * Fixed-purpose performance counters:
 */

/* Instr_Retired.Any: */
#define MSR_ARCH_PERFMON_FIXED_CTR0			0x309

/* CPU_CLK_Unhalted.Core: */
#define MSR_ARCH_PERFMON_FIXED_CTR1			0x30a

/* CPU_CLK_Unhalted.Ref: */
#define MSR_ARCH_PERFMON_FIXED_CTR2			0x30b


#ifdef CONFIG_PERF_COUNTERS
extern void init_hw_perf_counters(void);
extern void perf_counters_lapic_init(int nmi);
#else
static inline void init_hw_perf_counters(void)		{ }
static inline void perf_counters_lapic_init(int nmi)	{ }
#endif

#endif /* _ASM_X86_PERF_COUNTER_H */

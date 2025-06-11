/* SPDX-License-Identifier: GPL-2.0 */
/*
 * x86 TSC related functions
 */
#ifndef _ASM_X86_TSC_H
#define _ASM_X86_TSC_H

#include <asm/asm.h>
#include <asm/cpufeature.h>
#include <asm/processor.h>
#include <asm/msr.h>

/**
 * rdtsc() - returns the current TSC without ordering constraints
 *
 * rdtsc() returns the result of RDTSC as a 64-bit integer.  The
 * only ordering constraint it supplies is the ordering implied by
 * "asm volatile": it will put the RDTSC in the place you expect.  The
 * CPU can and will speculatively execute that RDTSC, though, so the
 * results can be non-monotonic if compared on different CPUs.
 */
static __always_inline u64 rdtsc(void)
{
	EAX_EDX_DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high);
}

/**
 * rdtsc_ordered() - read the current TSC in program order
 *
 * rdtsc_ordered() returns the result of RDTSC as a 64-bit integer.
 * It is ordered like a load to a global in-memory counter.  It should
 * be impossible to observe non-monotonic rdtsc_unordered() behavior
 * across multiple CPUs as long as the TSC is synced.
 */
static __always_inline u64 rdtsc_ordered(void)
{
	EAX_EDX_DECLARE_ARGS(val, low, high);

	/*
	 * The RDTSC instruction is not ordered relative to memory
	 * access.  The Intel SDM and the AMD APM are both vague on this
	 * point, but empirically an RDTSC instruction can be
	 * speculatively executed before prior loads.  An RDTSC
	 * immediately after an appropriate barrier appears to be
	 * ordered as a normal load, that is, it provides the same
	 * ordering guarantees as reading from a global memory location
	 * that some other imaginary CPU is updating continuously with a
	 * time stamp.
	 *
	 * Thus, use the preferred barrier on the respective CPU, aiming for
	 * RDTSCP as the default.
	 */
	asm volatile(ALTERNATIVE_2("rdtsc",
				   "lfence; rdtsc", X86_FEATURE_LFENCE_RDTSC,
				   "rdtscp", X86_FEATURE_RDTSCP)
			: EAX_EDX_RET(val, low, high)
			/* RDTSCP clobbers ECX with MSR_TSC_AUX. */
			:: "ecx");

	return EAX_EDX_VAL(val, low, high);
}

/*
 * Standard way to access the cycle counter.
 */
typedef unsigned long long cycles_t;

extern unsigned int cpu_khz;
extern unsigned int tsc_khz;

extern void disable_TSC(void);

static inline cycles_t get_cycles(void)
{
	if (!IS_ENABLED(CONFIG_X86_TSC) &&
	    !cpu_feature_enabled(X86_FEATURE_TSC))
		return 0;
	return rdtsc();
}
#define get_cycles get_cycles

extern void tsc_early_init(void);
extern void tsc_init(void);
extern void mark_tsc_unstable(char *reason);
extern int unsynchronized_tsc(void);
extern int check_tsc_unstable(void);
extern void mark_tsc_async_resets(char *reason);
extern unsigned long native_calibrate_cpu_early(void);
extern unsigned long native_calibrate_tsc(void);
extern unsigned long long native_sched_clock_from_tsc(u64 tsc);

extern int tsc_clocksource_reliable;
#ifdef CONFIG_X86_TSC
extern bool tsc_async_resets;
#else
# define tsc_async_resets	false
#endif

/*
 * Boot-time check whether the TSCs are synchronized across
 * all CPUs/cores:
 */
#ifdef CONFIG_X86_TSC
extern bool tsc_store_and_check_tsc_adjust(bool bootcpu);
extern void tsc_verify_tsc_adjust(bool resume);
extern void check_tsc_sync_target(void);
#else
static inline bool tsc_store_and_check_tsc_adjust(bool bootcpu) { return false; }
static inline void tsc_verify_tsc_adjust(bool resume) { }
static inline void check_tsc_sync_target(void) { }
#endif

extern int notsc_setup(char *);
extern void tsc_save_sched_clock_state(void);
extern void tsc_restore_sched_clock_state(void);

unsigned long cpu_khz_from_msr(void);

#endif /* _ASM_X86_TSC_H */

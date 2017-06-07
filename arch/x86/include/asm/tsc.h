/*
 * x86 TSC related functions
 */
#ifndef _ASM_X86_TSC_H
#define _ASM_X86_TSC_H

#include <asm/processor.h>

#define NS_SCALE	10 /* 2^10, carefully chosen */
#define US_SCALE	32 /* 2^32, arbitralrily chosen */

/*
 * Standard way to access the cycle counter.
 */
typedef unsigned long long cycles_t;

extern unsigned int cpu_khz;
extern unsigned int tsc_khz;

extern void disable_TSC(void);

static inline cycles_t get_cycles(void)
{
#ifndef CONFIG_X86_TSC
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return 0;
#endif

	return rdtsc();
}

extern struct system_counterval_t convert_art_to_tsc(u64 art);

extern void tsc_init(void);
extern void mark_tsc_unstable(char *reason);
extern int unsynchronized_tsc(void);
extern int check_tsc_unstable(void);
extern unsigned long native_calibrate_cpu(void);
extern unsigned long native_calibrate_tsc(void);
extern unsigned long long native_sched_clock_from_tsc(u64 tsc);

extern int tsc_clocksource_reliable;

/*
 * Boot-time check whether the TSCs are synchronized across
 * all CPUs/cores:
 */
#ifdef CONFIG_X86_TSC
extern bool tsc_store_and_check_tsc_adjust(bool bootcpu);
extern void tsc_verify_tsc_adjust(bool resume);
extern void check_tsc_sync_source(int cpu);
extern void check_tsc_sync_target(void);
#else
static inline bool tsc_store_and_check_tsc_adjust(bool bootcpu) { return false; }
static inline void tsc_verify_tsc_adjust(bool resume) { }
static inline void check_tsc_sync_source(int cpu) { }
static inline void check_tsc_sync_target(void) { }
#endif

extern int notsc_setup(char *);
extern void tsc_save_sched_clock_state(void);
extern void tsc_restore_sched_clock_state(void);

unsigned long cpu_khz_from_msr(void);

#endif /* _ASM_X86_TSC_H */

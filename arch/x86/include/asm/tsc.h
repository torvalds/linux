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
	unsigned long long ret = 0;

#ifndef CONFIG_X86_TSC
	if (!cpu_has_tsc)
		return 0;
#endif
	rdtscll(ret);

	return ret;
}

static __always_inline cycles_t vget_cycles(void)
{
	/*
	 * We only do VDSOs on TSC capable CPUs, so this shouldnt
	 * access boot_cpu_data (which is not VDSO-safe):
	 */
#ifndef CONFIG_X86_TSC
	if (!cpu_has_tsc)
		return 0;
#endif
	return (cycles_t)__native_read_tsc();
}

extern void tsc_init(void);
extern void mark_tsc_unstable(char *reason);
extern int unsynchronized_tsc(void);
extern int check_tsc_unstable(void);
extern unsigned long native_calibrate_tsc(void);

/*
 * Boot-time check whether the TSCs are synchronized across
 * all CPUs/cores:
 */
extern void check_tsc_sync_source(int cpu);
extern void check_tsc_sync_target(void);

extern int notsc_setup(char *);

#endif /* _ASM_X86_TSC_H */

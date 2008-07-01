#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>

unsigned int cpu_khz;           /* TSC clocks / usec, not used here */
EXPORT_SYMBOL(cpu_khz);
unsigned int tsc_khz;
EXPORT_SYMBOL(tsc_khz);

/*
 * TSC can be unstable due to cpufreq or due to unsynced TSCs
 */
int tsc_unstable;

/* native_sched_clock() is called before tsc_init(), so
   we must start with the TSC soft disabled to prevent
   erroneous rdtsc usage on !cpu_has_tsc processors */
int tsc_disabled = -1;

/*
 * Scheduler clock - returns current time in nanosec units.
 */
u64 native_sched_clock(void)
{
	u64 this_offset;

	/*
	 * Fall back to jiffies if there's no TSC available:
	 * ( But note that we still use it if the TSC is marked
	 *   unstable. We do this because unlike Time Of Day,
	 *   the scheduler clock tolerates small errors and it's
	 *   very important for it to be as fast as the platform
	 *   can achive it. )
	 */
	if (unlikely(tsc_disabled)) {
		/* No locking but a rare wrong value is not a big deal: */
		return (jiffies_64 - INITIAL_JIFFIES) * (1000000000 / HZ);
	}

	/* read the Time Stamp Counter: */
	rdtscll(this_offset);

	/* return the value in ns */
	return cycles_2_ns(this_offset);
}

/* We need to define a real function for sched_clock, to override the
   weak default version */
#ifdef CONFIG_PARAVIRT
unsigned long long sched_clock(void)
{
	return paravirt_sched_clock();
}
#else
unsigned long long
sched_clock(void) __attribute__((alias("native_sched_clock")));
#endif

int check_tsc_unstable(void)
{
	return tsc_unstable;
}
EXPORT_SYMBOL_GPL(check_tsc_unstable);

#ifdef CONFIG_X86_TSC
int __init notsc_setup(char *str)
{
	printk(KERN_WARNING "notsc: Kernel compiled with CONFIG_X86_TSC, "
			"cannot disable TSC completely.\n");
	tsc_disabled = 1;
	return 1;
}
#else
/*
 * disable flag for tsc. Takes effect by clearing the TSC cpu flag
 * in cpu/common.c
 */
int __init notsc_setup(char *str)
{
	setup_clear_cpu_cap(X86_FEATURE_TSC);
	return 1;
}
#endif

__setup("notsc", notsc_setup);

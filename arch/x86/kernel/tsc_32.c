#include <linux/sched.h>
#include <linux/clocksource.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/cpufreq.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/percpu.h>

#include <asm/delay.h>
#include <asm/tsc.h>
#include <asm/io.h>
#include <asm/timer.h>

#include "mach_timer.h"

extern int tsc_unstable;
extern int tsc_disabled;

/* clock source code */

static struct clocksource clocksource_tsc;

/*
 * We compare the TSC to the cycle_last value in the clocksource
 * structure to avoid a nasty time-warp issue. This can be observed in
 * a very small window right after one CPU updated cycle_last under
 * xtime lock and the other CPU reads a TSC value which is smaller
 * than the cycle_last reference value due to a TSC which is slighty
 * behind. This delta is nowhere else observable, but in that case it
 * results in a forward time jump in the range of hours due to the
 * unsigned delta calculation of the time keeping core code, which is
 * necessary to support wrapping clocksources like pm timer.
 */
static cycle_t read_tsc(void)
{
	cycle_t ret;

	rdtscll(ret);

	return ret >= clocksource_tsc.cycle_last ?
		ret : clocksource_tsc.cycle_last;
}

static struct clocksource clocksource_tsc = {
	.name			= "tsc",
	.rating			= 300,
	.read			= read_tsc,
	.mask			= CLOCKSOURCE_MASK(64),
	.mult			= 0, /* to be set */
	.shift			= 22,
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS |
				  CLOCK_SOURCE_MUST_VERIFY,
};

void mark_tsc_unstable(char *reason)
{
	if (!tsc_unstable) {
		tsc_unstable = 1;
		printk("Marking TSC unstable due to: %s.\n", reason);
		/* Can be called before registration */
		if (clocksource_tsc.mult)
			clocksource_change_rating(&clocksource_tsc, 0);
		else
			clocksource_tsc.rating = 0;
	}
}
EXPORT_SYMBOL_GPL(mark_tsc_unstable);

static int __init dmi_mark_tsc_unstable(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE "%s detected: marking TSC unstable.\n",
	       d->ident);
	tsc_unstable = 1;
	return 0;
}

/* List of systems that have known TSC problems */
static struct dmi_system_id __initdata bad_tsc_dmi_table[] = {
	{
	 .callback = dmi_mark_tsc_unstable,
	 .ident = "IBM Thinkpad 380XD",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "2635FA0"),
		     },
	 },
	 {}
};

/*
 * Make an educated guess if the TSC is trustworthy and synchronized
 * over all CPUs.
 */
__cpuinit int unsynchronized_tsc(void)
{
	if (!cpu_has_tsc || tsc_unstable)
		return 1;

	/* Anything with constant TSC should be synchronized */
	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		return 0;

	/*
	 * Intel systems are normally all synchronized.
	 * Exceptions must mark TSC as unstable:
	 */
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		/* assume multi socket systems are not synchronized: */
		if (num_possible_cpus() > 1)
			tsc_unstable = 1;
	}
	return tsc_unstable;
}

/*
 * Geode_LX - the OLPC CPU has a possibly a very reliable TSC
 */
#ifdef CONFIG_MGEODE_LX
/* RTSC counts during suspend */
#define RTSC_SUSP 0x100

static void __init check_geode_tsc_reliable(void)
{
	unsigned long res_low, res_high;

	rdmsr_safe(MSR_GEODE_BUSCONT_CONF0, &res_low, &res_high);
	if (res_low & RTSC_SUSP)
		clocksource_tsc.flags &= ~CLOCK_SOURCE_MUST_VERIFY;
}
#else
static inline void check_geode_tsc_reliable(void) { }
#endif


void __init tsc_init(void)
{
	int cpu;
	u64 lpj;

	if (!cpu_has_tsc || tsc_disabled > 0)
		return;

	cpu_khz = calculate_cpu_khz();
	tsc_khz = cpu_khz;

	if (!cpu_khz) {
		mark_tsc_unstable("could not calculate TSC khz");
		return;
	}

	lpj = ((u64)tsc_khz * 1000);
	do_div(lpj, HZ);
	lpj_fine = lpj;

	/* now allow native_sched_clock() to use rdtsc */
	tsc_disabled = 0;

	printk("Detected %lu.%03lu MHz processor.\n",
				(unsigned long)cpu_khz / 1000,
				(unsigned long)cpu_khz % 1000);

	/*
	 * Secondary CPUs do not run through tsc_init(), so set up
	 * all the scale factors for all CPUs, assuming the same
	 * speed as the bootup CPU. (cpufreq notifiers will fix this
	 * up if their speed diverges)
	 */
	for_each_possible_cpu(cpu)
		set_cyc2ns_scale(cpu_khz, cpu);

	use_tsc_delay();

	/* Check and install the TSC clocksource */
	dmi_check_system(bad_tsc_dmi_table);

	unsynchronized_tsc();
	check_geode_tsc_reliable();
	clocksource_tsc.mult = clocksource_khz2mult(tsc_khz,
						    clocksource_tsc.shift);
	/* lower the rating if we already know its unstable: */
	if (check_tsc_unstable()) {
		clocksource_tsc.rating = 0;
		clocksource_tsc.flags &= ~CLOCK_SOURCE_IS_CONTINUOUS;
	}
	clocksource_register(&clocksource_tsc);
}

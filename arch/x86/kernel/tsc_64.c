#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/time.h>
#include <linux/acpi.h>
#include <linux/cpufreq.h>
#include <linux/acpi_pmtmr.h>

#include <asm/hpet.h>
#include <asm/timex.h>
#include <asm/timer.h>
#include <asm/vgtod.h>

extern int tsc_unstable;
extern int tsc_disabled;

/*
 * Make an educated guess if the TSC is trustworthy and synchronized
 * over all CPUs.
 */
__cpuinit int unsynchronized_tsc(void)
{
	if (tsc_unstable)
		return 1;

#ifdef CONFIG_SMP
	if (apic_is_clustered_box())
		return 1;
#endif

	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		return 0;

	/* Assume multi socket systems are not synchronized */
	return num_present_cpus() > 1;
}

static struct clocksource clocksource_tsc;

/*
 * We compare the TSC to the cycle_last value in the clocksource
 * structure to avoid a nasty time-warp. This can be observed in a
 * very small window right after one CPU updated cycle_last under
 * xtime/vsyscall_gtod lock and the other CPU reads a TSC value which
 * is smaller than the cycle_last reference value due to a TSC which
 * is slighty behind. This delta is nowhere else observable, but in
 * that case it results in a forward time jump in the range of hours
 * due to the unsigned delta calculation of the time keeping core
 * code, which is necessary to support wrapping clocksources like pm
 * timer.
 */
static cycle_t read_tsc(void)
{
	cycle_t ret = (cycle_t)get_cycles();

	return ret >= clocksource_tsc.cycle_last ?
		ret : clocksource_tsc.cycle_last;
}

static cycle_t __vsyscall_fn vread_tsc(void)
{
	cycle_t ret = (cycle_t)vget_cycles();

	return ret >= __vsyscall_gtod_data.clock.cycle_last ?
		ret : __vsyscall_gtod_data.clock.cycle_last;
}

static struct clocksource clocksource_tsc = {
	.name			= "tsc",
	.rating			= 300,
	.read			= read_tsc,
	.mask			= CLOCKSOURCE_MASK(64),
	.shift			= 22,
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS |
				  CLOCK_SOURCE_MUST_VERIFY,
	.vread			= vread_tsc,
};

void mark_tsc_unstable(char *reason)
{
	if (!tsc_unstable) {
		tsc_unstable = 1;
		printk("Marking TSC unstable due to %s\n", reason);
		/* Change only the rating, when not registered */
		if (clocksource_tsc.mult)
			clocksource_change_rating(&clocksource_tsc, 0);
		else
			clocksource_tsc.rating = 0;
	}
}
EXPORT_SYMBOL_GPL(mark_tsc_unstable);

void __init init_tsc_clocksource(void)
{
	if (tsc_disabled > 0)
		return;

	clocksource_tsc.mult = clocksource_khz2mult(tsc_khz,
			clocksource_tsc.shift);
	if (check_tsc_unstable())
		clocksource_tsc.rating = 0;

	clocksource_register(&clocksource_tsc);
}

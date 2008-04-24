#include <linux/sched.h>
#include <linux/clocksource.h>
#include <linux/workqueue.h>
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

static int tsc_enabled;

/*
 * On some systems the TSC frequency does not
 * change with the cpu frequency. So we need
 * an extra value to store the TSC freq
 */
unsigned int tsc_khz;
EXPORT_SYMBOL_GPL(tsc_khz);

#ifdef CONFIG_X86_TSC
static int __init tsc_setup(char *str)
{
	printk(KERN_WARNING "notsc: Kernel compiled with CONFIG_X86_TSC, "
				"cannot disable TSC completely.\n");
	mark_tsc_unstable("user disabled TSC");
	return 1;
}
#else
/*
 * disable flag for tsc. Takes effect by clearing the TSC cpu flag
 * in cpu/common.c
 */
static int __init tsc_setup(char *str)
{
	setup_clear_cpu_cap(X86_FEATURE_TSC);
	return 1;
}
#endif

__setup("notsc", tsc_setup);

/*
 * code to mark and check if the TSC is unstable
 * due to cpufreq or due to unsynced TSCs
 */
static int tsc_unstable;

int check_tsc_unstable(void)
{
	return tsc_unstable;
}
EXPORT_SYMBOL_GPL(check_tsc_unstable);

/* Accelerators for sched_clock()
 * convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better precision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 */

DEFINE_PER_CPU(unsigned long, cyc2ns);

static void set_cyc2ns_scale(unsigned long cpu_khz, int cpu)
{
	unsigned long long tsc_now, ns_now;
	unsigned long flags, *scale;

	local_irq_save(flags);
	sched_clock_idle_sleep_event();

	scale = &per_cpu(cyc2ns, cpu);

	rdtscll(tsc_now);
	ns_now = __cycles_2_ns(tsc_now);

	if (cpu_khz)
		*scale = (NSEC_PER_MSEC << CYC2NS_SCALE_FACTOR)/cpu_khz;

	/*
	 * Start smoothly with the new frequency:
	 */
	sched_clock_idle_wakeup_event(0);
	local_irq_restore(flags);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long native_sched_clock(void)
{
	unsigned long long this_offset;

	/*
	 * Fall back to jiffies if there's no TSC available:
	 * ( But note that we still use it if the TSC is marked
	 *   unstable. We do this because unlike Time Of Day,
	 *   the scheduler clock tolerates small errors and it's
	 *   very important for it to be as fast as the platform
	 *   can achive it. )
	 */
	if (unlikely(!tsc_enabled && !tsc_unstable))
		/* No locking but a rare wrong value is not a big deal: */
		return (jiffies_64 - INITIAL_JIFFIES) * (1000000000 / HZ);

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
unsigned long long sched_clock(void)
	__attribute__((alias("native_sched_clock")));
#endif

unsigned long native_calculate_cpu_khz(void)
{
	unsigned long long start, end;
	unsigned long count;
	u64 delta64 = (u64)ULLONG_MAX;
	int i;
	unsigned long flags;

	local_irq_save(flags);

	/* run 3 times to ensure the cache is warm and to get an accurate reading */
	for (i = 0; i < 3; i++) {
		mach_prepare_counter();
		rdtscll(start);
		mach_countup(&count);
		rdtscll(end);

		/*
		 * Error: ECTCNEVERSET
		 * The CTC wasn't reliable: we got a hit on the very first read,
		 * or the CPU was so fast/slow that the quotient wouldn't fit in
		 * 32 bits..
		 */
		if (count <= 1)
			continue;

		/* cpu freq too slow: */
		if ((end - start) <= CALIBRATE_TIME_MSEC)
			continue;

		/*
		 * We want the minimum time of all runs in case one of them
		 * is inaccurate due to SMI or other delay
		 */
		delta64 = min(delta64, (end - start));
	}

	/* cpu freq too fast (or every run was bad): */
	if (delta64 > (1ULL<<32))
		goto err;

	delta64 += CALIBRATE_TIME_MSEC/2; /* round for do_div */
	do_div(delta64,CALIBRATE_TIME_MSEC);

	local_irq_restore(flags);
	return (unsigned long)delta64;
err:
	local_irq_restore(flags);
	return 0;
}

int recalibrate_cpu_khz(void)
{
#ifndef CONFIG_SMP
	unsigned long cpu_khz_old = cpu_khz;

	if (cpu_has_tsc) {
		cpu_khz = calculate_cpu_khz();
		tsc_khz = cpu_khz;
		cpu_data(0).loops_per_jiffy =
			cpufreq_scale(cpu_data(0).loops_per_jiffy,
					cpu_khz_old, cpu_khz);
		return 0;
	} else
		return -ENODEV;
#else
	return -ENODEV;
#endif
}

EXPORT_SYMBOL(recalibrate_cpu_khz);

#ifdef CONFIG_CPU_FREQ

/*
 * if the CPU frequency is scaled, TSC-based delays will need a different
 * loops_per_jiffy value to function properly.
 */
static unsigned int ref_freq;
static unsigned long loops_per_jiffy_ref;
static unsigned long cpu_khz_ref;

static int
time_cpufreq_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;

	if (!ref_freq) {
		if (!freq->old){
			ref_freq = freq->new;
			return 0;
		}
		ref_freq = freq->old;
		loops_per_jiffy_ref = cpu_data(freq->cpu).loops_per_jiffy;
		cpu_khz_ref = cpu_khz;
	}

	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
	    (val == CPUFREQ_POSTCHANGE && freq->old > freq->new) ||
	    (val == CPUFREQ_RESUMECHANGE)) {
		if (!(freq->flags & CPUFREQ_CONST_LOOPS))
			cpu_data(freq->cpu).loops_per_jiffy =
				cpufreq_scale(loops_per_jiffy_ref,
						ref_freq, freq->new);

		if (cpu_khz) {

			if (num_online_cpus() == 1)
				cpu_khz = cpufreq_scale(cpu_khz_ref,
						ref_freq, freq->new);
			if (!(freq->flags & CPUFREQ_CONST_LOOPS)) {
				tsc_khz = cpu_khz;
				set_cyc2ns_scale(cpu_khz, freq->cpu);
				/*
				 * TSC based sched_clock turns
				 * to junk w/ cpufreq
				 */
				mark_tsc_unstable("cpufreq changes");
			}
		}
	}

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call	= time_cpufreq_notifier
};

static int __init cpufreq_tsc(void)
{
	return cpufreq_register_notifier(&time_cpufreq_notifier_block,
					 CPUFREQ_TRANSITION_NOTIFIER);
}
core_initcall(cpufreq_tsc);

#endif

/* clock source code */

static unsigned long current_tsc_khz;
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
		tsc_enabled = 0;
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

	if (!cpu_has_tsc)
		return;

	cpu_khz = calculate_cpu_khz();
	tsc_khz = cpu_khz;

	if (!cpu_khz) {
		mark_tsc_unstable("could not calculate TSC khz");
		return;
	}

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
	current_tsc_khz = tsc_khz;
	clocksource_tsc.mult = clocksource_khz2mult(current_tsc_khz,
							clocksource_tsc.shift);
	/* lower the rating if we already know its unstable: */
	if (check_tsc_unstable()) {
		clocksource_tsc.rating = 0;
		clocksource_tsc.flags &= ~CLOCK_SOURCE_IS_CONTINUOUS;
	} else
		tsc_enabled = 1;

	clocksource_register(&clocksource_tsc);
}

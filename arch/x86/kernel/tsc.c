#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/acpi_pmtmr.h>
#include <linux/cpufreq.h>
#include <linux/dmi.h>
#include <linux/delay.h>
#include <linux/clocksource.h>
#include <linux/percpu.h>

#include <asm/hpet.h>
#include <asm/timer.h>
#include <asm/vgtod.h>
#include <asm/time.h>
#include <asm/delay.h>

unsigned int cpu_khz;           /* TSC clocks / usec, not used here */
EXPORT_SYMBOL(cpu_khz);
unsigned int tsc_khz;
EXPORT_SYMBOL(tsc_khz);

/*
 * TSC can be unstable due to cpufreq or due to unsynced TSCs
 */
static int tsc_unstable;

/* native_sched_clock() is called before tsc_init(), so
   we must start with the TSC soft disabled to prevent
   erroneous rdtsc usage on !cpu_has_tsc processors */
static int tsc_disabled = -1;

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

#define MAX_RETRIES     5
#define SMI_TRESHOLD    50000

/*
 * Read TSC and the reference counters. Take care of SMI disturbance
 */
static u64 tsc_read_refs(u64 *p, int hpet)
{
	u64 t1, t2;
	int i;

	for (i = 0; i < MAX_RETRIES; i++) {
		t1 = get_cycles();
		if (hpet)
			*p = hpet_readl(HPET_COUNTER) & 0xFFFFFFFF;
		else
			*p = acpi_pm_read_early();
		t2 = get_cycles();
		if ((t2 - t1) < SMI_TRESHOLD)
			return t2;
	}
	return ULLONG_MAX;
}

/*
 * Calculate the TSC frequency from HPET reference
 */
static unsigned long calc_hpet_ref(u64 deltatsc, u64 hpet1, u64 hpet2)
{
	u64 tmp;

	if (hpet2 < hpet1)
		hpet2 += 0x100000000ULL;
	hpet2 -= hpet1;
	tmp = ((u64)hpet2 * hpet_readl(HPET_PERIOD));
	do_div(tmp, 1000000);
	do_div(deltatsc, tmp);

	return (unsigned long) deltatsc;
}

/*
 * Calculate the TSC frequency from PMTimer reference
 */
static unsigned long calc_pmtimer_ref(u64 deltatsc, u64 pm1, u64 pm2)
{
	u64 tmp;

	if (!pm1 && !pm2)
		return ULONG_MAX;

	if (pm2 < pm1)
		pm2 += (u64)ACPI_PM_OVRRUN;
	pm2 -= pm1;
	tmp = pm2 * 1000000000LL;
	do_div(tmp, PMTMR_TICKS_PER_SEC);
	do_div(deltatsc, tmp);

	return (unsigned long) deltatsc;
}

#define CAL_MS		50
#define CAL_LATCH	(CLOCK_TICK_RATE / (1000 / CAL_MS))
#define CAL_PIT_LOOPS	5000

/*
 * Try to calibrate the TSC against the Programmable
 * Interrupt Timer and return the frequency of the TSC
 * in kHz.
 *
 * Return ULONG_MAX on failure to calibrate.
 */
static unsigned long pit_calibrate_tsc(void)
{
	u64 tsc, t1, t2, delta;
	unsigned long tscmin, tscmax;
	int pitcnt;

	/* Set the Gate high, disable speaker */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	/*
	 * Setup CTC channel 2* for mode 0, (interrupt on terminal
	 * count mode), binary count. Set the latch register to 50ms
	 * (LSB then MSB) to begin countdown.
	 */
	outb(0xb0, 0x43);
	outb(CAL_LATCH & 0xff, 0x42);
	outb(CAL_LATCH >> 8, 0x42);

	tsc = t1 = t2 = get_cycles();

	pitcnt = 0;
	tscmax = 0;
	tscmin = ULONG_MAX;
	while ((inb(0x61) & 0x20) == 0) {
		t2 = get_cycles();
		delta = t2 - tsc;
		tsc = t2;
		if ((unsigned long) delta < tscmin)
			tscmin = (unsigned int) delta;
		if ((unsigned long) delta > tscmax)
			tscmax = (unsigned int) delta;
		pitcnt++;
	}

	/*
	 * Sanity checks:
	 *
	 * If we were not able to read the PIT more than PIT_MIN_LOOPS
	 * times, then we have been hit by a massive SMI
	 *
	 * If the maximum is 10 times larger than the minimum,
	 * then we got hit by an SMI as well.
	 */
	if (pitcnt < CAL_PIT_LOOPS || tscmax > 10 * tscmin)
		return ULONG_MAX;

	/* Calculate the PIT value */
	delta = t2 - t1;
	do_div(delta, CAL_MS);
	return delta;
}


/**
 * native_calibrate_tsc - calibrate the tsc on boot
 */
unsigned long native_calibrate_tsc(void)
{
	u64 tsc1, tsc2, delta, ref1, ref2;
	unsigned long tsc_pit_min = ULONG_MAX, tsc_ref_min = ULONG_MAX;
	unsigned long flags;
	int hpet = is_hpet_enabled(), i;

	/*
	 * Run 5 calibration loops to get the lowest frequency value
	 * (the best estimate). We use two different calibration modes
	 * here:
	 *
	 * 1) PIT loop. We set the PIT Channel 2 to oneshot mode and
	 * load a timeout of 50ms. We read the time right after we
	 * started the timer and wait until the PIT count down reaches
	 * zero. In each wait loop iteration we read the TSC and check
	 * the delta to the previous read. We keep track of the min
	 * and max values of that delta. The delta is mostly defined
	 * by the IO time of the PIT access, so we can detect when a
	 * SMI/SMM disturbance happend between the two reads. If the
	 * maximum time is significantly larger than the minimum time,
	 * then we discard the result and have another try.
	 *
	 * 2) Reference counter. If available we use the HPET or the
	 * PMTIMER as a reference to check the sanity of that value.
	 * We use separate TSC readouts and check inside of the
	 * reference read for a SMI/SMM disturbance. We dicard
	 * disturbed values here as well. We do that around the PIT
	 * calibration delay loop as we have to wait for a certain
	 * amount of time anyway.
	 */
	for (i = 0; i < 5; i++) {
		unsigned long tsc_pit_khz;

		/*
		 * Read the start value and the reference count of
		 * hpet/pmtimer when available. Then do the PIT
		 * calibration, which will take at least 50ms, and
		 * read the end value.
		 */
		local_irq_save(flags);
		tsc1 = tsc_read_refs(&ref1, hpet);
		tsc_pit_khz = pit_calibrate_tsc();
		tsc2 = tsc_read_refs(&ref2, hpet);
		local_irq_restore(flags);

		/* Pick the lowest PIT TSC calibration so far */
		tsc_pit_min = min(tsc_pit_min, tsc_pit_khz);

		/* hpet or pmtimer available ? */
		if (!hpet && !ref1 && !ref2)
			continue;

		/* Check, whether the sampling was disturbed by an SMI */
		if (tsc1 == ULLONG_MAX || tsc2 == ULLONG_MAX)
			continue;

		tsc2 = (tsc2 - tsc1) * 1000000LL;
		if (hpet)
			tsc2 = calc_hpet_ref(tsc2, ref1, ref2);
		else
			tsc2 = calc_pmtimer_ref(tsc2, ref1, ref2);

		tsc_ref_min = min(tsc_ref_min, (unsigned long) tsc2);
	}

	/*
	 * Now check the results.
	 */
	if (tsc_pit_min == ULONG_MAX) {
		/* PIT gave no useful value */
		printk(KERN_WARNING "TSC: PIT calibration failed due to "
		       "SMI disturbance.\n");

		/* We don't have an alternative source, disable TSC */
		if (!hpet && !ref1 && !ref2) {
			printk("TSC: No reference (HPET/PMTIMER) available\n");
			return 0;
		}

		/* The alternative source failed as well, disable TSC */
		if (tsc_ref_min == ULONG_MAX) {
			printk(KERN_WARNING "TSC: HPET/PMTIMER calibration "
			       "failed due to SMI disturbance.\n");
			return 0;
		}

		/* Use the alternative source */
		printk(KERN_INFO "TSC: using %s reference calibration\n",
		       hpet ? "HPET" : "PMTIMER");

		return tsc_ref_min;
	}

	/* We don't have an alternative source, use the PIT calibration value */
	if (!hpet && !ref1 && !ref2) {
		printk(KERN_INFO "TSC: Using PIT calibration value\n");
		return tsc_pit_min;
	}

	/* The alternative source failed, use the PIT calibration value */
	if (tsc_ref_min == ULONG_MAX) {
		printk(KERN_WARNING "TSC: HPET/PMTIMER calibration failed due "
		       "to SMI disturbance. Using PIT calibration\n");
		return tsc_pit_min;
	}

	/* Check the reference deviation */
	delta = ((u64) tsc_pit_min) * 100;
	do_div(delta, tsc_ref_min);

	/*
	 * If both calibration results are inside a 5% window, the we
	 * use the lower frequency of those as it is probably the
	 * closest estimate.
	 */
	if (delta >= 95 && delta <= 105) {
		printk(KERN_INFO "TSC: PIT calibration confirmed by %s.\n",
		       hpet ? "HPET" : "PMTIMER");
		printk(KERN_INFO "TSC: using %s calibration value\n",
		       tsc_pit_min <= tsc_ref_min ? "PIT" :
		       hpet ? "HPET" : "PMTIMER");
		return tsc_pit_min <= tsc_ref_min ? tsc_pit_min : tsc_ref_min;
	}

	printk(KERN_WARNING "TSC: PIT calibration deviates from %s: %lu %lu.\n",
	       hpet ? "HPET" : "PMTIMER", tsc_pit_min, tsc_ref_min);

	/*
	 * The calibration values differ too much. In doubt, we use
	 * the PIT value as we know that there are PMTIMERs around
	 * running at double speed.
	 */
	printk(KERN_INFO "TSC: Using PIT calibration value\n");
	return tsc_pit_min;
}

#ifdef CONFIG_X86_32
/* Only called from the Powernow K7 cpu freq driver */
int recalibrate_cpu_khz(void)
{
#ifndef CONFIG_SMP
	unsigned long cpu_khz_old = cpu_khz;

	if (cpu_has_tsc) {
		tsc_khz = calibrate_tsc();
		cpu_khz = tsc_khz;
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

#endif /* CONFIG_X86_32 */

/* Accelerators for sched_clock()
 * convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *              ns = cycles / (freq / ns_per_sec)
 *              ns = cycles * (ns_per_sec / freq)
 *              ns = cycles * (10^9 / (cpu_khz * 10^3))
 *              ns = cycles * (10^6 / cpu_khz)
 *
 *      Then we use scaling math (suggested by george@mvista.com) to get:
 *              ns = cycles * (10^6 * SC / cpu_khz) / SC
 *              ns = cycles * cyc2ns_scale / SC
 *
 *      And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better precision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *                      -johnstul@us.ibm.com "math is hard, lets go shopping!"
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

	sched_clock_idle_wakeup_event(0);
	local_irq_restore(flags);
}

#ifdef CONFIG_CPU_FREQ

/* Frequency scaling support. Adjust the TSC based timer when the cpu frequency
 * changes.
 *
 * RED-PEN: On SMP we assume all CPUs run with the same frequency.  It's
 * not that important because current Opteron setups do not support
 * scaling on SMP anyroads.
 *
 * Should fix up last_tsc too. Currently gettimeofday in the
 * first tick after the change will be slightly wrong.
 */

static unsigned int  ref_freq;
static unsigned long loops_per_jiffy_ref;
static unsigned long tsc_khz_ref;

static int time_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned long *lpj, dummy;

	if (cpu_has(&cpu_data(freq->cpu), X86_FEATURE_CONSTANT_TSC))
		return 0;

	lpj = &dummy;
	if (!(freq->flags & CPUFREQ_CONST_LOOPS))
#ifdef CONFIG_SMP
		lpj = &cpu_data(freq->cpu).loops_per_jiffy;
#else
	lpj = &boot_cpu_data.loops_per_jiffy;
#endif

	if (!ref_freq) {
		ref_freq = freq->old;
		loops_per_jiffy_ref = *lpj;
		tsc_khz_ref = tsc_khz;
	}
	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
			(val == CPUFREQ_POSTCHANGE && freq->old > freq->new) ||
			(val == CPUFREQ_RESUMECHANGE)) {
		*lpj = 	cpufreq_scale(loops_per_jiffy_ref, ref_freq, freq->new);

		tsc_khz = cpufreq_scale(tsc_khz_ref, ref_freq, freq->new);
		if (!(freq->flags & CPUFREQ_CONST_LOOPS))
			mark_tsc_unstable("cpufreq changes");
	}

	set_cyc2ns_scale(tsc_khz, freq->cpu);

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call  = time_cpufreq_notifier
};

static int __init cpufreq_tsc(void)
{
	if (!cpu_has_tsc)
		return 0;
	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		return 0;
	cpufreq_register_notifier(&time_cpufreq_notifier_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	return 0;
}

core_initcall(cpufreq_tsc);

#endif /* CONFIG_CPU_FREQ */

/* clocksource code */

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

#ifdef CONFIG_X86_64
static cycle_t __vsyscall_fn vread_tsc(void)
{
	cycle_t ret = (cycle_t)vget_cycles();

	return ret >= __vsyscall_gtod_data.clock.cycle_last ?
		ret : __vsyscall_gtod_data.clock.cycle_last;
}
#endif

static struct clocksource clocksource_tsc = {
	.name                   = "tsc",
	.rating                 = 300,
	.read                   = read_tsc,
	.mask                   = CLOCKSOURCE_MASK(64),
	.shift                  = 22,
	.flags                  = CLOCK_SOURCE_IS_CONTINUOUS |
				  CLOCK_SOURCE_MUST_VERIFY,
#ifdef CONFIG_X86_64
	.vread                  = vread_tsc,
#endif
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

/*
 * Make an educated guess if the TSC is trustworthy and synchronized
 * over all CPUs.
 */
__cpuinit int unsynchronized_tsc(void)
{
	if (!cpu_has_tsc || tsc_unstable)
		return 1;

#ifdef CONFIG_SMP
	if (apic_is_clustered_box())
		return 1;
#endif

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

static void __init init_tsc_clocksource(void)
{
	clocksource_tsc.mult = clocksource_khz2mult(tsc_khz,
			clocksource_tsc.shift);
	/* lower the rating if we already know its unstable: */
	if (check_tsc_unstable()) {
		clocksource_tsc.rating = 0;
		clocksource_tsc.flags &= ~CLOCK_SOURCE_IS_CONTINUOUS;
	}
	clocksource_register(&clocksource_tsc);
}

void __init tsc_init(void)
{
	u64 lpj;
	int cpu;

	if (!cpu_has_tsc)
		return;

	tsc_khz = calibrate_tsc();
	cpu_khz = tsc_khz;

	if (!tsc_khz) {
		mark_tsc_unstable("could not calculate TSC khz");
		return;
	}

#ifdef CONFIG_X86_64
	if (cpu_has(&boot_cpu_data, X86_FEATURE_CONSTANT_TSC) &&
			(boot_cpu_data.x86_vendor == X86_VENDOR_AMD))
		cpu_khz = calibrate_cpu();
#endif

	lpj = ((u64)tsc_khz * 1000);
	do_div(lpj, HZ);
	lpj_fine = lpj;

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

	if (tsc_disabled > 0)
		return;

	/* now allow native_sched_clock() to use rdtsc */
	tsc_disabled = 0;

	use_tsc_delay();
	/* Check and install the TSC clocksource */
	dmi_check_system(bad_tsc_dmi_table);

	if (unsynchronized_tsc())
		mark_tsc_unstable("TSCs unsynchronized");

	check_geode_tsc_reliable();
	init_tsc_clocksource();
}


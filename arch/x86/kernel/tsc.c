#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/acpi_pmtmr.h>
#include <linux/cpufreq.h>

#include <asm/hpet.h>

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

#define MAX_RETRIES     5
#define SMI_TRESHOLD    50000

/*
 * Read TSC and the reference counters. Take care of SMI disturbance
 */
static u64 __init tsc_read_refs(u64 *pm, u64 *hpet)
{
	u64 t1, t2;
	int i;

	for (i = 0; i < MAX_RETRIES; i++) {
		t1 = get_cycles();
		if (hpet)
			*hpet = hpet_readl(HPET_COUNTER) & 0xFFFFFFFF;
		else
			*pm = acpi_pm_read_early();
		t2 = get_cycles();
		if ((t2 - t1) < SMI_TRESHOLD)
			return t2;
	}
	return ULLONG_MAX;
}

/**
 * tsc_calibrate - calibrate the tsc on boot
 */
static unsigned int __init tsc_calibrate(void)
{
	unsigned long flags;
	u64 tsc1, tsc2, tr1, tr2, delta, pm1, pm2, hpet1, hpet2;
	int hpet = is_hpet_enabled();
	unsigned int tsc_khz_val = 0;

	local_irq_save(flags);

	tsc1 = tsc_read_refs(&pm1, hpet ? &hpet1 : NULL);

	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	outb(0xb0, 0x43);
	outb((CLOCK_TICK_RATE / (1000 / 50)) & 0xff, 0x42);
	outb((CLOCK_TICK_RATE / (1000 / 50)) >> 8, 0x42);
	tr1 = get_cycles();
	while ((inb(0x61) & 0x20) == 0);
	tr2 = get_cycles();

	tsc2 = tsc_read_refs(&pm2, hpet ? &hpet2 : NULL);

	local_irq_restore(flags);

	/*
	 * Preset the result with the raw and inaccurate PIT
	 * calibration value
	 */
	delta = (tr2 - tr1);
	do_div(delta, 50);
	tsc_khz_val = delta;

	/* hpet or pmtimer available ? */
	if (!hpet && !pm1 && !pm2) {
		printk(KERN_INFO "TSC calibrated against PIT\n");
		goto out;
	}

	/* Check, whether the sampling was disturbed by an SMI */
	if (tsc1 == ULLONG_MAX || tsc2 == ULLONG_MAX) {
		printk(KERN_WARNING "TSC calibration disturbed by SMI, "
				"using PIT calibration result\n");
		goto out;
	}

	tsc2 = (tsc2 - tsc1) * 1000000LL;

	if (hpet) {
		printk(KERN_INFO "TSC calibrated against HPET\n");
		if (hpet2 < hpet1)
			hpet2 += 0x100000000ULL;
		hpet2 -= hpet1;
		tsc1 = ((u64)hpet2 * hpet_readl(HPET_PERIOD));
		do_div(tsc1, 1000000);
	} else {
		printk(KERN_INFO "TSC calibrated against PM_TIMER\n");
		if (pm2 < pm1)
			pm2 += (u64)ACPI_PM_OVRRUN;
		pm2 -= pm1;
		tsc1 = pm2 * 1000000000LL;
		do_div(tsc1, PMTMR_TICKS_PER_SEC);
	}

	do_div(tsc2, tsc1);
	tsc_khz_val = tsc2;

out:
	return tsc_khz_val;
}

unsigned long native_calculate_cpu_khz(void)
{
	return tsc_calibrate();
}

#ifdef CONFIG_X86_32
/* Only called from the Powernow K7 cpu freq driver */
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

void set_cyc2ns_scale(unsigned long cpu_khz, int cpu)
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

	set_cyc2ns_scale(tsc_khz_ref, freq->cpu);

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call  = time_cpufreq_notifier
};

static int __init cpufreq_tsc(void)
{
	cpufreq_register_notifier(&time_cpufreq_notifier_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	return 0;
}

core_initcall(cpufreq_tsc);

#endif /* CONFIG_CPU_FREQ */

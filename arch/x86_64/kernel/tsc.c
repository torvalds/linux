#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/time.h>
#include <linux/acpi.h>
#include <linux/cpufreq.h>

#include <asm/timex.h>

int notsc __initdata = 0;

unsigned int cpu_khz;		/* TSC clocks / usec, not used here */
EXPORT_SYMBOL(cpu_khz);

/*
 * do_gettimeoffset() returns microseconds since last timer interrupt was
 * triggered by hardware. A memory read of HPET is slower than a register read
 * of TSC, but much more reliable. It's also synchronized to the timer
 * interrupt. Note that do_gettimeoffset() may return more than hpet_tick, if a
 * timer interrupt has happened already, but vxtime.trigger wasn't updated yet.
 * This is not a problem, because jiffies hasn't updated either. They are bound
 * together by xtime_lock.
 */

unsigned int do_gettimeoffset_tsc(void)
{
	unsigned long t;
	unsigned long x;
	t = get_cycles_sync();
	if (t < vxtime.last_tsc)
		t = vxtime.last_tsc; /* hack */
	x = ((t - vxtime.last_tsc) * vxtime.tsc_quot) >> US_SCALE;
	return x;
}

static unsigned int cyc2ns_scale __read_mostly;

void set_cyc2ns_scale(unsigned long khz)
{
	cyc2ns_scale = (NSEC_PER_MSEC << NS_SCALE) / khz;
}

unsigned long long cycles_2_ns(unsigned long long cyc)
{
	return (cyc * cyc2ns_scale) >> NS_SCALE;
}

unsigned long long sched_clock(void)
{
	unsigned long a = 0;

	/* Could do CPU core sync here. Opteron can execute rdtsc speculatively,
	 * which means it is not completely exact and may not be monotonous
	 * between CPUs. But the errors should be too small to matter for
	 * scheduling purposes.
	 */

	rdtscll(a);
	return cycles_2_ns(a);
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

#include <linux/workqueue.h>

static unsigned int cpufreq_delayed_issched = 0;
static unsigned int cpufreq_init = 0;
static struct work_struct cpufreq_delayed_get_work;

static void handle_cpufreq_delayed_get(struct work_struct *v)
{
	unsigned int cpu;
	for_each_online_cpu(cpu) {
		cpufreq_get(cpu);
	}
	cpufreq_delayed_issched = 0;
}

/* if we notice lost ticks, schedule a call to cpufreq_get() as it tries
 * to verify the CPU frequency the timing core thinks the CPU is running
 * at is still correct.
 */
void cpufreq_delayed_get(void)
{
	static int warned;
	if (cpufreq_init && !cpufreq_delayed_issched) {
		cpufreq_delayed_issched = 1;
		if (!warned) {
			warned = 1;
			printk(KERN_DEBUG "Losing some ticks... "
				"checking if CPU frequency changed.\n");
		}
		schedule_work(&cpufreq_delayed_get_work);
	}
}

static unsigned int  ref_freq = 0;
static unsigned long loops_per_jiffy_ref = 0;

static unsigned long cpu_khz_ref = 0;

static int time_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				 void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned long *lpj, dummy;

	if (cpu_has(&cpu_data[freq->cpu], X86_FEATURE_CONSTANT_TSC))
		return 0;

	lpj = &dummy;
	if (!(freq->flags & CPUFREQ_CONST_LOOPS))
#ifdef CONFIG_SMP
		lpj = &cpu_data[freq->cpu].loops_per_jiffy;
#else
		lpj = &boot_cpu_data.loops_per_jiffy;
#endif

	if (!ref_freq) {
		ref_freq = freq->old;
		loops_per_jiffy_ref = *lpj;
		cpu_khz_ref = cpu_khz;
	}
	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
		(val == CPUFREQ_POSTCHANGE && freq->old > freq->new) ||
		(val == CPUFREQ_RESUMECHANGE)) {
		*lpj =
		cpufreq_scale(loops_per_jiffy_ref, ref_freq, freq->new);

		cpu_khz = cpufreq_scale(cpu_khz_ref, ref_freq, freq->new);
		if (!(freq->flags & CPUFREQ_CONST_LOOPS))
			vxtime.tsc_quot = (USEC_PER_MSEC << US_SCALE) / cpu_khz;
	}

	set_cyc2ns_scale(cpu_khz_ref);

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call  = time_cpufreq_notifier
};

static int __init cpufreq_tsc(void)
{
	INIT_WORK(&cpufreq_delayed_get_work, handle_cpufreq_delayed_get);
	if (!cpufreq_register_notifier(&time_cpufreq_notifier_block,
				       CPUFREQ_TRANSITION_NOTIFIER))
		cpufreq_init = 1;
	return 0;
}

core_initcall(cpufreq_tsc);

#endif

static int tsc_unstable = 0;

void mark_tsc_unstable(void)
{
	tsc_unstable = 1;
}
EXPORT_SYMBOL_GPL(mark_tsc_unstable);

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
	/* Most intel systems have synchronized TSCs except for
	   multi node systems */
 	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
#ifdef CONFIG_ACPI
		/* But TSC doesn't tick in C3 so don't use it there */
		if (acpi_gbl_FADT.header.length > 0 && acpi_gbl_FADT.C3latency < 1000)
			return 1;
#endif
 		return 0;
	}

 	/* Assume multi socket systems are not synchronized */
 	return num_present_cpus() > 1;
}

int __init notsc_setup(char *s)
{
	notsc = 1;
	return 1;
}

__setup("notsc", notsc_setup);

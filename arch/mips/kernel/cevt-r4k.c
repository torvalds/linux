/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 MIPS Technologies, Inc.
 * Copyright (C) 2007 Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/cpufreq.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/irq.h>

#include <asm/time.h>
#include <asm/cevt-r4k.h>

static int mips_next_event(unsigned long delta,
			   struct clock_event_device *evt)
{
	unsigned int cnt;
	int res;

	cnt = read_c0_count();
	cnt += delta;
	write_c0_compare(cnt);
	res = ((int)(read_c0_count() - cnt) >= 0) ? -ETIME : 0;
	return res;
}

/**
 * calculate_min_delta() - Calculate a good minimum delta for mips_next_event().
 *
 * Running under virtualisation can introduce overhead into mips_next_event() in
 * the form of hypervisor emulation of CP0_Count/CP0_Compare registers,
 * potentially with an unnatural frequency, which makes a fixed min_delta_ns
 * value inappropriate as it may be too small.
 *
 * It can also introduce occasional latency from the guest being descheduled.
 *
 * This function calculates a good minimum delta based roughly on the 75th
 * percentile of the time taken to do the mips_next_event() sequence, in order
 * to handle potentially higher overhead while also eliminating outliers due to
 * unpredictable hypervisor latency (which can be handled by retries).
 *
 * Return:	An appropriate minimum delta for the clock event device.
 */
static unsigned int calculate_min_delta(void)
{
	unsigned int cnt, i, j, k, l;
	unsigned int buf1[4], buf2[3];
	unsigned int min_delta;

	/*
	 * Calculate the median of 5 75th percentiles of 5 samples of how long
	 * it takes to set CP0_Compare = CP0_Count + delta.
	 */
	for (i = 0; i < 5; ++i) {
		for (j = 0; j < 5; ++j) {
			/*
			 * This is like the code in mips_next_event(), and
			 * directly measures the borderline "safe" delta.
			 */
			cnt = read_c0_count();
			write_c0_compare(cnt);
			cnt = read_c0_count() - cnt;

			/* Sorted insert into buf1 */
			for (k = 0; k < j; ++k) {
				if (cnt < buf1[k]) {
					l = min_t(unsigned int,
						  j, ARRAY_SIZE(buf1) - 1);
					for (; l > k; --l)
						buf1[l] = buf1[l - 1];
					break;
				}
			}
			if (k < ARRAY_SIZE(buf1))
				buf1[k] = cnt;
		}

		/* Sorted insert of 75th percentile into buf2 */
		for (k = 0; k < i && k < ARRAY_SIZE(buf2); ++k) {
			if (buf1[ARRAY_SIZE(buf1) - 1] < buf2[k]) {
				l = min_t(unsigned int,
					  i, ARRAY_SIZE(buf2) - 1);
				for (; l > k; --l)
					buf2[l] = buf2[l - 1];
				break;
			}
		}
		if (k < ARRAY_SIZE(buf2))
			buf2[k] = buf1[ARRAY_SIZE(buf1) - 1];
	}

	/* Use 2 * median of 75th percentiles */
	min_delta = buf2[ARRAY_SIZE(buf2) - 1] * 2;

	/* Don't go too low */
	if (min_delta < 0x300)
		min_delta = 0x300;

	pr_debug("%s: median 75th percentile=%#x, min_delta=%#x\n",
		 __func__, buf2[ARRAY_SIZE(buf2) - 1], min_delta);
	return min_delta;
}

DEFINE_PER_CPU(struct clock_event_device, mips_clockevent_device);
int cp0_timer_irq_installed;

/*
 * Possibly handle a performance counter interrupt.
 * Return true if the timer interrupt should not be checked
 */
static inline int handle_perf_irq(int r2)
{
	/*
	 * The performance counter overflow interrupt may be shared with the
	 * timer interrupt (cp0_perfcount_irq < 0). If it is and a
	 * performance counter has overflowed (perf_irq() == IRQ_HANDLED)
	 * and we can't reliably determine if a counter interrupt has also
	 * happened (!r2) then don't check for a timer interrupt.
	 */
	return (cp0_perfcount_irq < 0) &&
		perf_irq() == IRQ_HANDLED &&
		!r2;
}

irqreturn_t c0_compare_interrupt(int irq, void *dev_id)
{
	const int r2 = cpu_has_mips_r2_r6;
	struct clock_event_device *cd;
	int cpu = smp_processor_id();

	/*
	 * Suckage alert:
	 * Before R2 of the architecture there was no way to see if a
	 * performance counter interrupt was pending, so we have to run
	 * the performance counter interrupt handler anyway.
	 */
	if (handle_perf_irq(r2))
		return IRQ_HANDLED;

	/*
	 * The same applies to performance counter interrupts.	But with the
	 * above we now know that the reason we got here must be a timer
	 * interrupt.  Being the paranoiacs we are we check anyway.
	 */
	if (!r2 || (read_c0_cause() & CAUSEF_TI)) {
		/* Clear Count/Compare Interrupt */
		write_c0_compare(read_c0_compare());
		cd = &per_cpu(mips_clockevent_device, cpu);
		cd->event_handler(cd);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

struct irqaction c0_compare_irqaction = {
	.handler = c0_compare_interrupt,
	/*
	 * IRQF_SHARED: The timer interrupt may be shared with other interrupts
	 * such as perf counter and FDC interrupts.
	 */
	.flags = IRQF_PERCPU | IRQF_TIMER | IRQF_SHARED,
	.name = "timer",
};


void mips_event_handler(struct clock_event_device *dev)
{
}

/*
 * FIXME: This doesn't hold for the relocated E9000 compare interrupt.
 */
static int c0_compare_int_pending(void)
{
	/* When cpu_has_mips_r2, this checks Cause.TI instead of Cause.IP7 */
	return (read_c0_cause() >> cp0_compare_irq_shift) & (1ul << CAUSEB_IP);
}

/*
 * Compare interrupt can be routed and latched outside the core,
 * so wait up to worst case number of cycle counter ticks for timer interrupt
 * changes to propagate to the cause register.
 */
#define COMPARE_INT_SEEN_TICKS 50

int c0_compare_int_usable(void)
{
	unsigned int delta;
	unsigned int cnt;

	/*
	 * IP7 already pending?	 Try to clear it by acking the timer.
	 */
	if (c0_compare_int_pending()) {
		cnt = read_c0_count();
		write_c0_compare(cnt);
		back_to_back_c0_hazard();
		while (read_c0_count() < (cnt  + COMPARE_INT_SEEN_TICKS))
			if (!c0_compare_int_pending())
				break;
		if (c0_compare_int_pending())
			return 0;
	}

	for (delta = 0x10; delta <= 0x400000; delta <<= 1) {
		cnt = read_c0_count();
		cnt += delta;
		write_c0_compare(cnt);
		back_to_back_c0_hazard();
		if ((int)(read_c0_count() - cnt) < 0)
		    break;
		/* increase delta if the timer was already expired */
	}

	while ((int)(read_c0_count() - cnt) <= 0)
		;	/* Wait for expiry  */

	while (read_c0_count() < (cnt + COMPARE_INT_SEEN_TICKS))
		if (c0_compare_int_pending())
			break;
	if (!c0_compare_int_pending())
		return 0;
	cnt = read_c0_count();
	write_c0_compare(cnt);
	back_to_back_c0_hazard();
	while (read_c0_count() < (cnt + COMPARE_INT_SEEN_TICKS))
		if (!c0_compare_int_pending())
			break;
	if (c0_compare_int_pending())
		return 0;

	/*
	 * Feels like a real count / compare timer.
	 */
	return 1;
}

unsigned int __weak get_c0_compare_int(void)
{
	return MIPS_CPU_IRQ_BASE + cp0_compare_irq;
}

#ifdef CONFIG_CPU_FREQ

static unsigned long mips_ref_freq;

static int r4k_cpufreq_callback(struct notifier_block *nb,
				unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct clock_event_device *cd;
	unsigned long rate;
	int cpu;

	if (!mips_ref_freq)
		mips_ref_freq = freq->old;

	if (val == CPUFREQ_POSTCHANGE) {
		rate = cpufreq_scale(mips_hpt_frequency, mips_ref_freq,
				     freq->new);

		for_each_cpu(cpu, freq->policy->cpus) {
			cd = &per_cpu(mips_clockevent_device, cpu);

			clockevents_update_freq(cd, rate);
		}
	}

	return 0;
}

static struct notifier_block r4k_cpufreq_notifier = {
	.notifier_call  = r4k_cpufreq_callback,
};

static int __init r4k_register_cpufreq_notifier(void)
{
	return cpufreq_register_notifier(&r4k_cpufreq_notifier,
					 CPUFREQ_TRANSITION_NOTIFIER);

}
core_initcall(r4k_register_cpufreq_notifier);

#endif /* !CONFIG_CPU_FREQ */

int r4k_clockevent_init(void)
{
	unsigned long flags = IRQF_PERCPU | IRQF_TIMER | IRQF_SHARED;
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;
	unsigned int irq, min_delta;

	if (!cpu_has_counter || !mips_hpt_frequency)
		return -ENXIO;

	if (!c0_compare_int_usable())
		return -ENXIO;

	cd = &per_cpu(mips_clockevent_device, cpu);

	cd->name		= "MIPS";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_C3STOP |
				  CLOCK_EVT_FEAT_PERCPU;

	min_delta		= calculate_min_delta();

	cd->rating		= 300;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= mips_next_event;
	cd->event_handler	= mips_event_handler;

	clockevents_config_and_register(cd, mips_hpt_frequency, min_delta, 0x7fffffff);

	if (cp0_timer_irq_installed)
		return 0;

	cp0_timer_irq_installed = 1;

	/*
	 * With vectored interrupts things are getting platform specific.
	 * get_c0_compare_int is a hook to allow a platform to return the
	 * interrupt number of its liking.
	 */
	irq = get_c0_compare_int();

	if (request_irq(irq, c0_compare_interrupt, flags, "timer",
			c0_compare_interrupt))
		pr_err("Failed to request irq %d (timer)\n", irq);

	return 0;
}


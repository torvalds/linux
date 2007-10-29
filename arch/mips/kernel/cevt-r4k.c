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
#include <linux/percpu.h>

#include <asm/smtc_ipi.h>
#include <asm/time.h>

static int mips_next_event(unsigned long delta,
                           struct clock_event_device *evt)
{
	unsigned int cnt;
	int res;

#ifdef CONFIG_MIPS_MT_SMTC
	{
	unsigned long flags, vpflags;
	local_irq_save(flags);
	vpflags = dvpe();
#endif
	cnt = read_c0_count();
	cnt += delta;
	write_c0_compare(cnt);
	res = ((int)(read_c0_count() - cnt) > 0) ? -ETIME : 0;
#ifdef CONFIG_MIPS_MT_SMTC
	evpe(vpflags);
	local_irq_restore(flags);
	}
#endif
	return res;
}

static void mips_set_mode(enum clock_event_mode mode,
                          struct clock_event_device *evt)
{
	/* Nothing to do ...  */
}

static DEFINE_PER_CPU(struct clock_event_device, mips_clockevent_device);
static int cp0_timer_irq_installed;

/*
 * Timer ack for an R4k-compatible timer of a known frequency.
 */
static void c0_timer_ack(void)
{
	write_c0_compare(read_c0_compare());
}

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

static irqreturn_t c0_compare_interrupt(int irq, void *dev_id)
{
	const int r2 = cpu_has_mips_r2;
	struct clock_event_device *cd;
	int cpu = smp_processor_id();

	/*
	 * Suckage alert:
	 * Before R2 of the architecture there was no way to see if a
	 * performance counter interrupt was pending, so we have to run
	 * the performance counter interrupt handler anyway.
	 */
	if (handle_perf_irq(r2))
		goto out;

	/*
	 * The same applies to performance counter interrupts.  But with the
	 * above we now know that the reason we got here must be a timer
	 * interrupt.  Being the paranoiacs we are we check anyway.
	 */
	if (!r2 || (read_c0_cause() & (1 << 30))) {
		c0_timer_ack();
#ifdef CONFIG_MIPS_MT_SMTC
		if (cpu_data[cpu].vpe_id)
			goto out;
		cpu = 0;
#endif
		cd = &per_cpu(mips_clockevent_device, cpu);
		cd->event_handler(cd);
	}

out:
	return IRQ_HANDLED;
}

static struct irqaction c0_compare_irqaction = {
	.handler = c0_compare_interrupt,
#ifdef CONFIG_MIPS_MT_SMTC
	.flags = IRQF_DISABLED,
#else
	.flags = IRQF_DISABLED | IRQF_PERCPU,
#endif
	.name = "timer",
};

#ifdef CONFIG_MIPS_MT_SMTC
DEFINE_PER_CPU(struct clock_event_device, smtc_dummy_clockevent_device);

static void smtc_set_mode(enum clock_event_mode mode,
                          struct clock_event_device *evt)
{
}

static void mips_broadcast(cpumask_t mask)
{
	unsigned int cpu;

	for_each_cpu_mask(cpu, mask)
		smtc_send_ipi(cpu, SMTC_CLOCK_TICK, 0);
}

static void setup_smtc_dummy_clockevent_device(void)
{
	//uint64_t mips_freq = mips_hpt_^frequency;
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;

	cd = &per_cpu(smtc_dummy_clockevent_device, cpu);

	cd->name		= "SMTC";
	cd->features		= CLOCK_EVT_FEAT_DUMMY;

	/* Calculate the min / max delta */
	cd->mult	= 0; //div_sc((unsigned long) mips_freq, NSEC_PER_SEC, 32);
	cd->shift		= 0; //32;
	cd->max_delta_ns	= 0; //clockevent_delta2ns(0x7fffffff, cd);
	cd->min_delta_ns	= 0; //clockevent_delta2ns(0x30, cd);

	cd->rating		= 200;
	cd->irq			= 17; //-1;
//	if (cpu)
//		cd->cpumask	= CPU_MASK_ALL; // cpumask_of_cpu(cpu);
//	else
		cd->cpumask	= cpumask_of_cpu(cpu);

	cd->set_mode		= smtc_set_mode;

	cd->broadcast		= mips_broadcast;

	clockevents_register_device(cd);
}
#endif

static void mips_event_handler(struct clock_event_device *dev)
{
}

/*
 * FIXME: This doesn't hold for the relocated E9000 compare interrupt.
 */
static int c0_compare_int_pending(void)
{
	return (read_c0_cause() >> cp0_compare_irq) & 0x100;
}

static int c0_compare_int_usable(void)
{
	unsigned int delta;
	unsigned int cnt;

	/*
	 * IP7 already pending?  Try to clear it by acking the timer.
	 */
	if (c0_compare_int_pending()) {
		write_c0_compare(read_c0_count());
		irq_disable_hazard();
		if (c0_compare_int_pending())
			return 0;
	}

	for (delta = 0x10; delta <= 0x400000; delta <<= 1) {
		cnt = read_c0_count();
		cnt += delta;
		write_c0_compare(cnt);
		irq_disable_hazard();
		if ((int)(read_c0_count() - cnt) < 0)
		    break;
		/* increase delta if the timer was already expired */
	}

	while ((int)(read_c0_count() - cnt) <= 0)
		;	/* Wait for expiry  */

	if (!c0_compare_int_pending())
		return 0;

	write_c0_compare(read_c0_count());
	irq_disable_hazard();
	if (c0_compare_int_pending())
		return 0;

	/*
	 * Feels like a real count / compare timer.
	 */
	return 1;
}

void __cpuinit mips_clockevent_init(void)
{
	uint64_t mips_freq = mips_hpt_frequency;
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;
	unsigned int irq;

	if (!cpu_has_counter || !mips_hpt_frequency)
		return;

#ifdef CONFIG_MIPS_MT_SMTC
	setup_smtc_dummy_clockevent_device();

	/*
	 * On SMTC we only register VPE0's compare interrupt as clockevent
	 * device.
	 */
	if (cpu)
		return;
#endif

	if (!c0_compare_int_usable())
		return;

	/*
	 * With vectored interrupts things are getting platform specific.
	 * get_c0_compare_int is a hook to allow a platform to return the
	 * interrupt number of it's liking.
	 */
	irq = MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	if (get_c0_compare_int)
		irq = get_c0_compare_int();

	cd = &per_cpu(mips_clockevent_device, cpu);

	cd->name		= "MIPS";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;

	/* Calculate the min / max delta */
	cd->mult	= div_sc((unsigned long) mips_freq, NSEC_PER_SEC, 32);
	cd->shift		= 32;
	cd->max_delta_ns	= clockevent_delta2ns(0x7fffffff, cd);
	cd->min_delta_ns	= clockevent_delta2ns(0x300, cd);

	cd->rating		= 300;
	cd->irq			= irq;
#ifdef CONFIG_MIPS_MT_SMTC
	cd->cpumask		= CPU_MASK_ALL;
#else
	cd->cpumask		= cpumask_of_cpu(cpu);
#endif
	cd->set_next_event	= mips_next_event;
	cd->set_mode		= mips_set_mode;
	cd->event_handler	= mips_event_handler;

	clockevents_register_device(cd);

	if (!cp0_timer_irq_installed)
		return;

	cp0_timer_irq_installed = 1;

#ifdef CONFIG_MIPS_MT_SMTC
#define CPUCTR_IMASKBIT (0x100 << cp0_compare_irq)
	setup_irq_smtc(irq, &c0_compare_irqaction, CPUCTR_IMASKBIT);
#else
	setup_irq(irq, &c0_compare_irqaction);
#endif
}

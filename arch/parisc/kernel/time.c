// SPDX-License-Identifier: GPL-2.0
/*
 * Common time service routines for parisc machines.
 * based on arch/loongarch/kernel/time.c
 *
 * Copyright (C) 2024 Helge Deller <deller@gmx.de>
 */
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/sched_clock.h>
#include <linux/spinlock.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <asm/processor.h>

static u64 cr16_clock_freq;
static unsigned long clocktick;

int time_keeper_id;	/* CPU used for timekeeping */

static DEFINE_PER_CPU(struct clock_event_device, parisc_clockevent_device);

static void parisc_event_handler(struct clock_event_device *dev)
{
}

static int parisc_timer_next_event(unsigned long delta, struct clock_event_device *evt)
{
	unsigned long new_cr16;

	new_cr16 = mfctl(16) + delta;
	mtctl(new_cr16, 16);

	return 0;
}

irqreturn_t timer_interrupt(int irq, void *data)
{
	struct clock_event_device *cd;
	int cpu = smp_processor_id();

	cd = &per_cpu(parisc_clockevent_device, cpu);

	if (clockevent_state_periodic(cd))
		parisc_timer_next_event(clocktick, cd);

	if (clockevent_state_periodic(cd) || clockevent_state_oneshot(cd))
		cd->event_handler(cd);

	return IRQ_HANDLED;
}

static int parisc_set_state_oneshot(struct clock_event_device *evt)
{
	parisc_timer_next_event(clocktick, evt);

	return 0;
}

static int parisc_set_state_periodic(struct clock_event_device *evt)
{
	parisc_timer_next_event(clocktick, evt);

	return 0;
}

static int parisc_set_state_shutdown(struct clock_event_device *evt)
{
	return 0;
}

void parisc_clockevent_init(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned long min_delta = 0x600;	/* XXX */
	unsigned long max_delta = (1UL << (BITS_PER_LONG - 1));
	struct clock_event_device *cd;

	cd = &per_cpu(parisc_clockevent_device, cpu);

	cd->name = "cr16_clockevent";
	cd->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC |
			CLOCK_EVT_FEAT_PERCPU;

	cd->irq = TIMER_IRQ;
	cd->rating = 320;
	cd->cpumask = cpumask_of(cpu);
	cd->set_state_oneshot = parisc_set_state_oneshot;
	cd->set_state_oneshot_stopped = parisc_set_state_shutdown;
	cd->set_state_periodic = parisc_set_state_periodic;
	cd->set_state_shutdown = parisc_set_state_shutdown;
	cd->set_next_event = parisc_timer_next_event;
	cd->event_handler = parisc_event_handler;

	clockevents_config_and_register(cd, cr16_clock_freq, min_delta, max_delta);
}

unsigned long notrace profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (regs->gr[0] & PSW_N)
		pc -= 4;

#ifdef CONFIG_SMP
	if (in_lock_functions(pc))
		pc = regs->gr[2];
#endif

	return pc;
}
EXPORT_SYMBOL(profile_pc);

#if IS_ENABLED(CONFIG_RTC_DRV_GENERIC)
static int rtc_generic_get_time(struct device *dev, struct rtc_time *tm)
{
	struct pdc_tod tod_data;

	memset(tm, 0, sizeof(*tm));
	if (pdc_tod_read(&tod_data) < 0)
		return -EOPNOTSUPP;

	/* we treat tod_sec as unsigned, so this can work until year 2106 */
	rtc_time64_to_tm(tod_data.tod_sec, tm);
	return 0;
}

static int rtc_generic_set_time(struct device *dev, struct rtc_time *tm)
{
	time64_t secs = rtc_tm_to_time64(tm);
	int ret;

	/* hppa has Y2K38 problem: pdc_tod_set() takes an u32 value! */
	ret = pdc_tod_set(secs, 0);
	if (ret != 0) {
		pr_warn("pdc_tod_set(%lld) returned error %d\n", secs, ret);
		if (ret == PDC_INVALID_ARG)
			return -EINVAL;
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct rtc_class_ops rtc_generic_ops = {
	.read_time = rtc_generic_get_time,
	.set_time = rtc_generic_set_time,
};

static int __init rtc_init(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_data(NULL, "rtc-generic", -1,
					     &rtc_generic_ops,
					     sizeof(rtc_generic_ops));

	return PTR_ERR_OR_ZERO(pdev);
}
device_initcall(rtc_init);
#endif

void read_persistent_clock64(struct timespec64 *ts)
{
	static struct pdc_tod tod_data;
	if (pdc_tod_read(&tod_data) == 0) {
		ts->tv_sec = tod_data.tod_sec;
		ts->tv_nsec = tod_data.tod_usec * 1000;
	} else {
		printk(KERN_ERR "Error reading tod clock\n");
	        ts->tv_sec = 0;
		ts->tv_nsec = 0;
	}
}

static u64 notrace read_cr16_sched_clock(void)
{
	return get_cycles();
}

static u64 notrace read_cr16(struct clocksource *cs)
{
	return get_cycles();
}

static struct clocksource clocksource_cr16 = {
	.name			= "cr16",
	.rating			= 300,
	.read			= read_cr16,
	.mask			= CLOCKSOURCE_MASK(BITS_PER_LONG),
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS |
					CLOCK_SOURCE_VALID_FOR_HRES |
					CLOCK_SOURCE_MUST_VERIFY |
					CLOCK_SOURCE_VERIFY_PERCPU,
};


/*
 * timer interrupt and sched_clock() initialization
 */

void __init time_init(void)
{
	cr16_clock_freq = 100 * PAGE0->mem_10msec;  /* Hz */
	clocktick = cr16_clock_freq / HZ;

	/* register as sched_clock source */
	sched_clock_register(read_cr16_sched_clock, BITS_PER_LONG, cr16_clock_freq);

	parisc_clockevent_init();

	/* register at clocksource framework */
	clocksource_register_hz(&clocksource_cr16, cr16_clock_freq);
}

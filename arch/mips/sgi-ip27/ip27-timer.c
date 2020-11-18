// SPDX-License-Identifier: GPL-2.0
/*
 * Copytight (C) 1999, 2000, 05, 06 Ralf Baechle (ralf@linux-mips.org)
 * Copytight (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/bcd.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched_clock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/param.h>
#include <linux/smp.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/mm.h>
#include <linux/platform_device.h>

#include <asm/time.h>
#include <asm/sgialib.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/arch.h>
#include <asm/sn/addrs.h>
#include <asm/sn/agent.h>

#include "ip27-common.h"

static int rt_next_event(unsigned long delta, struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	int slice = cputoslice(cpu);
	unsigned long cnt;

	cnt = LOCAL_HUB_L(PI_RT_COUNT);
	cnt += delta;
	LOCAL_HUB_S(PI_RT_COMPARE_A + PI_COUNT_OFFSET * slice, cnt);

	return LOCAL_HUB_L(PI_RT_COUNT) >= cnt ? -ETIME : 0;
}

static DEFINE_PER_CPU(struct clock_event_device, hub_rt_clockevent);
static DEFINE_PER_CPU(char [11], hub_rt_name);

static irqreturn_t hub_rt_counter_handler(int irq, void *dev_id)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd = &per_cpu(hub_rt_clockevent, cpu);
	int slice = cputoslice(cpu);

	/*
	 * Ack
	 */
	LOCAL_HUB_S(PI_RT_PEND_A + PI_COUNT_OFFSET * slice, 0);
	cd->event_handler(cd);

	return IRQ_HANDLED;
}

struct irqaction hub_rt_irqaction = {
	.handler	= hub_rt_counter_handler,
	.percpu_dev_id	= &hub_rt_clockevent,
	.flags		= IRQF_PERCPU | IRQF_TIMER,
	.name		= "hub-rt",
};

/*
 * This is a hack; we really need to figure these values out dynamically
 *
 * Since 800 ns works very well with various HUB frequencies, such as
 * 360, 380, 390 and 400 MHZ, we use 800 ns rtc cycle time.
 *
 * Ralf: which clock rate is used to feed the counter?
 */
#define NSEC_PER_CYCLE		800
#define CYCLES_PER_SEC		(NSEC_PER_SEC / NSEC_PER_CYCLE)

void hub_rt_clock_event_init(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd = &per_cpu(hub_rt_clockevent, cpu);
	unsigned char *name = per_cpu(hub_rt_name, cpu);

	sprintf(name, "hub-rt %d", cpu);
	cd->name		= name;
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;
	clockevent_set_clock(cd, CYCLES_PER_SEC);
	cd->max_delta_ns	= clockevent_delta2ns(0xfffffffffffff, cd);
	cd->max_delta_ticks	= 0xfffffffffffff;
	cd->min_delta_ns	= clockevent_delta2ns(0x300, cd);
	cd->min_delta_ticks	= 0x300;
	cd->rating		= 200;
	cd->irq			= IP27_RT_TIMER_IRQ;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= rt_next_event;
	clockevents_register_device(cd);

	enable_percpu_irq(IP27_RT_TIMER_IRQ, IRQ_TYPE_NONE);
}

static void __init hub_rt_clock_event_global_init(void)
{
	irq_set_handler(IP27_RT_TIMER_IRQ, handle_percpu_devid_irq);
	irq_set_percpu_devid(IP27_RT_TIMER_IRQ);
	setup_percpu_irq(IP27_RT_TIMER_IRQ, &hub_rt_irqaction);
}

static u64 hub_rt_read(struct clocksource *cs)
{
	return REMOTE_HUB_L(cputonasid(0), PI_RT_COUNT);
}

struct clocksource hub_rt_clocksource = {
	.name	= "HUB-RT",
	.rating = 200,
	.read	= hub_rt_read,
	.mask	= CLOCKSOURCE_MASK(52),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 notrace hub_rt_read_sched_clock(void)
{
	return REMOTE_HUB_L(cputonasid(0), PI_RT_COUNT);
}

static void __init hub_rt_clocksource_init(void)
{
	struct clocksource *cs = &hub_rt_clocksource;

	clocksource_register_hz(cs, CYCLES_PER_SEC);

	sched_clock_register(hub_rt_read_sched_clock, 52, CYCLES_PER_SEC);
}

void __init plat_time_init(void)
{
	hub_rt_clocksource_init();
	hub_rt_clock_event_global_init();
	hub_rt_clock_event_init();
}

void hub_rtc_init(nasid_t nasid)
{

	/*
	 * We only need to initialize the current node.
	 * If this is not the current node then it is a cpuless
	 * node and timeouts will not happen there.
	 */
	if (get_nasid() == nasid) {
		LOCAL_HUB_S(PI_RT_EN_A, 1);
		LOCAL_HUB_S(PI_RT_EN_B, 1);
		LOCAL_HUB_S(PI_PROF_EN_A, 0);
		LOCAL_HUB_S(PI_PROF_EN_B, 0);
		LOCAL_HUB_S(PI_RT_COUNT, 0);
		LOCAL_HUB_S(PI_RT_PEND_A, 0);
		LOCAL_HUB_S(PI_RT_PEND_B, 0);
	}
}

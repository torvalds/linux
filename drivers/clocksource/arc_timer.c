// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-17 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

/* ARC700 has two 32bit independent prog Timers: TIMER0 and TIMER1, Each can be
 * programmed to go from @count to @limit and optionally interrupt.
 * We've designated TIMER0 for clockevents and TIMER1 for clocksource
 *
 * ARCv2 based HS38 cores have RTC (in-core) and GFRC (inside ARConnect/MCIP)
 * which are suitable for UP and SMP based clocksources respectively
 */

#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <soc/arc/timers.h>
#include <soc/arc/mcip.h>


static unsigned long arc_timer_freq;

static int noinline arc_get_timer_clk(struct device_node *node)
{
	struct clk *clk;
	int ret;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("timer missing clk\n");
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("Couldn't enable parent clk\n");
		return ret;
	}

	arc_timer_freq = clk_get_rate(clk);

	return 0;
}

/********** Clock Source Device *********/

#ifdef CONFIG_ARC_TIMERS_64BIT

static u64 arc_read_gfrc(struct clocksource *cs)
{
	unsigned long flags;
	u32 l, h;

	/*
	 * From a programming model pov, there seems to be just one instance of
	 * MCIP_CMD/MCIP_READBACK however micro-architecturally there's
	 * an instance PER ARC CORE (not per cluster), and there are dedicated
	 * hardware decode logic (per core) inside ARConnect to handle
	 * simultaneous read/write accesses from cores via those two registers.
	 * So several concurrent commands to ARConnect are OK if they are
	 * trying to access two different sub-components (like GFRC,
	 * inter-core interrupt, etc...). HW also supports simultaneously
	 * accessing GFRC by multiple cores.
	 * That's why it is safe to disable hard interrupts on the local CPU
	 * before access to GFRC instead of taking global MCIP spinlock
	 * defined in arch/arc/kernel/mcip.c
	 */
	local_irq_save(flags);

	__mcip_cmd(CMD_GFRC_READ_LO, 0);
	l = read_aux_reg(ARC_REG_MCIP_READBACK);

	__mcip_cmd(CMD_GFRC_READ_HI, 0);
	h = read_aux_reg(ARC_REG_MCIP_READBACK);

	local_irq_restore(flags);

	return (((u64)h) << 32) | l;
}

static notrace u64 arc_gfrc_clock_read(void)
{
	return arc_read_gfrc(NULL);
}

static struct clocksource arc_counter_gfrc = {
	.name   = "ARConnect GFRC",
	.rating = 400,
	.read   = arc_read_gfrc,
	.mask   = CLOCKSOURCE_MASK(64),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init arc_cs_setup_gfrc(struct device_node *node)
{
	struct mcip_bcr mp;
	int ret;

	READ_BCR(ARC_REG_MCIP_BCR, mp);
	if (!mp.gfrc) {
		pr_warn("Global-64-bit-Ctr clocksource not detected\n");
		return -ENXIO;
	}

	ret = arc_get_timer_clk(node);
	if (ret)
		return ret;

	sched_clock_register(arc_gfrc_clock_read, 64, arc_timer_freq);

	return clocksource_register_hz(&arc_counter_gfrc, arc_timer_freq);
}
TIMER_OF_DECLARE(arc_gfrc, "snps,archs-timer-gfrc", arc_cs_setup_gfrc);

#define AUX_RTC_CTRL	0x103
#define AUX_RTC_LOW	0x104
#define AUX_RTC_HIGH	0x105

static u64 arc_read_rtc(struct clocksource *cs)
{
	unsigned long status;
	u32 l, h;

	/*
	 * hardware has an internal state machine which tracks readout of
	 * low/high and updates the CTRL.status if
	 *  - interrupt/exception taken between the two reads
	 *  - high increments after low has been read
	 */
	do {
		l = read_aux_reg(AUX_RTC_LOW);
		h = read_aux_reg(AUX_RTC_HIGH);
		status = read_aux_reg(AUX_RTC_CTRL);
	} while (!(status & _BITUL(31)));

	return (((u64)h) << 32) | l;
}

static notrace u64 arc_rtc_clock_read(void)
{
	return arc_read_rtc(NULL);
}

static struct clocksource arc_counter_rtc = {
	.name   = "ARCv2 RTC",
	.rating = 350,
	.read   = arc_read_rtc,
	.mask   = CLOCKSOURCE_MASK(64),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init arc_cs_setup_rtc(struct device_node *node)
{
	struct bcr_timer timer;
	int ret;

	READ_BCR(ARC_REG_TIMERS_BCR, timer);
	if (!timer.rtc) {
		pr_warn("Local-64-bit-Ctr clocksource not detected\n");
		return -ENXIO;
	}

	/* Local to CPU hence not usable in SMP */
	if (IS_ENABLED(CONFIG_SMP)) {
		pr_warn("Local-64-bit-Ctr not usable in SMP\n");
		return -EINVAL;
	}

	ret = arc_get_timer_clk(node);
	if (ret)
		return ret;

	write_aux_reg(AUX_RTC_CTRL, 1);

	sched_clock_register(arc_rtc_clock_read, 64, arc_timer_freq);

	return clocksource_register_hz(&arc_counter_rtc, arc_timer_freq);
}
TIMER_OF_DECLARE(arc_rtc, "snps,archs-timer-rtc", arc_cs_setup_rtc);

#endif

/*
 * 32bit TIMER1 to keep counting monotonically and wraparound
 */

static u64 arc_read_timer1(struct clocksource *cs)
{
	return (u64) read_aux_reg(ARC_REG_TIMER1_CNT);
}

static notrace u64 arc_timer1_clock_read(void)
{
	return arc_read_timer1(NULL);
}

static struct clocksource arc_counter_timer1 = {
	.name   = "ARC Timer1",
	.rating = 300,
	.read   = arc_read_timer1,
	.mask   = CLOCKSOURCE_MASK(32),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init arc_cs_setup_timer1(struct device_node *node)
{
	int ret;

	/* Local to CPU hence not usable in SMP */
	if (IS_ENABLED(CONFIG_SMP))
		return -EINVAL;

	ret = arc_get_timer_clk(node);
	if (ret)
		return ret;

	write_aux_reg(ARC_REG_TIMER1_LIMIT, ARC_TIMERN_MAX);
	write_aux_reg(ARC_REG_TIMER1_CNT, 0);
	write_aux_reg(ARC_REG_TIMER1_CTRL, TIMER_CTRL_NH);

	sched_clock_register(arc_timer1_clock_read, 32, arc_timer_freq);

	return clocksource_register_hz(&arc_counter_timer1, arc_timer_freq);
}

/********** Clock Event Device *********/

static int arc_timer_irq;

/*
 * Arm the timer to interrupt after @cycles
 * The distinction for oneshot/periodic is done in arc_event_timer_ack() below
 */
static void arc_timer_event_setup(unsigned int cycles)
{
	write_aux_reg(ARC_REG_TIMER0_LIMIT, cycles);
	write_aux_reg(ARC_REG_TIMER0_CNT, 0);	/* start from 0 */

	write_aux_reg(ARC_REG_TIMER0_CTRL, TIMER_CTRL_IE | TIMER_CTRL_NH);
}


static int arc_clkevent_set_next_event(unsigned long delta,
				       struct clock_event_device *dev)
{
	arc_timer_event_setup(delta);
	return 0;
}

static int arc_clkevent_set_periodic(struct clock_event_device *dev)
{
	/*
	 * At X Hz, 1 sec = 1000ms -> X cycles;
	 *		      10ms -> X / 100 cycles
	 */
	arc_timer_event_setup(arc_timer_freq / HZ);
	return 0;
}

static DEFINE_PER_CPU(struct clock_event_device, arc_clockevent_device) = {
	.name			= "ARC Timer0",
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC,
	.rating			= 300,
	.set_next_event		= arc_clkevent_set_next_event,
	.set_state_periodic	= arc_clkevent_set_periodic,
};

static irqreturn_t timer_irq_handler(int irq, void *dev_id)
{
	/*
	 * Note that generic IRQ core could have passed @evt for @dev_id if
	 * irq_set_chip_and_handler() asked for handle_percpu_devid_irq()
	 */
	struct clock_event_device *evt = this_cpu_ptr(&arc_clockevent_device);
	int irq_reenable = clockevent_state_periodic(evt);

	/*
	 * 1. ACK the interrupt
	 *    - For ARC700, any write to CTRL reg ACKs it, so just rewrite
	 *      Count when [N]ot [H]alted bit.
	 *    - For HS3x, it is a bit subtle. On taken count-down interrupt,
	 *      IP bit [3] is set, which needs to be cleared for ACK'ing.
	 *      The write below can only update the other two bits, hence
	 *      explicitly clears IP bit
	 * 2. Re-arm interrupt if periodic by writing to IE bit [0]
	 */
	write_aux_reg(ARC_REG_TIMER0_CTRL, irq_reenable | TIMER_CTRL_NH);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}


static int arc_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *evt = this_cpu_ptr(&arc_clockevent_device);

	evt->cpumask = cpumask_of(smp_processor_id());

	clockevents_config_and_register(evt, arc_timer_freq, 0, ARC_TIMERN_MAX);
	enable_percpu_irq(arc_timer_irq, 0);
	return 0;
}

static int arc_timer_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(arc_timer_irq);
	return 0;
}

/*
 * clockevent setup for boot CPU
 */
static int __init arc_clockevent_setup(struct device_node *node)
{
	struct clock_event_device *evt = this_cpu_ptr(&arc_clockevent_device);
	int ret;

	arc_timer_irq = irq_of_parse_and_map(node, 0);
	if (arc_timer_irq <= 0) {
		pr_err("clockevent: missing irq\n");
		return -EINVAL;
	}

	ret = arc_get_timer_clk(node);
	if (ret) {
		pr_err("clockevent: missing clk\n");
		return ret;
	}

	/* Needs apriori irq_set_percpu_devid() done in intc map function */
	ret = request_percpu_irq(arc_timer_irq, timer_irq_handler,
				 "Timer0 (per-cpu-tick)", evt);
	if (ret) {
		pr_err("clockevent: unable to request irq\n");
		return ret;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ARC_TIMER_STARTING,
				"clockevents/arc/timer:starting",
				arc_timer_starting_cpu,
				arc_timer_dying_cpu);
	if (ret) {
		pr_err("Failed to setup hotplug state\n");
		return ret;
	}
	return 0;
}

static int __init arc_of_timer_init(struct device_node *np)
{
	static int init_count = 0;
	int ret;

	if (!init_count) {
		init_count = 1;
		ret = arc_clockevent_setup(np);
	} else {
		ret = arc_cs_setup_timer1(np);
	}

	return ret;
}
TIMER_OF_DECLARE(arc_clkevt, "snps,arc-timer", arc_of_timer_init);

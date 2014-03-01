/*
 * Copyright (C) 2007-2013 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012-2013 Xilinx, Inc.
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched_clock.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/cpuinfo.h>

static void __iomem *timer_baseaddr;

static unsigned int freq_div_hz;
static unsigned int timer_clock_freq;

#define TCSR0	(0x00)
#define TLR0	(0x04)
#define TCR0	(0x08)
#define TCSR1	(0x10)
#define TLR1	(0x14)
#define TCR1	(0x18)

#define TCSR_MDT	(1<<0)
#define TCSR_UDT	(1<<1)
#define TCSR_GENT	(1<<2)
#define TCSR_CAPT	(1<<3)
#define TCSR_ARHT	(1<<4)
#define TCSR_LOAD	(1<<5)
#define TCSR_ENIT	(1<<6)
#define TCSR_ENT	(1<<7)
#define TCSR_TINT	(1<<8)
#define TCSR_PWMA	(1<<9)
#define TCSR_ENALL	(1<<10)

static inline void xilinx_timer0_stop(void)
{
	out_be32(timer_baseaddr + TCSR0,
		 in_be32(timer_baseaddr + TCSR0) & ~TCSR_ENT);
}

static inline void xilinx_timer0_start_periodic(unsigned long load_val)
{
	if (!load_val)
		load_val = 1;
	/* loading value to timer reg */
	out_be32(timer_baseaddr + TLR0, load_val);

	/* load the initial value */
	out_be32(timer_baseaddr + TCSR0, TCSR_LOAD);

	/* see timer data sheet for detail
	 * !ENALL - don't enable 'em all
	 * !PWMA - disable pwm
	 * TINT - clear interrupt status
	 * ENT- enable timer itself
	 * ENIT - enable interrupt
	 * !LOAD - clear the bit to let go
	 * ARHT - auto reload
	 * !CAPT - no external trigger
	 * !GENT - no external signal
	 * UDT - set the timer as down counter
	 * !MDT0 - generate mode
	 */
	out_be32(timer_baseaddr + TCSR0,
			TCSR_TINT|TCSR_ENIT|TCSR_ENT|TCSR_ARHT|TCSR_UDT);
}

static inline void xilinx_timer0_start_oneshot(unsigned long load_val)
{
	if (!load_val)
		load_val = 1;
	/* loading value to timer reg */
	out_be32(timer_baseaddr + TLR0, load_val);

	/* load the initial value */
	out_be32(timer_baseaddr + TCSR0, TCSR_LOAD);

	out_be32(timer_baseaddr + TCSR0,
			TCSR_TINT|TCSR_ENIT|TCSR_ENT|TCSR_ARHT|TCSR_UDT);
}

static int xilinx_timer_set_next_event(unsigned long delta,
					struct clock_event_device *dev)
{
	pr_debug("%s: next event, delta %x\n", __func__, (u32)delta);
	xilinx_timer0_start_oneshot(delta);
	return 0;
}

static void xilinx_timer_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_info("%s: periodic\n", __func__);
		xilinx_timer0_start_periodic(freq_div_hz);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		pr_info("%s: oneshot\n", __func__);
		break;
	case CLOCK_EVT_MODE_UNUSED:
		pr_info("%s: unused\n", __func__);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
		pr_info("%s: shutdown\n", __func__);
		xilinx_timer0_stop();
		break;
	case CLOCK_EVT_MODE_RESUME:
		pr_info("%s: resume\n", __func__);
		break;
	}
}

static struct clock_event_device clockevent_xilinx_timer = {
	.name		= "xilinx_clockevent",
	.features       = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 8,
	.rating		= 300,
	.set_next_event	= xilinx_timer_set_next_event,
	.set_mode	= xilinx_timer_set_mode,
};

static inline void timer_ack(void)
{
	out_be32(timer_baseaddr + TCSR0, in_be32(timer_baseaddr + TCSR0));
}

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_xilinx_timer;
#ifdef CONFIG_HEART_BEAT
	heartbeat();
#endif
	timer_ack();
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction timer_irqaction = {
	.handler = timer_interrupt,
	.flags = IRQF_TIMER,
	.name = "timer",
	.dev_id = &clockevent_xilinx_timer,
};

static __init void xilinx_clockevent_init(void)
{
	clockevent_xilinx_timer.mult =
		div_sc(timer_clock_freq, NSEC_PER_SEC,
				clockevent_xilinx_timer.shift);
	clockevent_xilinx_timer.max_delta_ns =
		clockevent_delta2ns((u32)~0, &clockevent_xilinx_timer);
	clockevent_xilinx_timer.min_delta_ns =
		clockevent_delta2ns(1, &clockevent_xilinx_timer);
	clockevent_xilinx_timer.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_xilinx_timer);
}

static u64 xilinx_clock_read(void)
{
	return in_be32(timer_baseaddr + TCR1);
}

static cycle_t xilinx_read(struct clocksource *cs)
{
	/* reading actual value of timer 1 */
	return (cycle_t)xilinx_clock_read();
}

static struct timecounter xilinx_tc = {
	.cc = NULL,
};

static cycle_t xilinx_cc_read(const struct cyclecounter *cc)
{
	return xilinx_read(NULL);
}

static struct cyclecounter xilinx_cc = {
	.read = xilinx_cc_read,
	.mask = CLOCKSOURCE_MASK(32),
	.shift = 8,
};

static int __init init_xilinx_timecounter(void)
{
	xilinx_cc.mult = div_sc(timer_clock_freq, NSEC_PER_SEC,
				xilinx_cc.shift);

	timecounter_init(&xilinx_tc, &xilinx_cc, sched_clock());

	return 0;
}

static struct clocksource clocksource_microblaze = {
	.name		= "xilinx_clocksource",
	.rating		= 300,
	.read		= xilinx_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init xilinx_clocksource_init(void)
{
	if (clocksource_register_hz(&clocksource_microblaze, timer_clock_freq))
		panic("failed to register clocksource");

	/* stop timer1 */
	out_be32(timer_baseaddr + TCSR1,
		 in_be32(timer_baseaddr + TCSR1) & ~TCSR_ENT);
	/* start timer1 - up counting without interrupt */
	out_be32(timer_baseaddr + TCSR1, TCSR_TINT|TCSR_ENT|TCSR_ARHT);

	/* register timecounter - for ftrace support */
	init_xilinx_timecounter();
	return 0;
}

static void __init xilinx_timer_init(struct device_node *timer)
{
	struct clk *clk;
	static int initialized;
	u32 irq;
	u32 timer_num = 1;

	if (initialized)
		return;

	initialized = 1;

	timer_baseaddr = of_iomap(timer, 0);
	if (!timer_baseaddr) {
		pr_err("ERROR: invalid timer base address\n");
		BUG();
	}

	irq = irq_of_parse_and_map(timer, 0);

	of_property_read_u32(timer, "xlnx,one-timer-only", &timer_num);
	if (timer_num) {
		pr_emerg("Please enable two timers in HW\n");
		BUG();
	}

	pr_info("%s: irq=%d\n", timer->full_name, irq);

	clk = of_clk_get(timer, 0);
	if (IS_ERR(clk)) {
		pr_err("ERROR: timer CCF input clock not found\n");
		/* If there is clock-frequency property than use it */
		of_property_read_u32(timer, "clock-frequency",
				    &timer_clock_freq);
	} else {
		timer_clock_freq = clk_get_rate(clk);
	}

	if (!timer_clock_freq) {
		pr_err("ERROR: Using CPU clock frequency\n");
		timer_clock_freq = cpuinfo.cpu_clock_freq;
	}

	freq_div_hz = timer_clock_freq / HZ;

	setup_irq(irq, &timer_irqaction);
#ifdef CONFIG_HEART_BEAT
	setup_heartbeat();
#endif
	xilinx_clocksource_init();
	xilinx_clockevent_init();

	sched_clock_register(xilinx_clock_read, 32, timer_clock_freq);
}

CLOCKSOURCE_OF_DECLARE(xilinx_timer, "xlnx,xps-timer-1.00.a",
		       xilinx_timer_init);

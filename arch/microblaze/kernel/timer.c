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
#include <linux/sched/clock.h>
#include <linux/sched_clock.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/timecounter.h>
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

static unsigned int (*read_fn)(void __iomem *);
static void (*write_fn)(u32, void __iomem *);

static void timer_write32(u32 val, void __iomem *addr)
{
	iowrite32(val, addr);
}

static unsigned int timer_read32(void __iomem *addr)
{
	return ioread32(addr);
}

static void timer_write32_be(u32 val, void __iomem *addr)
{
	iowrite32be(val, addr);
}

static unsigned int timer_read32_be(void __iomem *addr)
{
	return ioread32be(addr);
}

static inline void xilinx_timer0_stop(void)
{
	write_fn(read_fn(timer_baseaddr + TCSR0) & ~TCSR_ENT,
		 timer_baseaddr + TCSR0);
}

static inline void xilinx_timer0_start_periodic(unsigned long load_val)
{
	if (!load_val)
		load_val = 1;
	/* loading value to timer reg */
	write_fn(load_val, timer_baseaddr + TLR0);

	/* load the initial value */
	write_fn(TCSR_LOAD, timer_baseaddr + TCSR0);

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
	write_fn(TCSR_TINT|TCSR_ENIT|TCSR_ENT|TCSR_ARHT|TCSR_UDT,
		 timer_baseaddr + TCSR0);
}

static inline void xilinx_timer0_start_oneshot(unsigned long load_val)
{
	if (!load_val)
		load_val = 1;
	/* loading value to timer reg */
	write_fn(load_val, timer_baseaddr + TLR0);

	/* load the initial value */
	write_fn(TCSR_LOAD, timer_baseaddr + TCSR0);

	write_fn(TCSR_TINT|TCSR_ENIT|TCSR_ENT|TCSR_ARHT|TCSR_UDT,
		 timer_baseaddr + TCSR0);
}

static int xilinx_timer_set_next_event(unsigned long delta,
					struct clock_event_device *dev)
{
	pr_debug("%s: next event, delta %x\n", __func__, (u32)delta);
	xilinx_timer0_start_oneshot(delta);
	return 0;
}

static int xilinx_timer_shutdown(struct clock_event_device *evt)
{
	pr_info("%s\n", __func__);
	xilinx_timer0_stop();
	return 0;
}

static int xilinx_timer_set_periodic(struct clock_event_device *evt)
{
	pr_info("%s\n", __func__);
	xilinx_timer0_start_periodic(freq_div_hz);
	return 0;
}

static struct clock_event_device clockevent_xilinx_timer = {
	.name			= "xilinx_clockevent",
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC,
	.shift			= 8,
	.rating			= 300,
	.set_next_event		= xilinx_timer_set_next_event,
	.set_state_shutdown	= xilinx_timer_shutdown,
	.set_state_periodic	= xilinx_timer_set_periodic,
};

static inline void timer_ack(void)
{
	write_fn(read_fn(timer_baseaddr + TCSR0), timer_baseaddr + TCSR0);
}

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_xilinx_timer;
#ifdef CONFIG_HEART_BEAT
	microblaze_heartbeat();
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

static __init int xilinx_clockevent_init(void)
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

	return 0;
}

static u64 xilinx_clock_read(void)
{
	return read_fn(timer_baseaddr + TCR1);
}

static u64 xilinx_read(struct clocksource *cs)
{
	/* reading actual value of timer 1 */
	return (u64)xilinx_clock_read();
}

static struct timecounter xilinx_tc = {
	.cc = NULL,
};

static u64 xilinx_cc_read(const struct cyclecounter *cc)
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
	int ret;

	ret = clocksource_register_hz(&clocksource_microblaze,
				      timer_clock_freq);
	if (ret) {
		pr_err("failed to register clocksource");
		return ret;
	}

	/* stop timer1 */
	write_fn(read_fn(timer_baseaddr + TCSR1) & ~TCSR_ENT,
		 timer_baseaddr + TCSR1);
	/* start timer1 - up counting without interrupt */
	write_fn(TCSR_TINT|TCSR_ENT|TCSR_ARHT, timer_baseaddr + TCSR1);

	/* register timecounter - for ftrace support */
	return init_xilinx_timecounter();
}

static int __init xilinx_timer_init(struct device_node *timer)
{
	struct clk *clk;
	static int initialized;
	u32 irq;
	u32 timer_num = 1;
	int ret;

	if (initialized)
		return -EINVAL;

	initialized = 1;

	timer_baseaddr = of_iomap(timer, 0);
	if (!timer_baseaddr) {
		pr_err("ERROR: invalid timer base address\n");
		return -ENXIO;
	}

	write_fn = timer_write32;
	read_fn = timer_read32;

	write_fn(TCSR_MDT, timer_baseaddr + TCSR0);
	if (!(read_fn(timer_baseaddr + TCSR0) & TCSR_MDT)) {
		write_fn = timer_write32_be;
		read_fn = timer_read32_be;
	}

	irq = irq_of_parse_and_map(timer, 0);
	if (irq <= 0) {
		pr_err("Failed to parse and map irq");
		return -EINVAL;
	}

	of_property_read_u32(timer, "xlnx,one-timer-only", &timer_num);
	if (timer_num) {
		pr_err("Please enable two timers in HW\n");
		return -EINVAL;
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

	ret = setup_irq(irq, &timer_irqaction);
	if (ret) {
		pr_err("Failed to setup IRQ");
		return ret;
	}

#ifdef CONFIG_HEART_BEAT
	microblaze_setup_heartbeat();
#endif

	ret = xilinx_clocksource_init();
	if (ret)
		return ret;

	ret = xilinx_clockevent_init();
	if (ret)
		return ret;

	sched_clock_register(xilinx_clock_read, 32, timer_clock_freq);

	return 0;
}

CLOCKSOURCE_OF_DECLARE(xilinx_timer, "xlnx,xps-timer-1.00.a",
		       xilinx_timer_init);

/*
 * Marvell Armada 370/XP SoC timer handling.
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Timer 0 is used as free-running clocksource, while timer 1 is
 * used as clock_event_device.
 *
 * ---
 * Clocksource driver for Armada 370 and Armada XP SoC.
 * This driver implements one compatible string for each SoC, given
 * each has its own characteristics:
 *
 *   * Armada 370 has no 25 MHz fixed timer.
 *
 *   * Armada XP cannot work properly without such 25 MHz fixed timer as
 *     doing otherwise leads to using a clocksource whose frequency varies
 *     when doing cpufreq frequency changes.
 *
 * See Documentation/devicetree/bindings/timer/marvell,armada-370-xp-timer.txt
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/sched_clock.h>

#include <asm/localtimer.h>
#include <linux/percpu.h>
/*
 * Timer block registers.
 */
#define TIMER_CTRL_OFF		0x0000
#define  TIMER0_EN		 BIT(0)
#define  TIMER0_RELOAD_EN	 BIT(1)
#define  TIMER0_25MHZ            BIT(11)
#define  TIMER0_DIV(div)         ((div) << 19)
#define  TIMER1_EN		 BIT(2)
#define  TIMER1_RELOAD_EN	 BIT(3)
#define  TIMER1_25MHZ            BIT(12)
#define  TIMER1_DIV(div)         ((div) << 22)
#define TIMER_EVENTS_STATUS	0x0004
#define  TIMER0_CLR_MASK         (~0x1)
#define  TIMER1_CLR_MASK         (~0x100)
#define TIMER0_RELOAD_OFF	0x0010
#define TIMER0_VAL_OFF		0x0014
#define TIMER1_RELOAD_OFF	0x0018
#define TIMER1_VAL_OFF		0x001c

#define LCL_TIMER_EVENTS_STATUS	0x0028
/* Global timers are connected to the coherency fabric clock, and the
   below divider reduces their incrementing frequency. */
#define TIMER_DIVIDER_SHIFT     5
#define TIMER_DIVIDER           (1 << TIMER_DIVIDER_SHIFT)

/*
 * SoC-specific data.
 */
static void __iomem *timer_base, *local_base;
static unsigned int timer_clk;
static bool timer25Mhz = true;

/*
 * Number of timer ticks per jiffy.
 */
static u32 ticks_per_jiffy;

static struct clock_event_device __percpu **percpu_armada_370_xp_evt;

static void timer_ctrl_clrset(u32 clr, u32 set)
{
	writel((readl(timer_base + TIMER_CTRL_OFF) & ~clr) | set,
		timer_base + TIMER_CTRL_OFF);
}

static void local_timer_ctrl_clrset(u32 clr, u32 set)
{
	writel((readl(local_base + TIMER_CTRL_OFF) & ~clr) | set,
		local_base + TIMER_CTRL_OFF);
}

static u32 notrace armada_370_xp_read_sched_clock(void)
{
	return ~readl(timer_base + TIMER0_VAL_OFF);
}

/*
 * Clockevent handling.
 */
static int
armada_370_xp_clkevt_next_event(unsigned long delta,
				struct clock_event_device *dev)
{
	/*
	 * Clear clockevent timer interrupt.
	 */
	writel(TIMER0_CLR_MASK, local_base + LCL_TIMER_EVENTS_STATUS);

	/*
	 * Setup new clockevent timer value.
	 */
	writel(delta, local_base + TIMER0_VAL_OFF);

	/*
	 * Enable the timer.
	 */
	local_timer_ctrl_clrset(TIMER0_RELOAD_EN,
				TIMER0_EN | TIMER0_DIV(TIMER_DIVIDER_SHIFT));
	return 0;
}

static void
armada_370_xp_clkevt_mode(enum clock_event_mode mode,
			  struct clock_event_device *dev)
{
	if (mode == CLOCK_EVT_MODE_PERIODIC) {

		/*
		 * Setup timer to fire at 1/HZ intervals.
		 */
		writel(ticks_per_jiffy - 1, local_base + TIMER0_RELOAD_OFF);
		writel(ticks_per_jiffy - 1, local_base + TIMER0_VAL_OFF);

		/*
		 * Enable timer.
		 */
		local_timer_ctrl_clrset(0, TIMER0_RELOAD_EN |
					   TIMER0_EN |
					   TIMER0_DIV(TIMER_DIVIDER_SHIFT));
	} else {
		/*
		 * Disable timer.
		 */
		local_timer_ctrl_clrset(TIMER0_EN, 0);

		/*
		 * ACK pending timer interrupt.
		 */
		writel(TIMER0_CLR_MASK, local_base + LCL_TIMER_EVENTS_STATUS);
	}
}

static struct clock_event_device armada_370_xp_clkevt = {
	.name		= "armada_370_xp_per_cpu_tick",
	.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 32,
	.rating		= 300,
	.set_next_event	= armada_370_xp_clkevt_next_event,
	.set_mode	= armada_370_xp_clkevt_mode,
};

static irqreturn_t armada_370_xp_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * ACK timer interrupt and call event handler.
	 */
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;

	writel(TIMER0_CLR_MASK, local_base + LCL_TIMER_EVENTS_STATUS);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

/*
 * Setup the local clock events for a CPU.
 */
static int armada_370_xp_timer_setup(struct clock_event_device *evt)
{
	u32 clr = 0, set = 0;
	int cpu = smp_processor_id();

	/* Use existing clock_event for cpu 0 */
	if (!smp_processor_id())
		return 0;

	if (timer25Mhz)
		set = TIMER0_25MHZ;
	else
		clr = TIMER0_25MHZ;
	local_timer_ctrl_clrset(clr, set);

	evt->name		= armada_370_xp_clkevt.name;
	evt->irq		= armada_370_xp_clkevt.irq;
	evt->features		= armada_370_xp_clkevt.features;
	evt->shift		= armada_370_xp_clkevt.shift;
	evt->rating		= armada_370_xp_clkevt.rating,
	evt->set_next_event	= armada_370_xp_clkevt_next_event,
	evt->set_mode		= armada_370_xp_clkevt_mode,
	evt->cpumask		= cpumask_of(cpu);

	*__this_cpu_ptr(percpu_armada_370_xp_evt) = evt;

	clockevents_config_and_register(evt, timer_clk, 1, 0xfffffffe);
	enable_percpu_irq(evt->irq, 0);

	return 0;
}

static void  armada_370_xp_timer_stop(struct clock_event_device *evt)
{
	evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	disable_percpu_irq(evt->irq);
}

static struct local_timer_ops armada_370_xp_local_timer_ops = {
	.setup	= armada_370_xp_timer_setup,
	.stop	=  armada_370_xp_timer_stop,
};

static void __init armada_370_xp_timer_common_init(struct device_node *np)
{
	u32 clr = 0, set = 0;
	int res;

	timer_base = of_iomap(np, 0);
	WARN_ON(!timer_base);
	local_base = of_iomap(np, 1);

	if (timer25Mhz)
		set = TIMER0_25MHZ;
	else
		clr = TIMER0_25MHZ;
	timer_ctrl_clrset(clr, set);
	local_timer_ctrl_clrset(clr, set);

	/*
	 * We use timer 0 as clocksource, and private(local) timer 0
	 * for clockevents
	 */
	armada_370_xp_clkevt.irq = irq_of_parse_and_map(np, 4);

	ticks_per_jiffy = (timer_clk + HZ / 2) / HZ;

	/*
	 * Set scale and timer for sched_clock.
	 */
	setup_sched_clock(armada_370_xp_read_sched_clock, 32, timer_clk);

	/*
	 * Setup free-running clocksource timer (interrupts
	 * disabled).
	 */
	writel(0xffffffff, timer_base + TIMER0_VAL_OFF);
	writel(0xffffffff, timer_base + TIMER0_RELOAD_OFF);

	timer_ctrl_clrset(0, TIMER0_EN | TIMER0_RELOAD_EN |
			     TIMER0_DIV(TIMER_DIVIDER_SHIFT));

	clocksource_mmio_init(timer_base + TIMER0_VAL_OFF,
			      "armada_370_xp_clocksource",
			      timer_clk, 300, 32, clocksource_mmio_readl_down);

	/* Register the clockevent on the private timer of CPU 0 */
	armada_370_xp_clkevt.cpumask = cpumask_of(0);
	clockevents_config_and_register(&armada_370_xp_clkevt,
					timer_clk, 1, 0xfffffffe);

	percpu_armada_370_xp_evt = alloc_percpu(struct clock_event_device *);


	/*
	 * Setup clockevent timer (interrupt-driven).
	 */
	*__this_cpu_ptr(percpu_armada_370_xp_evt) = &armada_370_xp_clkevt;
	res = request_percpu_irq(armada_370_xp_clkevt.irq,
				armada_370_xp_timer_interrupt,
				armada_370_xp_clkevt.name,
				percpu_armada_370_xp_evt);
	if (!res) {
		enable_percpu_irq(armada_370_xp_clkevt.irq, 0);
#ifdef CONFIG_LOCAL_TIMERS
		local_timer_register(&armada_370_xp_local_timer_ops);
#endif
	}
}

static void __init armada_xp_timer_init(struct device_node *np)
{
	/* The fixed 25MHz timer is required, timer25Mhz is true by default */
	timer_clk = 25000000;

	armada_370_xp_timer_common_init(np);
}
CLOCKSOURCE_OF_DECLARE(armada_xp, "marvell,armada-xp-timer",
		       armada_xp_timer_init);

static void __init armada_370_timer_init(struct device_node *np)
{
	struct clk *clk = of_clk_get(np, 0);

	WARN_ON(IS_ERR(clk));
	timer_clk = clk_get_rate(clk) / TIMER_DIVIDER;
	timer25Mhz = false;

	armada_370_xp_timer_common_init(np);
}
CLOCKSOURCE_OF_DECLARE(armada_370, "marvell,armada-370-timer",
		       armada_370_timer_init);

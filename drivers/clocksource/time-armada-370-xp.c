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
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/timer.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/sched_clock.h>
#include <linux/percpu.h>
#include <linux/time-armada-370-xp.h>

/*
 * Timer block registers.
 */
#define TIMER_CTRL_OFF		0x0000
#define  TIMER0_EN		 0x0001
#define  TIMER0_RELOAD_EN	 0x0002
#define  TIMER0_25MHZ            0x0800
#define  TIMER0_DIV(div)         ((div) << 19)
#define  TIMER1_EN		 0x0004
#define  TIMER1_RELOAD_EN	 0x0008
#define  TIMER1_25MHZ            0x1000
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

static struct clock_event_device __percpu *armada_370_xp_evt;

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
	u32 u;
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
	u = readl(local_base + TIMER_CTRL_OFF);
	u = ((u & ~TIMER0_RELOAD_EN) | TIMER0_EN |
	     TIMER0_DIV(TIMER_DIVIDER_SHIFT));
	writel(u, local_base + TIMER_CTRL_OFF);

	return 0;
}

static void
armada_370_xp_clkevt_mode(enum clock_event_mode mode,
			  struct clock_event_device *dev)
{
	u32 u;

	if (mode == CLOCK_EVT_MODE_PERIODIC) {

		/*
		 * Setup timer to fire at 1/HZ intervals.
		 */
		writel(ticks_per_jiffy - 1, local_base + TIMER0_RELOAD_OFF);
		writel(ticks_per_jiffy - 1, local_base + TIMER0_VAL_OFF);

		/*
		 * Enable timer.
		 */

		u = readl(local_base + TIMER_CTRL_OFF);

		writel((u | TIMER0_EN | TIMER0_RELOAD_EN |
			TIMER0_DIV(TIMER_DIVIDER_SHIFT)),
			local_base + TIMER_CTRL_OFF);
	} else {
		/*
		 * Disable timer.
		 */
		u = readl(local_base + TIMER_CTRL_OFF);
		writel(u & ~TIMER0_EN, local_base + TIMER_CTRL_OFF);

		/*
		 * ACK pending timer interrupt.
		 */
		writel(TIMER0_CLR_MASK, local_base + LCL_TIMER_EVENTS_STATUS);
	}
}

static int armada_370_xp_clkevt_irq;

static irqreturn_t armada_370_xp_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * ACK timer interrupt and call event handler.
	 */
	struct clock_event_device *evt = dev_id;

	writel(TIMER0_CLR_MASK, local_base + LCL_TIMER_EVENTS_STATUS);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

/*
 * Setup the local clock events for a CPU.
 */
static int __cpuinit armada_370_xp_timer_setup(struct clock_event_device *evt)
{
	u32 u;
	int cpu = smp_processor_id();

	u = readl(local_base + TIMER_CTRL_OFF);
	if (timer25Mhz)
		writel(u | TIMER0_25MHZ, local_base + TIMER_CTRL_OFF);
	else
		writel(u & ~TIMER0_25MHZ, local_base + TIMER_CTRL_OFF);

	evt->name		= "armada_370_xp_per_cpu_tick",
	evt->features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC;
	evt->shift		= 32,
	evt->rating		= 300,
	evt->set_next_event	= armada_370_xp_clkevt_next_event,
	evt->set_mode		= armada_370_xp_clkevt_mode,
	evt->irq		= armada_370_xp_clkevt_irq;
	evt->cpumask		= cpumask_of(cpu);

	clockevents_config_and_register(evt, timer_clk, 1, 0xfffffffe);
	enable_percpu_irq(evt->irq, 0);

	return 0;
}

static void __cpuinit armada_370_xp_timer_stop(struct clock_event_device *evt)
{
	evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	disable_percpu_irq(evt->irq);
}

static int __cpuinit armada_370_xp_timer_cpu_notify(struct notifier_block *self,
					   unsigned long action, void *hcpu)
{
	/*
	 * Grab cpu pointer in each case to avoid spurious
	 * preemptible warnings
	 */
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		armada_370_xp_timer_setup(this_cpu_ptr(armada_370_xp_evt));
		break;
	case CPU_DYING:
		armada_370_xp_timer_stop(this_cpu_ptr(armada_370_xp_evt));
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block armada_370_xp_timer_cpu_nb __cpuinitdata = {
	.notifier_call = armada_370_xp_timer_cpu_notify,
};

void __init armada_370_xp_timer_init(void)
{
	u32 u;
	struct device_node *np;
	int res;

	np = of_find_compatible_node(NULL, NULL, "marvell,armada-370-xp-timer");
	timer_base = of_iomap(np, 0);
	WARN_ON(!timer_base);
	local_base = of_iomap(np, 1);

	if (of_find_property(np, "marvell,timer-25Mhz", NULL)) {
		/* The fixed 25MHz timer is available so let's use it */
		u = readl(timer_base + TIMER_CTRL_OFF);
		writel(u | TIMER0_25MHZ,
		       timer_base + TIMER_CTRL_OFF);
		timer_clk = 25000000;
	} else {
		unsigned long rate = 0;
		struct clk *clk = of_clk_get(np, 0);
		WARN_ON(IS_ERR(clk));
		rate =  clk_get_rate(clk);

		u = readl(timer_base + TIMER_CTRL_OFF);
		writel(u & ~(TIMER0_25MHZ),
		       timer_base + TIMER_CTRL_OFF);

		timer_clk = rate / TIMER_DIVIDER;
		timer25Mhz = false;
	}

	/*
	 * We use timer 0 as clocksource, and private(local) timer 0
	 * for clockevents
	 */
	armada_370_xp_clkevt_irq = irq_of_parse_and_map(np, 4);

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

	u = readl(timer_base + TIMER_CTRL_OFF);

	writel((u | TIMER0_EN | TIMER0_RELOAD_EN |
		TIMER0_DIV(TIMER_DIVIDER_SHIFT)), timer_base + TIMER_CTRL_OFF);

	clocksource_mmio_init(timer_base + TIMER0_VAL_OFF,
			      "armada_370_xp_clocksource",
			      timer_clk, 300, 32, clocksource_mmio_readl_down);

	register_cpu_notifier(&armada_370_xp_timer_cpu_nb);

	armada_370_xp_evt = alloc_percpu(struct clock_event_device);


	/*
	 * Setup clockevent timer (interrupt-driven).
	 */
	res = request_percpu_irq(armada_370_xp_clkevt_irq,
				armada_370_xp_timer_interrupt,
				"armada_370_xp_per_cpu_tick",
				armada_370_xp_evt);
	/* Immediately configure the timer on the boot CPU */
	if (!res)
		armada_370_xp_timer_setup(this_cpu_ptr(armada_370_xp_evt));
}

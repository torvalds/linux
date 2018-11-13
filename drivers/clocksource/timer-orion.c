/*
 * Marvell Orion SoC timer handling.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Timer 0 is used as free-running clocksource, while timer 1 is
 * used as clock_event_device.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <linux/sched_clock.h>

#define TIMER_CTRL		0x00
#define  TIMER0_EN		BIT(0)
#define  TIMER0_RELOAD_EN	BIT(1)
#define  TIMER1_EN		BIT(2)
#define  TIMER1_RELOAD_EN	BIT(3)
#define TIMER0_RELOAD		0x10
#define TIMER0_VAL		0x14
#define TIMER1_RELOAD		0x18
#define TIMER1_VAL		0x1c

#define ORION_ONESHOT_MIN	1
#define ORION_ONESHOT_MAX	0xfffffffe

static void __iomem *timer_base;

static unsigned long notrace orion_read_timer(void)
{
	return ~readl(timer_base + TIMER0_VAL);
}

static struct delay_timer orion_delay_timer = {
	.read_current_timer = orion_read_timer,
};

static void orion_delay_timer_init(unsigned long rate)
{
	orion_delay_timer.freq = rate;
	register_current_timer_delay(&orion_delay_timer);
}

/*
 * Free-running clocksource handling.
 */
static u64 notrace orion_read_sched_clock(void)
{
	return ~readl(timer_base + TIMER0_VAL);
}

/*
 * Clockevent handling.
 */
static u32 ticks_per_jiffy;

static int orion_clkevt_next_event(unsigned long delta,
				   struct clock_event_device *dev)
{
	/* setup and enable one-shot timer */
	writel(delta, timer_base + TIMER1_VAL);
	atomic_io_modify(timer_base + TIMER_CTRL,
		TIMER1_RELOAD_EN | TIMER1_EN, TIMER1_EN);

	return 0;
}

static int orion_clkevt_shutdown(struct clock_event_device *dev)
{
	/* disable timer */
	atomic_io_modify(timer_base + TIMER_CTRL,
			 TIMER1_RELOAD_EN | TIMER1_EN, 0);
	return 0;
}

static int orion_clkevt_set_periodic(struct clock_event_device *dev)
{
	/* setup and enable periodic timer at 1/HZ intervals */
	writel(ticks_per_jiffy - 1, timer_base + TIMER1_RELOAD);
	writel(ticks_per_jiffy - 1, timer_base + TIMER1_VAL);
	atomic_io_modify(timer_base + TIMER_CTRL,
			 TIMER1_RELOAD_EN | TIMER1_EN,
			 TIMER1_RELOAD_EN | TIMER1_EN);
	return 0;
}

static struct clock_event_device orion_clkevt = {
	.name			= "orion_event",
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC,
	.shift			= 32,
	.rating			= 300,
	.set_next_event		= orion_clkevt_next_event,
	.set_state_shutdown	= orion_clkevt_shutdown,
	.set_state_periodic	= orion_clkevt_set_periodic,
	.set_state_oneshot	= orion_clkevt_shutdown,
	.tick_resume		= orion_clkevt_shutdown,
};

static irqreturn_t orion_clkevt_irq_handler(int irq, void *dev_id)
{
	orion_clkevt.event_handler(&orion_clkevt);
	return IRQ_HANDLED;
}

static struct irqaction orion_clkevt_irq = {
	.name		= "orion_event",
	.flags		= IRQF_TIMER,
	.handler	= orion_clkevt_irq_handler,
};

static int __init orion_timer_init(struct device_node *np)
{
	unsigned long rate;
	struct clk *clk;
	int irq, ret;

	/* timer registers are shared with watchdog timer */
	timer_base = of_iomap(np, 0);
	if (!timer_base) {
		pr_err("%pOFn: unable to map resource\n", np);
		return -ENXIO;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("%pOFn: unable to get clk\n", np);
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("Failed to prepare clock\n");
		return ret;
	}

	/* we are only interested in timer1 irq */
	irq = irq_of_parse_and_map(np, 1);
	if (irq <= 0) {
		pr_err("%pOFn: unable to parse timer1 irq\n", np);
		return -EINVAL;
	}

	rate = clk_get_rate(clk);

	/* setup timer0 as free-running clocksource */
	writel(~0, timer_base + TIMER0_VAL);
	writel(~0, timer_base + TIMER0_RELOAD);
	atomic_io_modify(timer_base + TIMER_CTRL,
		TIMER0_RELOAD_EN | TIMER0_EN,
		TIMER0_RELOAD_EN | TIMER0_EN);

	ret = clocksource_mmio_init(timer_base + TIMER0_VAL,
				    "orion_clocksource", rate, 300, 32,
				    clocksource_mmio_readl_down);
	if (ret) {
		pr_err("Failed to initialize mmio timer\n");
		return ret;
	}

	sched_clock_register(orion_read_sched_clock, 32, rate);

	/* setup timer1 as clockevent timer */
	ret = setup_irq(irq, &orion_clkevt_irq);
	if (ret) {
		pr_err("%pOFn: unable to setup irq\n", np);
		return ret;
	}

	ticks_per_jiffy = (clk_get_rate(clk) + HZ/2) / HZ;
	orion_clkevt.cpumask = cpumask_of(0);
	orion_clkevt.irq = irq;
	clockevents_config_and_register(&orion_clkevt, rate,
					ORION_ONESHOT_MIN, ORION_ONESHOT_MAX);


	orion_delay_timer_init(rate);

	return 0;
}
TIMER_OF_DECLARE(orion_timer, "marvell,orion-timer", orion_timer_init);

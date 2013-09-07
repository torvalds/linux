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
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <asm/sched_clock.h>

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
static DEFINE_SPINLOCK(timer_ctrl_lock);

/*
 * Thread-safe access to TIMER_CTRL register
 * (shared with watchdog timer)
 */
void orion_timer_ctrl_clrset(u32 clr, u32 set)
{
	spin_lock(&timer_ctrl_lock);
	writel((readl(timer_base + TIMER_CTRL) & ~clr) | set,
		timer_base + TIMER_CTRL);
	spin_unlock(&timer_ctrl_lock);
}
EXPORT_SYMBOL(orion_timer_ctrl_clrset);

/*
 * Free-running clocksource handling.
 */
static u32 notrace orion_read_sched_clock(void)
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
	orion_timer_ctrl_clrset(TIMER1_RELOAD_EN, TIMER1_EN);

	return 0;
}

static void orion_clkevt_mode(enum clock_event_mode mode,
			      struct clock_event_device *dev)
{
	if (mode == CLOCK_EVT_MODE_PERIODIC) {
		/* setup and enable periodic timer at 1/HZ intervals */
		writel(ticks_per_jiffy - 1, timer_base + TIMER1_RELOAD);
		writel(ticks_per_jiffy - 1, timer_base + TIMER1_VAL);
		orion_timer_ctrl_clrset(0, TIMER1_RELOAD_EN | TIMER1_EN);
	} else {
		/* disable timer */
		orion_timer_ctrl_clrset(TIMER1_RELOAD_EN | TIMER1_EN, 0);
	}
}

static struct clock_event_device orion_clkevt = {
	.name		= "orion_event",
	.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 32,
	.rating		= 300,
	.set_next_event	= orion_clkevt_next_event,
	.set_mode	= orion_clkevt_mode,
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

static void __init orion_timer_init(struct device_node *np)
{
	struct clk *clk;
	int irq;

	/* timer registers are shared with watchdog timer */
	timer_base = of_iomap(np, 0);
	if (!timer_base)
		panic("%s: unable to map resource\n", np->name);

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk))
		panic("%s: unable to get clk\n", np->name);
	clk_prepare_enable(clk);

	/* we are only interested in timer1 irq */
	irq = irq_of_parse_and_map(np, 1);
	if (irq <= 0)
		panic("%s: unable to parse timer1 irq\n", np->name);

	/* setup timer0 as free-running clocksource */
	writel(~0, timer_base + TIMER0_VAL);
	writel(~0, timer_base + TIMER0_RELOAD);
	orion_timer_ctrl_clrset(0, TIMER0_RELOAD_EN | TIMER0_EN);
	clocksource_mmio_init(timer_base + TIMER0_VAL, "orion_clocksource",
			      clk_get_rate(clk), 300, 32,
			      clocksource_mmio_readl_down);
	setup_sched_clock(orion_read_sched_clock, 32, clk_get_rate(clk));

	/* setup timer1 as clockevent timer */
	if (setup_irq(irq, &orion_clkevt_irq))
		panic("%s: unable to setup irq\n", np->name);

	ticks_per_jiffy = (clk_get_rate(clk) + HZ/2) / HZ;
	orion_clkevt.cpumask = cpumask_of(0);
	orion_clkevt.irq = irq;
	clockevents_config_and_register(&orion_clkevt, clk_get_rate(clk),
					ORION_ONESHOT_MIN, ORION_ONESHOT_MAX);
}
CLOCKSOURCE_OF_DECLARE(orion_timer, "marvell,orion-timer", orion_timer_init);

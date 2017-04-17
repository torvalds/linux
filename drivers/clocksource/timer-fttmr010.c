/*
 * Faraday Technology FTTMR010 timer driver
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on a rewrite of arch/arm/mach-gemini/timer.c:
 * Copyright (C) 2001-2006 Storlink, Corp.
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/sched_clock.h>
#include <linux/clk.h>

/*
 * Register definitions for the timers
 */
#define TIMER1_COUNT		(0x00)
#define TIMER1_LOAD		(0x04)
#define TIMER1_MATCH1		(0x08)
#define TIMER1_MATCH2		(0x0c)
#define TIMER2_COUNT		(0x10)
#define TIMER2_LOAD		(0x14)
#define TIMER2_MATCH1		(0x18)
#define TIMER2_MATCH2		(0x1c)
#define TIMER3_COUNT		(0x20)
#define TIMER3_LOAD		(0x24)
#define TIMER3_MATCH1		(0x28)
#define TIMER3_MATCH2		(0x2c)
#define TIMER_CR		(0x30)
#define TIMER_INTR_STATE	(0x34)
#define TIMER_INTR_MASK		(0x38)

#define TIMER_1_CR_ENABLE	(1 << 0)
#define TIMER_1_CR_CLOCK	(1 << 1)
#define TIMER_1_CR_INT		(1 << 2)
#define TIMER_2_CR_ENABLE	(1 << 3)
#define TIMER_2_CR_CLOCK	(1 << 4)
#define TIMER_2_CR_INT		(1 << 5)
#define TIMER_3_CR_ENABLE	(1 << 6)
#define TIMER_3_CR_CLOCK	(1 << 7)
#define TIMER_3_CR_INT		(1 << 8)
#define TIMER_1_CR_UPDOWN	(1 << 9)
#define TIMER_2_CR_UPDOWN	(1 << 10)
#define TIMER_3_CR_UPDOWN	(1 << 11)
#define TIMER_DEFAULT_FLAGS	(TIMER_1_CR_UPDOWN | \
				 TIMER_3_CR_ENABLE | \
				 TIMER_3_CR_UPDOWN)

#define TIMER_1_INT_MATCH1	(1 << 0)
#define TIMER_1_INT_MATCH2	(1 << 1)
#define TIMER_1_INT_OVERFLOW	(1 << 2)
#define TIMER_2_INT_MATCH1	(1 << 3)
#define TIMER_2_INT_MATCH2	(1 << 4)
#define TIMER_2_INT_OVERFLOW	(1 << 5)
#define TIMER_3_INT_MATCH1	(1 << 6)
#define TIMER_3_INT_MATCH2	(1 << 7)
#define TIMER_3_INT_OVERFLOW	(1 << 8)
#define TIMER_INT_ALL_MASK	0x1ff

static unsigned int tick_rate;
static void __iomem *base;

static u64 notrace fttmr010_read_sched_clock(void)
{
	return readl(base + TIMER3_COUNT);
}

static int fttmr010_timer_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	u32 cr;

	/* Setup the match register */
	cr = readl(base + TIMER1_COUNT);
	writel(cr + cycles, base + TIMER1_MATCH1);
	if (readl(base + TIMER1_COUNT) - cr > cycles)
		return -ETIME;

	return 0;
}

static int fttmr010_timer_shutdown(struct clock_event_device *evt)
{
	u32 cr;

	/*
	 * Disable also for oneshot: the set_next() call will arm the timer
	 * instead.
	 */
	/* Stop timer and interrupt. */
	cr = readl(base + TIMER_CR);
	cr &= ~(TIMER_1_CR_ENABLE | TIMER_1_CR_INT);
	writel(cr, base + TIMER_CR);

	/* Setup counter start from 0 */
	writel(0, base + TIMER1_COUNT);
	writel(0, base + TIMER1_LOAD);

	/* enable interrupt */
	cr = readl(base + TIMER_INTR_MASK);
	cr &= ~(TIMER_1_INT_OVERFLOW | TIMER_1_INT_MATCH2);
	cr |= TIMER_1_INT_MATCH1;
	writel(cr, base + TIMER_INTR_MASK);

	/* start the timer */
	cr = readl(base + TIMER_CR);
	cr |= TIMER_1_CR_ENABLE;
	writel(cr, base + TIMER_CR);

	return 0;
}

static int fttmr010_timer_set_periodic(struct clock_event_device *evt)
{
	u32 period = DIV_ROUND_CLOSEST(tick_rate, HZ);
	u32 cr;

	/* Stop timer and interrupt */
	cr = readl(base + TIMER_CR);
	cr &= ~(TIMER_1_CR_ENABLE | TIMER_1_CR_INT);
	writel(cr, base + TIMER_CR);

	/* Setup timer to fire at 1/HT intervals. */
	cr = 0xffffffff - (period - 1);
	writel(cr, base + TIMER1_COUNT);
	writel(cr, base + TIMER1_LOAD);

	/* enable interrupt on overflow */
	cr = readl(base + TIMER_INTR_MASK);
	cr &= ~(TIMER_1_INT_MATCH1 | TIMER_1_INT_MATCH2);
	cr |= TIMER_1_INT_OVERFLOW;
	writel(cr, base + TIMER_INTR_MASK);

	/* Start the timer */
	cr = readl(base + TIMER_CR);
	cr |= TIMER_1_CR_ENABLE;
	cr |= TIMER_1_CR_INT;
	writel(cr, base + TIMER_CR);

	return 0;
}

/* Use TIMER1 as clock event */
static struct clock_event_device fttmr010_clockevent = {
	.name			= "TIMER1",
	/* Reasonably fast and accurate clock event */
	.rating			= 300,
	.shift                  = 32,
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= fttmr010_timer_set_next_event,
	.set_state_shutdown	= fttmr010_timer_shutdown,
	.set_state_periodic	= fttmr010_timer_set_periodic,
	.set_state_oneshot	= fttmr010_timer_shutdown,
	.tick_resume		= fttmr010_timer_shutdown,
};

/*
 * IRQ handler for the timer
 */
static irqreturn_t fttmr010_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &fttmr010_clockevent;

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction fttmr010_timer_irq = {
	.name		= "Faraday FTTMR010 Timer Tick",
	.flags		= IRQF_TIMER,
	.handler	= fttmr010_timer_interrupt,
};

static int __init fttmr010_timer_common_init(struct device_node *np)
{
	int irq;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("Can't remap registers");
		return -ENXIO;
	}
	/* IRQ for timer 1 */
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("Can't parse IRQ");
		return -EINVAL;
	}

	/*
	 * Reset the interrupt mask and status
	 */
	writel(TIMER_INT_ALL_MASK, base + TIMER_INTR_MASK);
	writel(0, base + TIMER_INTR_STATE);
	writel(TIMER_DEFAULT_FLAGS, base + TIMER_CR);

	/*
	 * Setup free-running clocksource timer (interrupts
	 * disabled.)
	 */
	writel(0, base + TIMER3_COUNT);
	writel(0, base + TIMER3_LOAD);
	writel(0, base + TIMER3_MATCH1);
	writel(0, base + TIMER3_MATCH2);
	clocksource_mmio_init(base + TIMER3_COUNT,
			      "fttmr010_clocksource", tick_rate,
			      300, 32, clocksource_mmio_readl_up);
	sched_clock_register(fttmr010_read_sched_clock, 32, tick_rate);

	/*
	 * Setup clockevent timer (interrupt-driven.)
	 */
	writel(0, base + TIMER1_COUNT);
	writel(0, base + TIMER1_LOAD);
	writel(0, base + TIMER1_MATCH1);
	writel(0, base + TIMER1_MATCH2);
	setup_irq(irq, &fttmr010_timer_irq);
	fttmr010_clockevent.cpumask = cpumask_of(0);
	clockevents_config_and_register(&fttmr010_clockevent, tick_rate,
					1, 0xffffffff);

	return 0;
}

static int __init fttmr010_timer_of_init(struct device_node *np)
{
	/*
	 * These implementations require a clock reference.
	 * FIXME: we currently only support clocking using PCLK
	 * and using EXTCLK is not supported in the driver.
	 */
	struct clk *clk;

	clk = of_clk_get_by_name(np, "PCLK");
	if (IS_ERR(clk)) {
		pr_err("could not get PCLK");
		return PTR_ERR(clk);
	}
	tick_rate = clk_get_rate(clk);

	return fttmr010_timer_common_init(np);
}
CLOCKSOURCE_OF_DECLARE(fttmr010, "faraday,fttmr010", fttmr010_timer_of_init);

/*
 * Gemini-specific: relevant registers in the global syscon
 */
#define GLOBAL_STATUS		0x04
#define CPU_AHB_RATIO_MASK	(0x3 << 18)
#define CPU_AHB_1_1		(0x0 << 18)
#define CPU_AHB_3_2		(0x1 << 18)
#define CPU_AHB_24_13		(0x2 << 18)
#define CPU_AHB_2_1		(0x3 << 18)
#define REG_TO_AHB_SPEED(reg)	((((reg) >> 15) & 0x7) * 10 + 130)

static int __init gemini_timer_of_init(struct device_node *np)
{
	static struct regmap *map;
	int ret;
	u32 val;

	map = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(map)) {
		pr_err("Can't get regmap for syscon handle\n");
		return -ENODEV;
	}
	ret = regmap_read(map, GLOBAL_STATUS, &val);
	if (ret) {
		pr_err("Can't read syscon status register\n");
		return -ENXIO;
	}

	tick_rate = REG_TO_AHB_SPEED(val) * 1000000;
	pr_info("Bus: %dMHz ", tick_rate / 1000000);

	tick_rate /= 6;		/* APB bus run AHB*(1/6) */

	switch (val & CPU_AHB_RATIO_MASK) {
	case CPU_AHB_1_1:
		pr_cont("(1/1)\n");
		break;
	case CPU_AHB_3_2:
		pr_cont("(3/2)\n");
		break;
	case CPU_AHB_24_13:
		pr_cont("(24/13)\n");
		break;
	case CPU_AHB_2_1:
		pr_cont("(2/1)\n");
		break;
	}

	return fttmr010_timer_common_init(np);
}
CLOCKSOURCE_OF_DECLARE(gemini, "cortina,gemini-timer", gemini_timer_of_init);

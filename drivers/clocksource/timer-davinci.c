// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI DaVinci clocksource driver
 *
 * Copyright (C) 2019 Texas Instruments
 * Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
 * (with tiny parts adopted from code by Kevin Hilman <khilman@baylibre.com>)
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <clocksource/timer-davinci.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

#define DAVINCI_TIMER_REG_TIM12			0x10
#define DAVINCI_TIMER_REG_TIM34			0x14
#define DAVINCI_TIMER_REG_PRD12			0x18
#define DAVINCI_TIMER_REG_PRD34			0x1c
#define DAVINCI_TIMER_REG_TCR			0x20
#define DAVINCI_TIMER_REG_TGCR			0x24

#define DAVINCI_TIMER_TIMMODE_MASK		GENMASK(3, 2)
#define DAVINCI_TIMER_RESET_MASK		GENMASK(1, 0)
#define DAVINCI_TIMER_TIMMODE_32BIT_UNCHAINED	BIT(2)
#define DAVINCI_TIMER_UNRESET			GENMASK(1, 0)

#define DAVINCI_TIMER_ENAMODE_MASK		GENMASK(1, 0)
#define DAVINCI_TIMER_ENAMODE_DISABLED		0x00
#define DAVINCI_TIMER_ENAMODE_ONESHOT		BIT(0)
#define DAVINCI_TIMER_ENAMODE_PERIODIC		BIT(1)

#define DAVINCI_TIMER_ENAMODE_SHIFT_TIM12	6
#define DAVINCI_TIMER_ENAMODE_SHIFT_TIM34	22

#define DAVINCI_TIMER_MIN_DELTA			0x01
#define DAVINCI_TIMER_MAX_DELTA			0xfffffffe

#define DAVINCI_TIMER_CLKSRC_BITS		32

#define DAVINCI_TIMER_TGCR_DEFAULT \
		(DAVINCI_TIMER_TIMMODE_32BIT_UNCHAINED | DAVINCI_TIMER_UNRESET)

struct davinci_clockevent {
	struct clock_event_device dev;
	void __iomem *base;
	unsigned int cmp_off;
};

/*
 * This must be globally accessible by davinci_timer_read_sched_clock(), so
 * let's keep it here.
 */
static struct {
	struct clocksource dev;
	void __iomem *base;
	unsigned int tim_off;
} davinci_clocksource;

static struct davinci_clockevent *
to_davinci_clockevent(struct clock_event_device *clockevent)
{
	return container_of(clockevent, struct davinci_clockevent, dev);
}

static unsigned int
davinci_clockevent_read(struct davinci_clockevent *clockevent,
			unsigned int reg)
{
	return readl_relaxed(clockevent->base + reg);
}

static void davinci_clockevent_write(struct davinci_clockevent *clockevent,
				     unsigned int reg, unsigned int val)
{
	writel_relaxed(val, clockevent->base + reg);
}

static void davinci_tim12_shutdown(void __iomem *base)
{
	unsigned int tcr;

	tcr = DAVINCI_TIMER_ENAMODE_DISABLED <<
		DAVINCI_TIMER_ENAMODE_SHIFT_TIM12;
	/*
	 * This function is only ever called if we're using both timer
	 * halves. In this case TIM34 runs in periodic mode and we must
	 * not modify it.
	 */
	tcr |= DAVINCI_TIMER_ENAMODE_PERIODIC <<
		DAVINCI_TIMER_ENAMODE_SHIFT_TIM34;

	writel_relaxed(tcr, base + DAVINCI_TIMER_REG_TCR);
}

static void davinci_tim12_set_oneshot(void __iomem *base)
{
	unsigned int tcr;

	tcr = DAVINCI_TIMER_ENAMODE_ONESHOT <<
		DAVINCI_TIMER_ENAMODE_SHIFT_TIM12;
	/* Same as above. */
	tcr |= DAVINCI_TIMER_ENAMODE_PERIODIC <<
		DAVINCI_TIMER_ENAMODE_SHIFT_TIM34;

	writel_relaxed(tcr, base + DAVINCI_TIMER_REG_TCR);
}

static int davinci_clockevent_shutdown(struct clock_event_device *dev)
{
	struct davinci_clockevent *clockevent;

	clockevent = to_davinci_clockevent(dev);

	davinci_tim12_shutdown(clockevent->base);

	return 0;
}

static int davinci_clockevent_set_oneshot(struct clock_event_device *dev)
{
	struct davinci_clockevent *clockevent = to_davinci_clockevent(dev);

	davinci_clockevent_write(clockevent, DAVINCI_TIMER_REG_TIM12, 0x0);

	davinci_tim12_set_oneshot(clockevent->base);

	return 0;
}

static int
davinci_clockevent_set_next_event_std(unsigned long cycles,
				      struct clock_event_device *dev)
{
	struct davinci_clockevent *clockevent = to_davinci_clockevent(dev);

	davinci_clockevent_shutdown(dev);

	davinci_clockevent_write(clockevent, DAVINCI_TIMER_REG_TIM12, 0x0);
	davinci_clockevent_write(clockevent, DAVINCI_TIMER_REG_PRD12, cycles);

	davinci_clockevent_set_oneshot(dev);

	return 0;
}

static int
davinci_clockevent_set_next_event_cmp(unsigned long cycles,
				      struct clock_event_device *dev)
{
	struct davinci_clockevent *clockevent = to_davinci_clockevent(dev);
	unsigned int curr_time;

	curr_time = davinci_clockevent_read(clockevent,
					    DAVINCI_TIMER_REG_TIM12);
	davinci_clockevent_write(clockevent,
				 clockevent->cmp_off, curr_time + cycles);

	return 0;
}

static irqreturn_t davinci_timer_irq_timer(int irq, void *data)
{
	struct davinci_clockevent *clockevent = data;

	if (!clockevent_state_oneshot(&clockevent->dev))
		davinci_tim12_shutdown(clockevent->base);

	clockevent->dev.event_handler(&clockevent->dev);

	return IRQ_HANDLED;
}

static u64 notrace davinci_timer_read_sched_clock(void)
{
	return readl_relaxed(davinci_clocksource.base +
			     davinci_clocksource.tim_off);
}

static u64 davinci_clocksource_read(struct clocksource *dev)
{
	return davinci_timer_read_sched_clock();
}

/*
 * Standard use-case: we're using tim12 for clockevent and tim34 for
 * clocksource. The default is making the former run in oneshot mode
 * and the latter in periodic mode.
 */
static void davinci_clocksource_init_tim34(void __iomem *base)
{
	int tcr;

	tcr = DAVINCI_TIMER_ENAMODE_PERIODIC <<
		DAVINCI_TIMER_ENAMODE_SHIFT_TIM34;
	tcr |= DAVINCI_TIMER_ENAMODE_ONESHOT <<
		DAVINCI_TIMER_ENAMODE_SHIFT_TIM12;

	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TIM34);
	writel_relaxed(UINT_MAX, base + DAVINCI_TIMER_REG_PRD34);
	writel_relaxed(tcr, base + DAVINCI_TIMER_REG_TCR);
}

/*
 * Special use-case on da830: the DSP may use tim34. We're using tim12 for
 * both clocksource and clockevent. We set tim12 to periodic and don't touch
 * tim34.
 */
static void davinci_clocksource_init_tim12(void __iomem *base)
{
	unsigned int tcr;

	tcr = DAVINCI_TIMER_ENAMODE_PERIODIC <<
		DAVINCI_TIMER_ENAMODE_SHIFT_TIM12;

	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TIM12);
	writel_relaxed(UINT_MAX, base + DAVINCI_TIMER_REG_PRD12);
	writel_relaxed(tcr, base + DAVINCI_TIMER_REG_TCR);
}

static void davinci_timer_init(void __iomem *base)
{
	/* Set clock to internal mode and disable it. */
	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TCR);
	/*
	 * Reset both 32-bit timers, set no prescaler for timer 34, set the
	 * timer to dual 32-bit unchained mode, unreset both 32-bit timers.
	 */
	writel_relaxed(DAVINCI_TIMER_TGCR_DEFAULT,
		       base + DAVINCI_TIMER_REG_TGCR);
	/* Init both counters to zero. */
	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TIM12);
	writel_relaxed(0x0, base + DAVINCI_TIMER_REG_TIM34);
}

int __init davinci_timer_register(struct clk *clk,
				  const struct davinci_timer_cfg *timer_cfg)
{
	struct davinci_clockevent *clockevent;
	unsigned int tick_rate;
	void __iomem *base;
	int rv;

	rv = clk_prepare_enable(clk);
	if (rv) {
		pr_err("Unable to prepare and enable the timer clock\n");
		return rv;
	}

	if (!request_mem_region(timer_cfg->reg.start,
				resource_size(&timer_cfg->reg),
				"davinci-timer")) {
		pr_err("Unable to request memory region\n");
		rv = -EBUSY;
		goto exit_clk_disable;
	}

	base = ioremap(timer_cfg->reg.start, resource_size(&timer_cfg->reg));
	if (!base) {
		pr_err("Unable to map the register range\n");
		rv = -ENOMEM;
		goto exit_mem_region;
	}

	davinci_timer_init(base);
	tick_rate = clk_get_rate(clk);

	clockevent = kzalloc(sizeof(*clockevent), GFP_KERNEL);
	if (!clockevent) {
		rv = -ENOMEM;
		goto exit_iounmap_base;
	}

	clockevent->dev.name = "tim12";
	clockevent->dev.features = CLOCK_EVT_FEAT_ONESHOT;
	clockevent->dev.cpumask = cpumask_of(0);
	clockevent->base = base;

	if (timer_cfg->cmp_off) {
		clockevent->cmp_off = timer_cfg->cmp_off;
		clockevent->dev.set_next_event =
				davinci_clockevent_set_next_event_cmp;
	} else {
		clockevent->dev.set_next_event =
				davinci_clockevent_set_next_event_std;
		clockevent->dev.set_state_oneshot =
				davinci_clockevent_set_oneshot;
		clockevent->dev.set_state_shutdown =
				davinci_clockevent_shutdown;
	}

	rv = request_irq(timer_cfg->irq[DAVINCI_TIMER_CLOCKEVENT_IRQ].start,
			 davinci_timer_irq_timer, IRQF_TIMER,
			 "clockevent/tim12", clockevent);
	if (rv) {
		pr_err("Unable to request the clockevent interrupt\n");
		goto exit_free_clockevent;
	}

	davinci_clocksource.dev.rating = 300;
	davinci_clocksource.dev.read = davinci_clocksource_read;
	davinci_clocksource.dev.mask =
			CLOCKSOURCE_MASK(DAVINCI_TIMER_CLKSRC_BITS);
	davinci_clocksource.dev.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	davinci_clocksource.base = base;

	if (timer_cfg->cmp_off) {
		davinci_clocksource.dev.name = "tim12";
		davinci_clocksource.tim_off = DAVINCI_TIMER_REG_TIM12;
		davinci_clocksource_init_tim12(base);
	} else {
		davinci_clocksource.dev.name = "tim34";
		davinci_clocksource.tim_off = DAVINCI_TIMER_REG_TIM34;
		davinci_clocksource_init_tim34(base);
	}

	clockevents_config_and_register(&clockevent->dev, tick_rate,
					DAVINCI_TIMER_MIN_DELTA,
					DAVINCI_TIMER_MAX_DELTA);

	rv = clocksource_register_hz(&davinci_clocksource.dev, tick_rate);
	if (rv) {
		pr_err("Unable to register clocksource\n");
		goto exit_free_irq;
	}

	sched_clock_register(davinci_timer_read_sched_clock,
			     DAVINCI_TIMER_CLKSRC_BITS, tick_rate);

	return 0;

exit_free_irq:
	free_irq(timer_cfg->irq[DAVINCI_TIMER_CLOCKEVENT_IRQ].start,
			clockevent);
exit_free_clockevent:
	kfree(clockevent);
exit_iounmap_base:
	iounmap(base);
exit_mem_region:
	release_mem_region(timer_cfg->reg.start,
			   resource_size(&timer_cfg->reg));
exit_clk_disable:
	clk_disable_unprepare(clk);
	return rv;
}

static int __init of_davinci_timer_register(struct device_node *np)
{
	struct davinci_timer_cfg timer_cfg = { };
	struct clk *clk;
	int rv;

	rv = of_address_to_resource(np, 0, &timer_cfg.reg);
	if (rv) {
		pr_err("Unable to get the register range for timer\n");
		return rv;
	}

	rv = of_irq_to_resource_table(np, timer_cfg.irq,
				      DAVINCI_TIMER_NUM_IRQS);
	if (rv != DAVINCI_TIMER_NUM_IRQS) {
		pr_err("Unable to get the interrupts for timer\n");
		return rv;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("Unable to get the timer clock\n");
		return PTR_ERR(clk);
	}

	rv = davinci_timer_register(clk, &timer_cfg);
	if (rv)
		clk_put(clk);

	return rv;
}
TIMER_OF_DECLARE(davinci_timer, "ti,da830-timer", of_davinci_timer_register);

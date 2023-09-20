// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Clocksource driver for Loongson-1 SoC
 *
 * Copyright (c) 2023 Keguang Zhang <keguang.zhang@gmail.com>
 */

#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/sizes.h>
#include "timer-of.h"

/* Loongson-1 PWM Timer Register Definitions */
#define PWM_CNTR		0x0
#define PWM_HRC			0x4
#define PWM_LRC			0x8
#define PWM_CTRL		0xc

/* PWM Control Register Bits */
#define INT_LRC_EN		BIT(11)
#define INT_HRC_EN		BIT(10)
#define CNTR_RST		BIT(7)
#define INT_SR			BIT(6)
#define INT_EN			BIT(5)
#define PWM_SINGLE		BIT(4)
#define PWM_OE			BIT(3)
#define CNT_EN			BIT(0)

#define CNTR_WIDTH		24

DEFINE_RAW_SPINLOCK(ls1x_timer_lock);

struct ls1x_clocksource {
	void __iomem *reg_base;
	unsigned long ticks_per_jiffy;
	struct clocksource clksrc;
};

static inline struct ls1x_clocksource *to_ls1x_clksrc(struct clocksource *c)
{
	return container_of(c, struct ls1x_clocksource, clksrc);
}

static inline void ls1x_pwmtimer_set_period(unsigned int period,
					    struct timer_of *to)
{
	writel(period, timer_of_base(to) + PWM_LRC);
	writel(period, timer_of_base(to) + PWM_HRC);
}

static inline void ls1x_pwmtimer_clear(struct timer_of *to)
{
	writel(0, timer_of_base(to) + PWM_CNTR);
}

static inline void ls1x_pwmtimer_start(struct timer_of *to)
{
	writel((INT_EN | PWM_OE | CNT_EN), timer_of_base(to) + PWM_CTRL);
}

static inline void ls1x_pwmtimer_stop(struct timer_of *to)
{
	writel(0, timer_of_base(to) + PWM_CTRL);
}

static inline void ls1x_pwmtimer_irq_ack(struct timer_of *to)
{
	int val;

	val = readl(timer_of_base(to) + PWM_CTRL);
	val |= INT_SR;
	writel(val, timer_of_base(to) + PWM_CTRL);
}

static irqreturn_t ls1x_clockevent_isr(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = dev_id;
	struct timer_of *to = to_timer_of(clkevt);

	ls1x_pwmtimer_irq_ack(to);
	ls1x_pwmtimer_clear(to);
	ls1x_pwmtimer_start(to);

	clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

static int ls1x_clockevent_set_state_periodic(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	raw_spin_lock(&ls1x_timer_lock);
	ls1x_pwmtimer_set_period(timer_of_period(to), to);
	ls1x_pwmtimer_clear(to);
	ls1x_pwmtimer_start(to);
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static int ls1x_clockevent_tick_resume(struct clock_event_device *clkevt)
{
	raw_spin_lock(&ls1x_timer_lock);
	ls1x_pwmtimer_start(to_timer_of(clkevt));
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static int ls1x_clockevent_set_state_shutdown(struct clock_event_device *clkevt)
{
	raw_spin_lock(&ls1x_timer_lock);
	ls1x_pwmtimer_stop(to_timer_of(clkevt));
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static int ls1x_clockevent_set_next(unsigned long evt,
				    struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	raw_spin_lock(&ls1x_timer_lock);
	ls1x_pwmtimer_set_period(evt, to);
	ls1x_pwmtimer_clear(to);
	ls1x_pwmtimer_start(to);
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static struct timer_of ls1x_to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,
	.clkevt = {
		.name			= "ls1x-pwmtimer",
		.features		= CLOCK_EVT_FEAT_PERIODIC |
					  CLOCK_EVT_FEAT_ONESHOT,
		.rating			= 300,
		.set_next_event		= ls1x_clockevent_set_next,
		.set_state_periodic	= ls1x_clockevent_set_state_periodic,
		.set_state_oneshot	= ls1x_clockevent_set_state_shutdown,
		.set_state_shutdown	= ls1x_clockevent_set_state_shutdown,
		.tick_resume		= ls1x_clockevent_tick_resume,
	},
	.of_irq = {
		.handler		= ls1x_clockevent_isr,
		.flags			= IRQF_TIMER,
	},
};

/*
 * Since the PWM timer overflows every two ticks, its not very useful
 * to just read by itself. So use jiffies to emulate a free
 * running counter:
 */
static u64 ls1x_clocksource_read(struct clocksource *cs)
{
	struct ls1x_clocksource *ls1x_cs = to_ls1x_clksrc(cs);
	unsigned long flags;
	int count;
	u32 jifs;
	static int old_count;
	static u32 old_jifs;

	raw_spin_lock_irqsave(&ls1x_timer_lock, flags);
	/*
	 * Although our caller may have the read side of xtime_lock,
	 * this is now a seqlock, and we are cheating in this routine
	 * by having side effects on state that we cannot undo if
	 * there is a collision on the seqlock and our caller has to
	 * retry.  (Namely, old_jifs and old_count.)  So we must treat
	 * jiffies as volatile despite the lock.  We read jiffies
	 * before latching the timer count to guarantee that although
	 * the jiffies value might be older than the count (that is,
	 * the counter may underflow between the last point where
	 * jiffies was incremented and the point where we latch the
	 * count), it cannot be newer.
	 */
	jifs = jiffies;
	/* read the count */
	count = readl(ls1x_cs->reg_base + PWM_CNTR);

	/*
	 * It's possible for count to appear to go the wrong way for this
	 * reason:
	 *
	 *  The timer counter underflows, but we haven't handled the resulting
	 *  interrupt and incremented jiffies yet.
	 *
	 * Previous attempts to handle these cases intelligently were buggy, so
	 * we just do the simple thing now.
	 */
	if (count < old_count && jifs == old_jifs)
		count = old_count;

	old_count = count;
	old_jifs = jifs;

	raw_spin_unlock_irqrestore(&ls1x_timer_lock, flags);

	return (u64)(jifs * ls1x_cs->ticks_per_jiffy) + count;
}

static struct ls1x_clocksource ls1x_clocksource = {
	.clksrc = {
		.name           = "ls1x-pwmtimer",
		.rating		= 300,
		.read           = ls1x_clocksource_read,
		.mask           = CLOCKSOURCE_MASK(CNTR_WIDTH),
		.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
	},
};

static int __init ls1x_pwm_clocksource_init(struct device_node *np)
{
	struct timer_of *to = &ls1x_to;
	int ret;

	ret = timer_of_init(np, to);
	if (ret)
		return ret;

	clockevents_config_and_register(&to->clkevt, timer_of_rate(to),
					0x1, GENMASK(CNTR_WIDTH - 1, 0));

	ls1x_clocksource.reg_base = timer_of_base(to);
	ls1x_clocksource.ticks_per_jiffy = timer_of_period(to);

	return clocksource_register_hz(&ls1x_clocksource.clksrc,
				       timer_of_rate(to));
}

TIMER_OF_DECLARE(ls1x_pwm_clocksource, "loongson,ls1b-pwmtimer",
		 ls1x_pwm_clocksource_init);

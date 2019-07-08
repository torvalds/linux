// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/sizes.h>
#include <asm/time.h>

#include <loongson1.h>
#include <platform.h>

#ifdef CONFIG_CEVT_CSRC_LS1X

#if defined(CONFIG_TIMER_USE_PWM1)
#define LS1X_TIMER_BASE	LS1X_PWM1_BASE
#define LS1X_TIMER_IRQ	LS1X_PWM1_IRQ

#elif defined(CONFIG_TIMER_USE_PWM2)
#define LS1X_TIMER_BASE	LS1X_PWM2_BASE
#define LS1X_TIMER_IRQ	LS1X_PWM2_IRQ

#elif defined(CONFIG_TIMER_USE_PWM3)
#define LS1X_TIMER_BASE	LS1X_PWM3_BASE
#define LS1X_TIMER_IRQ	LS1X_PWM3_IRQ

#else
#define LS1X_TIMER_BASE	LS1X_PWM0_BASE
#define LS1X_TIMER_IRQ	LS1X_PWM0_IRQ
#endif

DEFINE_RAW_SPINLOCK(ls1x_timer_lock);

static void __iomem *timer_reg_base;
static uint32_t ls1x_jiffies_per_tick;

static inline void ls1x_pwmtimer_set_period(uint32_t period)
{
	__raw_writel(period, timer_reg_base + PWM_HRC);
	__raw_writel(period, timer_reg_base + PWM_LRC);
}

static inline void ls1x_pwmtimer_restart(void)
{
	__raw_writel(0x0, timer_reg_base + PWM_CNT);
	__raw_writel(INT_EN | CNT_EN, timer_reg_base + PWM_CTRL);
}

void __init ls1x_pwmtimer_init(void)
{
	timer_reg_base = ioremap_nocache(LS1X_TIMER_BASE, SZ_16);
	if (!timer_reg_base)
		panic("Failed to remap timer registers");

	ls1x_jiffies_per_tick = DIV_ROUND_CLOSEST(mips_hpt_frequency, HZ);

	ls1x_pwmtimer_set_period(ls1x_jiffies_per_tick);
	ls1x_pwmtimer_restart();
}

static u64 ls1x_clocksource_read(struct clocksource *cs)
{
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
	count = __raw_readl(timer_reg_base + PWM_CNT);

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

	return (u64) (jifs * ls1x_jiffies_per_tick) + count;
}

static struct clocksource ls1x_clocksource = {
	.name		= "ls1x-pwmtimer",
	.read		= ls1x_clocksource_read,
	.mask		= CLOCKSOURCE_MASK(24),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static irqreturn_t ls1x_clockevent_isr(int irq, void *devid)
{
	struct clock_event_device *cd = devid;

	ls1x_pwmtimer_restart();
	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static int ls1x_clockevent_set_state_periodic(struct clock_event_device *cd)
{
	raw_spin_lock(&ls1x_timer_lock);
	ls1x_pwmtimer_set_period(ls1x_jiffies_per_tick);
	ls1x_pwmtimer_restart();
	__raw_writel(INT_EN | CNT_EN, timer_reg_base + PWM_CTRL);
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static int ls1x_clockevent_tick_resume(struct clock_event_device *cd)
{
	raw_spin_lock(&ls1x_timer_lock);
	__raw_writel(INT_EN | CNT_EN, timer_reg_base + PWM_CTRL);
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static int ls1x_clockevent_set_state_shutdown(struct clock_event_device *cd)
{
	raw_spin_lock(&ls1x_timer_lock);
	__raw_writel(__raw_readl(timer_reg_base + PWM_CTRL) & ~CNT_EN,
		     timer_reg_base + PWM_CTRL);
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static int ls1x_clockevent_set_next(unsigned long evt,
				    struct clock_event_device *cd)
{
	raw_spin_lock(&ls1x_timer_lock);
	ls1x_pwmtimer_set_period(evt);
	ls1x_pwmtimer_restart();
	raw_spin_unlock(&ls1x_timer_lock);

	return 0;
}

static struct clock_event_device ls1x_clockevent = {
	.name			= "ls1x-pwmtimer",
	.features		= CLOCK_EVT_FEAT_PERIODIC,
	.rating			= 300,
	.irq			= LS1X_TIMER_IRQ,
	.set_next_event		= ls1x_clockevent_set_next,
	.set_state_shutdown	= ls1x_clockevent_set_state_shutdown,
	.set_state_periodic	= ls1x_clockevent_set_state_periodic,
	.set_state_oneshot	= ls1x_clockevent_set_state_shutdown,
	.tick_resume		= ls1x_clockevent_tick_resume,
};

static struct irqaction ls1x_pwmtimer_irqaction = {
	.name		= "ls1x-pwmtimer",
	.handler	= ls1x_clockevent_isr,
	.dev_id		= &ls1x_clockevent,
	.flags		= IRQF_PERCPU | IRQF_TIMER,
};

static void __init ls1x_time_init(void)
{
	struct clock_event_device *cd = &ls1x_clockevent;
	int ret;

	if (!mips_hpt_frequency)
		panic("Invalid timer clock rate");

	ls1x_pwmtimer_init();

	clockevent_set_clock(cd, mips_hpt_frequency);
	cd->max_delta_ns = clockevent_delta2ns(0xffffff, cd);
	cd->max_delta_ticks = 0xffffff;
	cd->min_delta_ns = clockevent_delta2ns(0x000300, cd);
	cd->min_delta_ticks = 0x000300;
	cd->cpumask = cpumask_of(smp_processor_id());
	clockevents_register_device(cd);

	ls1x_clocksource.rating = 200 + mips_hpt_frequency / 10000000;
	ret = clocksource_register_hz(&ls1x_clocksource, mips_hpt_frequency);
	if (ret)
		panic(KERN_ERR "Failed to register clocksource: %d\n", ret);

	setup_irq(LS1X_TIMER_IRQ, &ls1x_pwmtimer_irqaction);
}
#endif /* CONFIG_CEVT_CSRC_LS1X */

void __init plat_time_init(void)
{
	struct clk *clk = NULL;

	/* initialize LS1X clocks */
	ls1x_clk_init();

#ifdef CONFIG_CEVT_CSRC_LS1X
	/* setup LS1X PWM timer */
	clk = clk_get(NULL, "ls1x-pwmtimer");
	if (IS_ERR(clk))
		panic("unable to get timer clock, err=%ld", PTR_ERR(clk));

	mips_hpt_frequency = clk_get_rate(clk);
	ls1x_time_init();
#else
	/* setup mips r4k timer */
	clk = clk_get(NULL, "cpu_clk");
	if (IS_ERR(clk))
		panic("unable to get cpu clock, err=%ld", PTR_ERR(clk));

	mips_hpt_frequency = clk_get_rate(clk) / 2;
#endif /* CONFIG_CEVT_CSRC_LS1X */
}

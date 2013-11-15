/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * samsung - Common hr-timer support (s3c and s5p)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sched_clock.h>

#include <clocksource/samsung_pwm.h>


/*
 * Clocksource driver
 */

#define REG_TCFG0			0x00
#define REG_TCFG1			0x04
#define REG_TCON			0x08
#define REG_TINT_CSTAT			0x44

#define REG_TCNTB(chan)			(0x0c + 12 * (chan))
#define REG_TCMPB(chan)			(0x10 + 12 * (chan))

#define TCFG0_PRESCALER_MASK		0xff
#define TCFG0_PRESCALER1_SHIFT		8

#define TCFG1_SHIFT(x)	  		((x) * 4)
#define TCFG1_MUX_MASK	  		0xf

/*
 * Each channel occupies 4 bits in TCON register, but there is a gap of 4
 * bits (one channel) after channel 0, so channels have different numbering
 * when accessing TCON register.
 *
 * In addition, the location of autoreload bit for channel 4 (TCON channel 5)
 * in its set of bits is 2 as opposed to 3 for other channels.
 */
#define TCON_START(chan)		(1 << (4 * (chan) + 0))
#define TCON_MANUALUPDATE(chan)		(1 << (4 * (chan) + 1))
#define TCON_INVERT(chan)		(1 << (4 * (chan) + 2))
#define _TCON_AUTORELOAD(chan)		(1 << (4 * (chan) + 3))
#define _TCON_AUTORELOAD4(chan)		(1 << (4 * (chan) + 2))
#define TCON_AUTORELOAD(chan)		\
	((chan < 5) ? _TCON_AUTORELOAD(chan) : _TCON_AUTORELOAD4(chan))

DEFINE_SPINLOCK(samsung_pwm_lock);
EXPORT_SYMBOL(samsung_pwm_lock);

struct samsung_pwm_clocksource {
	void __iomem *base;
	void __iomem *source_reg;
	unsigned int irq[SAMSUNG_PWM_NUM];
	struct samsung_pwm_variant variant;

	struct clk *timerclk;

	unsigned int event_id;
	unsigned int source_id;
	unsigned int tcnt_max;
	unsigned int tscaler_div;
	unsigned int tdiv;

	unsigned long clock_count_per_tick;
};

static struct samsung_pwm_clocksource pwm;

static void samsung_timer_set_prescale(unsigned int channel, u16 prescale)
{
	unsigned long flags;
	u8 shift = 0;
	u32 reg;

	if (channel >= 2)
		shift = TCFG0_PRESCALER1_SHIFT;

	spin_lock_irqsave(&samsung_pwm_lock, flags);

	reg = readl(pwm.base + REG_TCFG0);
	reg &= ~(TCFG0_PRESCALER_MASK << shift);
	reg |= (prescale - 1) << shift;
	writel(reg, pwm.base + REG_TCFG0);

	spin_unlock_irqrestore(&samsung_pwm_lock, flags);
}

static void samsung_timer_set_divisor(unsigned int channel, u8 divisor)
{
	u8 shift = TCFG1_SHIFT(channel);
	unsigned long flags;
	u32 reg;
	u8 bits;

	bits = (fls(divisor) - 1) - pwm.variant.div_base;

	spin_lock_irqsave(&samsung_pwm_lock, flags);

	reg = readl(pwm.base + REG_TCFG1);
	reg &= ~(TCFG1_MUX_MASK << shift);
	reg |= bits << shift;
	writel(reg, pwm.base + REG_TCFG1);

	spin_unlock_irqrestore(&samsung_pwm_lock, flags);
}

static void samsung_time_stop(unsigned int channel)
{
	unsigned long tcon;
	unsigned long flags;

	if (channel > 0)
		++channel;

	spin_lock_irqsave(&samsung_pwm_lock, flags);

	tcon = __raw_readl(pwm.base + REG_TCON);
	tcon &= ~TCON_START(channel);
	__raw_writel(tcon, pwm.base + REG_TCON);

	spin_unlock_irqrestore(&samsung_pwm_lock, flags);
}

static void samsung_time_setup(unsigned int channel, unsigned long tcnt)
{
	unsigned long tcon;
	unsigned long flags;
	unsigned int tcon_chan = channel;

	if (tcon_chan > 0)
		++tcon_chan;

	spin_lock_irqsave(&samsung_pwm_lock, flags);

	tcon = __raw_readl(pwm.base + REG_TCON);

	tcon &= ~(TCON_START(tcon_chan) | TCON_AUTORELOAD(tcon_chan));
	tcon |= TCON_MANUALUPDATE(tcon_chan);

	__raw_writel(tcnt, pwm.base + REG_TCNTB(channel));
	__raw_writel(tcnt, pwm.base + REG_TCMPB(channel));
	__raw_writel(tcon, pwm.base + REG_TCON);

	spin_unlock_irqrestore(&samsung_pwm_lock, flags);
}

static void samsung_time_start(unsigned int channel, bool periodic)
{
	unsigned long tcon;
	unsigned long flags;

	if (channel > 0)
		++channel;

	spin_lock_irqsave(&samsung_pwm_lock, flags);

	tcon = __raw_readl(pwm.base + REG_TCON);

	tcon &= ~TCON_MANUALUPDATE(channel);
	tcon |= TCON_START(channel);

	if (periodic)
		tcon |= TCON_AUTORELOAD(channel);
	else
		tcon &= ~TCON_AUTORELOAD(channel);

	__raw_writel(tcon, pwm.base + REG_TCON);

	spin_unlock_irqrestore(&samsung_pwm_lock, flags);
}

static int samsung_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	/*
	 * This check is needed to account for internal rounding
	 * errors inside clockevents core, which might result in
	 * passing cycles = 0, which in turn would not generate any
	 * timer interrupt and hang the system.
	 *
	 * Another solution would be to set up the clockevent device
	 * with min_delta = 2, but this would unnecessarily increase
	 * the minimum sleep period.
	 */
	if (!cycles)
		cycles = 1;

	samsung_time_setup(pwm.event_id, cycles);
	samsung_time_start(pwm.event_id, false);

	return 0;
}

static void samsung_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	samsung_time_stop(pwm.event_id);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		samsung_time_setup(pwm.event_id, pwm.clock_count_per_tick - 1);
		samsung_time_start(pwm.event_id, true);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static void samsung_clockevent_resume(struct clock_event_device *cev)
{
	samsung_timer_set_prescale(pwm.event_id, pwm.tscaler_div);
	samsung_timer_set_divisor(pwm.event_id, pwm.tdiv);

	if (pwm.variant.has_tint_cstat) {
		u32 mask = (1 << pwm.event_id);
		writel(mask | (mask << 5), pwm.base + REG_TINT_CSTAT);
	}
}

static struct clock_event_device time_event_device = {
	.name		= "samsung_event_timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= samsung_set_next_event,
	.set_mode	= samsung_set_mode,
	.resume		= samsung_clockevent_resume,
};

static irqreturn_t samsung_clock_event_isr(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	if (pwm.variant.has_tint_cstat) {
		u32 mask = (1 << pwm.event_id);
		writel(mask | (mask << 5), pwm.base + REG_TINT_CSTAT);
	}

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction samsung_clock_event_irq = {
	.name		= "samsung_time_irq",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= samsung_clock_event_isr,
	.dev_id		= &time_event_device,
};

static void __init samsung_clockevent_init(void)
{
	unsigned long pclk;
	unsigned long clock_rate;
	unsigned int irq_number;

	pclk = clk_get_rate(pwm.timerclk);

	samsung_timer_set_prescale(pwm.event_id, pwm.tscaler_div);
	samsung_timer_set_divisor(pwm.event_id, pwm.tdiv);

	clock_rate = pclk / (pwm.tscaler_div * pwm.tdiv);
	pwm.clock_count_per_tick = clock_rate / HZ;

	time_event_device.cpumask = cpumask_of(0);
	clockevents_config_and_register(&time_event_device,
						clock_rate, 1, pwm.tcnt_max);

	irq_number = pwm.irq[pwm.event_id];
	setup_irq(irq_number, &samsung_clock_event_irq);

	if (pwm.variant.has_tint_cstat) {
		u32 mask = (1 << pwm.event_id);
		writel(mask | (mask << 5), pwm.base + REG_TINT_CSTAT);
	}
}

static void samsung_clocksource_suspend(struct clocksource *cs)
{
	samsung_time_stop(pwm.source_id);
}

static void samsung_clocksource_resume(struct clocksource *cs)
{
	samsung_timer_set_prescale(pwm.source_id, pwm.tscaler_div);
	samsung_timer_set_divisor(pwm.source_id, pwm.tdiv);

	samsung_time_setup(pwm.source_id, pwm.tcnt_max);
	samsung_time_start(pwm.source_id, true);
}

static cycle_t samsung_clocksource_read(struct clocksource *c)
{
	return ~readl_relaxed(pwm.source_reg);
}

static struct clocksource samsung_clocksource = {
	.name		= "samsung_clocksource_timer",
	.rating		= 250,
	.read		= samsung_clocksource_read,
	.suspend	= samsung_clocksource_suspend,
	.resume		= samsung_clocksource_resume,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Override the global weak sched_clock symbol with this
 * local implementation which uses the clocksource to get some
 * better resolution when scheduling the kernel. We accept that
 * this wraps around for now, since it is just a relative time
 * stamp. (Inspired by U300 implementation.)
 */
static u32 notrace samsung_read_sched_clock(void)
{
	return samsung_clocksource_read(NULL);
}

static void __init samsung_clocksource_init(void)
{
	unsigned long pclk;
	unsigned long clock_rate;
	int ret;

	pclk = clk_get_rate(pwm.timerclk);

	samsung_timer_set_prescale(pwm.source_id, pwm.tscaler_div);
	samsung_timer_set_divisor(pwm.source_id, pwm.tdiv);

	clock_rate = pclk / (pwm.tscaler_div * pwm.tdiv);

	samsung_time_setup(pwm.source_id, pwm.tcnt_max);
	samsung_time_start(pwm.source_id, true);

	if (pwm.source_id == 4)
		pwm.source_reg = pwm.base + 0x40;
	else
		pwm.source_reg = pwm.base + pwm.source_id * 0x0c + 0x14;

	setup_sched_clock(samsung_read_sched_clock,
						pwm.variant.bits, clock_rate);

	samsung_clocksource.mask = CLOCKSOURCE_MASK(pwm.variant.bits);
	ret = clocksource_register_hz(&samsung_clocksource, clock_rate);
	if (ret)
		panic("samsung_clocksource_timer: can't register clocksource\n");
}

static void __init samsung_timer_resources(void)
{
	clk_prepare_enable(pwm.timerclk);

	pwm.tcnt_max = (1UL << pwm.variant.bits) - 1;
	if (pwm.variant.bits == 16) {
		pwm.tscaler_div = 25;
		pwm.tdiv = 2;
	} else {
		pwm.tscaler_div = 2;
		pwm.tdiv = 1;
	}
}

/*
 * PWM master driver
 */
static void __init _samsung_pwm_clocksource_init(void)
{
	u8 mask;
	int channel;

	mask = ~pwm.variant.output_mask & ((1 << SAMSUNG_PWM_NUM) - 1);
	channel = fls(mask) - 1;
	if (channel < 0)
		panic("failed to find PWM channel for clocksource");
	pwm.source_id = channel;

	mask &= ~(1 << channel);
	channel = fls(mask) - 1;
	if (channel < 0)
		panic("failed to find PWM channel for clock event");
	pwm.event_id = channel;

	samsung_timer_resources();
	samsung_clockevent_init();
	samsung_clocksource_init();
}

void __init samsung_pwm_clocksource_init(void __iomem *base,
			unsigned int *irqs, struct samsung_pwm_variant *variant)
{
	pwm.base = base;
	memcpy(&pwm.variant, variant, sizeof(pwm.variant));
	memcpy(pwm.irq, irqs, SAMSUNG_PWM_NUM * sizeof(*irqs));

	pwm.timerclk = clk_get(NULL, "timers");
	if (IS_ERR(pwm.timerclk))
		panic("failed to get timers clock for timer");

	_samsung_pwm_clocksource_init();
}

#ifdef CONFIG_CLKSRC_OF
static void __init samsung_pwm_alloc(struct device_node *np,
				     const struct samsung_pwm_variant *variant)
{
	struct property *prop;
	const __be32 *cur;
	u32 val;
	int i;

	memcpy(&pwm.variant, variant, sizeof(pwm.variant));
	for (i = 0; i < SAMSUNG_PWM_NUM; ++i)
		pwm.irq[i] = irq_of_parse_and_map(np, i);

	of_property_for_each_u32(np, "samsung,pwm-outputs", prop, cur, val) {
		if (val >= SAMSUNG_PWM_NUM) {
			pr_warning("%s: invalid channel index in samsung,pwm-outputs property\n",
								__func__);
			continue;
		}
		pwm.variant.output_mask |= 1 << val;
	}

	pwm.base = of_iomap(np, 0);
	if (!pwm.base) {
		pr_err("%s: failed to map PWM registers\n", __func__);
		return;
	}

	pwm.timerclk = of_clk_get_by_name(np, "timers");
	if (IS_ERR(pwm.timerclk))
		panic("failed to get timers clock for timer");

	_samsung_pwm_clocksource_init();
}

static const struct samsung_pwm_variant s3c24xx_variant = {
	.bits		= 16,
	.div_base	= 1,
	.has_tint_cstat	= false,
	.tclk_mask	= (1 << 4),
};

static void __init s3c2410_pwm_clocksource_init(struct device_node *np)
{
	samsung_pwm_alloc(np, &s3c24xx_variant);
}
CLOCKSOURCE_OF_DECLARE(s3c2410_pwm, "samsung,s3c2410-pwm", s3c2410_pwm_clocksource_init);

static const struct samsung_pwm_variant s3c64xx_variant = {
	.bits		= 32,
	.div_base	= 0,
	.has_tint_cstat	= true,
	.tclk_mask	= (1 << 7) | (1 << 6) | (1 << 5),
};

static void __init s3c64xx_pwm_clocksource_init(struct device_node *np)
{
	samsung_pwm_alloc(np, &s3c64xx_variant);
}
CLOCKSOURCE_OF_DECLARE(s3c6400_pwm, "samsung,s3c6400-pwm", s3c64xx_pwm_clocksource_init);

static const struct samsung_pwm_variant s5p64x0_variant = {
	.bits		= 32,
	.div_base	= 0,
	.has_tint_cstat	= true,
	.tclk_mask	= 0,
};

static void __init s5p64x0_pwm_clocksource_init(struct device_node *np)
{
	samsung_pwm_alloc(np, &s5p64x0_variant);
}
CLOCKSOURCE_OF_DECLARE(s5p6440_pwm, "samsung,s5p6440-pwm", s5p64x0_pwm_clocksource_init);

static const struct samsung_pwm_variant s5p_variant = {
	.bits		= 32,
	.div_base	= 0,
	.has_tint_cstat	= true,
	.tclk_mask	= (1 << 5),
};

static void __init s5p_pwm_clocksource_init(struct device_node *np)
{
	samsung_pwm_alloc(np, &s5p_variant);
}
CLOCKSOURCE_OF_DECLARE(s5pc100_pwm, "samsung,s5pc100-pwm", s5p_pwm_clocksource_init);
#endif

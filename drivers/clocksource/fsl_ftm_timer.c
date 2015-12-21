/*
 * Freescale FlexTimer Module (FTM) timer driver.
 *
 * Copyright 2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

#define FTM_SC		0x00
#define FTM_SC_CLK_SHIFT	3
#define FTM_SC_CLK_MASK	(0x3 << FTM_SC_CLK_SHIFT)
#define FTM_SC_CLK(c)	((c) << FTM_SC_CLK_SHIFT)
#define FTM_SC_PS_MASK	0x7
#define FTM_SC_TOIE	BIT(6)
#define FTM_SC_TOF	BIT(7)

#define FTM_CNT		0x04
#define FTM_MOD		0x08
#define FTM_CNTIN	0x4C

#define FTM_PS_MAX	7

struct ftm_clock_device {
	void __iomem *clksrc_base;
	void __iomem *clkevt_base;
	unsigned long periodic_cyc;
	unsigned long ps;
	bool big_endian;
};

static struct ftm_clock_device *priv;

static inline u32 ftm_readl(void __iomem *addr)
{
	if (priv->big_endian)
		return ioread32be(addr);
	else
		return ioread32(addr);
}

static inline void ftm_writel(u32 val, void __iomem *addr)
{
	if (priv->big_endian)
		iowrite32be(val, addr);
	else
		iowrite32(val, addr);
}

static inline void ftm_counter_enable(void __iomem *base)
{
	u32 val;

	/* select and enable counter clock source */
	val = ftm_readl(base + FTM_SC);
	val &= ~(FTM_SC_PS_MASK | FTM_SC_CLK_MASK);
	val |= priv->ps | FTM_SC_CLK(1);
	ftm_writel(val, base + FTM_SC);
}

static inline void ftm_counter_disable(void __iomem *base)
{
	u32 val;

	/* disable counter clock source */
	val = ftm_readl(base + FTM_SC);
	val &= ~(FTM_SC_PS_MASK | FTM_SC_CLK_MASK);
	ftm_writel(val, base + FTM_SC);
}

static inline void ftm_irq_acknowledge(void __iomem *base)
{
	u32 val;

	val = ftm_readl(base + FTM_SC);
	val &= ~FTM_SC_TOF;
	ftm_writel(val, base + FTM_SC);
}

static inline void ftm_irq_enable(void __iomem *base)
{
	u32 val;

	val = ftm_readl(base + FTM_SC);
	val |= FTM_SC_TOIE;
	ftm_writel(val, base + FTM_SC);
}

static inline void ftm_irq_disable(void __iomem *base)
{
	u32 val;

	val = ftm_readl(base + FTM_SC);
	val &= ~FTM_SC_TOIE;
	ftm_writel(val, base + FTM_SC);
}

static inline void ftm_reset_counter(void __iomem *base)
{
	/*
	 * The CNT register contains the FTM counter value.
	 * Reset clears the CNT register. Writing any value to COUNT
	 * updates the counter with its initial value, CNTIN.
	 */
	ftm_writel(0x00, base + FTM_CNT);
}

static u64 notrace ftm_read_sched_clock(void)
{
	return ftm_readl(priv->clksrc_base + FTM_CNT);
}

static int ftm_set_next_event(unsigned long delta,
				struct clock_event_device *unused)
{
	/*
	 * The CNNIN and MOD are all double buffer registers, writing
	 * to the MOD register latches the value into a buffer. The MOD
	 * register is updated with the value of its write buffer with
	 * the following scenario:
	 * a, the counter source clock is diabled.
	 */
	ftm_counter_disable(priv->clkevt_base);

	/* Force the value of CNTIN to be loaded into the FTM counter */
	ftm_reset_counter(priv->clkevt_base);

	/*
	 * The counter increments until the value of MOD is reached,
	 * at which point the counter is reloaded with the value of CNTIN.
	 * The TOF (the overflow flag) bit is set when the FTM counter
	 * changes from MOD to CNTIN. So we should using the delta - 1.
	 */
	ftm_writel(delta - 1, priv->clkevt_base + FTM_MOD);

	ftm_counter_enable(priv->clkevt_base);

	ftm_irq_enable(priv->clkevt_base);

	return 0;
}

static int ftm_set_oneshot(struct clock_event_device *evt)
{
	ftm_counter_disable(priv->clkevt_base);
	return 0;
}

static int ftm_set_periodic(struct clock_event_device *evt)
{
	ftm_set_next_event(priv->periodic_cyc, evt);
	return 0;
}

static irqreturn_t ftm_evt_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	ftm_irq_acknowledge(priv->clkevt_base);

	if (likely(clockevent_state_oneshot(evt))) {
		ftm_irq_disable(priv->clkevt_base);
		ftm_counter_disable(priv->clkevt_base);
	}

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct clock_event_device ftm_clockevent = {
	.name			= "Freescale ftm timer",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_state_periodic	= ftm_set_periodic,
	.set_state_oneshot	= ftm_set_oneshot,
	.set_next_event		= ftm_set_next_event,
	.rating			= 300,
};

static struct irqaction ftm_timer_irq = {
	.name		= "Freescale ftm timer",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= ftm_evt_interrupt,
	.dev_id		= &ftm_clockevent,
};

static int __init ftm_clockevent_init(unsigned long freq, int irq)
{
	int err;

	ftm_writel(0x00, priv->clkevt_base + FTM_CNTIN);
	ftm_writel(~0u, priv->clkevt_base + FTM_MOD);

	ftm_reset_counter(priv->clkevt_base);

	err = setup_irq(irq, &ftm_timer_irq);
	if (err) {
		pr_err("ftm: setup irq failed: %d\n", err);
		return err;
	}

	ftm_clockevent.cpumask = cpumask_of(0);
	ftm_clockevent.irq = irq;

	clockevents_config_and_register(&ftm_clockevent,
					freq / (1 << priv->ps),
					1, 0xffff);

	ftm_counter_enable(priv->clkevt_base);

	return 0;
}

static int __init ftm_clocksource_init(unsigned long freq)
{
	int err;

	ftm_writel(0x00, priv->clksrc_base + FTM_CNTIN);
	ftm_writel(~0u, priv->clksrc_base + FTM_MOD);

	ftm_reset_counter(priv->clksrc_base);

	sched_clock_register(ftm_read_sched_clock, 16, freq / (1 << priv->ps));
	err = clocksource_mmio_init(priv->clksrc_base + FTM_CNT, "fsl-ftm",
				    freq / (1 << priv->ps), 300, 16,
				    clocksource_mmio_readl_up);
	if (err) {
		pr_err("ftm: init clock source mmio failed: %d\n", err);
		return err;
	}

	ftm_counter_enable(priv->clksrc_base);

	return 0;
}

static int __init __ftm_clk_init(struct device_node *np, char *cnt_name,
				 char *ftm_name)
{
	struct clk *clk;
	int err;

	clk = of_clk_get_by_name(np, cnt_name);
	if (IS_ERR(clk)) {
		pr_err("ftm: Cannot get \"%s\": %ld\n", cnt_name, PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	err = clk_prepare_enable(clk);
	if (err) {
		pr_err("ftm: clock failed to prepare+enable \"%s\": %d\n",
			cnt_name, err);
		return err;
	}

	clk = of_clk_get_by_name(np, ftm_name);
	if (IS_ERR(clk)) {
		pr_err("ftm: Cannot get \"%s\": %ld\n", ftm_name, PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	err = clk_prepare_enable(clk);
	if (err)
		pr_err("ftm: clock failed to prepare+enable \"%s\": %d\n",
			ftm_name, err);

	return clk_get_rate(clk);
}

static unsigned long __init ftm_clk_init(struct device_node *np)
{
	unsigned long freq;

	freq = __ftm_clk_init(np, "ftm-evt-counter-en", "ftm-evt");
	if (freq <= 0)
		return 0;

	freq = __ftm_clk_init(np, "ftm-src-counter-en", "ftm-src");
	if (freq <= 0)
		return 0;

	return freq;
}

static int __init ftm_calc_closest_round_cyc(unsigned long freq)
{
	priv->ps = 0;

	/* The counter register is only using the lower 16 bits, and
	 * if the 'freq' value is to big here, then the periodic_cyc
	 * may exceed 0xFFFF.
	 */
	do {
		priv->periodic_cyc = DIV_ROUND_CLOSEST(freq,
						HZ * (1 << priv->ps++));
	} while (priv->periodic_cyc > 0xFFFF);

	if (priv->ps > FTM_PS_MAX) {
		pr_err("ftm: the prescaler is %lu > %d\n",
				priv->ps, FTM_PS_MAX);
		return -EINVAL;
	}

	return 0;
}

static void __init ftm_timer_init(struct device_node *np)
{
	unsigned long freq;
	int irq;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return;

	priv->clkevt_base = of_iomap(np, 0);
	if (!priv->clkevt_base) {
		pr_err("ftm: unable to map event timer registers\n");
		goto err;
	}

	priv->clksrc_base = of_iomap(np, 1);
	if (!priv->clksrc_base) {
		pr_err("ftm: unable to map source timer registers\n");
		goto err;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("ftm: unable to get IRQ from DT, %d\n", irq);
		goto err;
	}

	priv->big_endian = of_property_read_bool(np, "big-endian");

	freq = ftm_clk_init(np);
	if (!freq)
		goto err;

	if (ftm_calc_closest_round_cyc(freq))
		goto err;

	if (ftm_clocksource_init(freq))
		goto err;

	if (ftm_clockevent_init(freq, irq))
		goto err;

	return;

err:
	kfree(priv);
}
CLOCKSOURCE_OF_DECLARE(flextimer, "fsl,ftm-timer", ftm_timer_init);

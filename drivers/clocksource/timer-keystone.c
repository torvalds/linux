// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keystone broadcast clock-event
 *
 * Copyright 2013 Texas Instruments, Inc.
 *
 * Author: Ivan Khoronzhuk <ivan.khoronzhuk@ti.com>
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define TIMER_NAME			"timer-keystone"

/* Timer register offsets */
#define TIM12				0x10
#define TIM34				0x14
#define PRD12				0x18
#define PRD34				0x1c
#define TCR				0x20
#define TGCR				0x24
#define INTCTLSTAT			0x44

/* Timer register bitfields */
#define TCR_ENAMODE_MASK		0xC0
#define TCR_ENAMODE_ONESHOT_MASK	0x40
#define TCR_ENAMODE_PERIODIC_MASK	0x80

#define TGCR_TIM_UNRESET_MASK		0x03
#define INTCTLSTAT_ENINT_MASK		0x01

/**
 * struct keystone_timer: holds timer's data
 * @base: timer memory base address
 * @hz_period: cycles per HZ period
 * @event_dev: event device based on timer
 */
static struct keystone_timer {
	void __iomem *base;
	unsigned long hz_period;
	struct clock_event_device event_dev;
} timer;

static inline u32 keystone_timer_readl(unsigned long rg)
{
	return readl_relaxed(timer.base + rg);
}

static inline void keystone_timer_writel(u32 val, unsigned long rg)
{
	writel_relaxed(val, timer.base + rg);
}

/**
 * keystone_timer_barrier: write memory barrier
 * use explicit barrier to avoid using readl/writel non relaxed function
 * variants, because in our case non relaxed variants hide the true places
 * where barrier is needed.
 */
static inline void keystone_timer_barrier(void)
{
	__iowmb();
}

/**
 * keystone_timer_config: configures timer to work in oneshot/periodic modes.
 * @ mask: mask of the mode to configure
 * @ period: cycles number to configure for
 */
static int keystone_timer_config(u64 period, int mask)
{
	u32 tcr;
	u32 off;

	tcr = keystone_timer_readl(TCR);
	off = tcr & ~(TCR_ENAMODE_MASK);

	/* set enable mode */
	tcr |= mask;

	/* disable timer */
	keystone_timer_writel(off, TCR);
	/* here we have to be sure the timer has been disabled */
	keystone_timer_barrier();

	/* reset counter to zero, set new period */
	keystone_timer_writel(0, TIM12);
	keystone_timer_writel(0, TIM34);
	keystone_timer_writel(period & 0xffffffff, PRD12);
	keystone_timer_writel(period >> 32, PRD34);

	/*
	 * enable timer
	 * here we have to be sure that CNTLO, CNTHI, PRDLO, PRDHI registers
	 * have been written.
	 */
	keystone_timer_barrier();
	keystone_timer_writel(tcr, TCR);
	return 0;
}

static void keystone_timer_disable(void)
{
	u32 tcr;

	tcr = keystone_timer_readl(TCR);

	/* disable timer */
	tcr &= ~(TCR_ENAMODE_MASK);
	keystone_timer_writel(tcr, TCR);
}

static irqreturn_t keystone_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static int keystone_set_next_event(unsigned long cycles,
				  struct clock_event_device *evt)
{
	return keystone_timer_config(cycles, TCR_ENAMODE_ONESHOT_MASK);
}

static int keystone_shutdown(struct clock_event_device *evt)
{
	keystone_timer_disable();
	return 0;
}

static int keystone_set_periodic(struct clock_event_device *evt)
{
	keystone_timer_config(timer.hz_period, TCR_ENAMODE_PERIODIC_MASK);
	return 0;
}

static int __init keystone_timer_init(struct device_node *np)
{
	struct clock_event_device *event_dev = &timer.event_dev;
	unsigned long rate;
	struct clk *clk;
	int irq, error;

	irq  = irq_of_parse_and_map(np, 0);
	if (!irq) {
		pr_err("%s: failed to map interrupts\n", __func__);
		return -EINVAL;
	}

	timer.base = of_iomap(np, 0);
	if (!timer.base) {
		pr_err("%s: failed to map registers\n", __func__);
		return -ENXIO;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to get clock\n", __func__);
		iounmap(timer.base);
		return PTR_ERR(clk);
	}

	error = clk_prepare_enable(clk);
	if (error) {
		pr_err("%s: failed to enable clock\n", __func__);
		goto err;
	}

	rate = clk_get_rate(clk);

	/* disable, use internal clock source */
	keystone_timer_writel(0, TCR);
	/* here we have to be sure the timer has been disabled */
	keystone_timer_barrier();

	/* reset timer as 64-bit, no pre-scaler, plus features are disabled */
	keystone_timer_writel(0, TGCR);

	/* unreset timer */
	keystone_timer_writel(TGCR_TIM_UNRESET_MASK, TGCR);

	/* init counter to zero */
	keystone_timer_writel(0, TIM12);
	keystone_timer_writel(0, TIM34);

	timer.hz_period = DIV_ROUND_UP(rate, HZ);

	/* enable timer interrupts */
	keystone_timer_writel(INTCTLSTAT_ENINT_MASK, INTCTLSTAT);

	error = request_irq(irq, keystone_timer_interrupt, IRQF_TIMER,
			    TIMER_NAME, event_dev);
	if (error) {
		pr_err("%s: failed to setup irq\n", __func__);
		goto err;
	}

	/* setup clockevent */
	event_dev->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	event_dev->set_next_event = keystone_set_next_event;
	event_dev->set_state_shutdown = keystone_shutdown;
	event_dev->set_state_periodic = keystone_set_periodic;
	event_dev->set_state_oneshot = keystone_shutdown;
	event_dev->cpumask = cpu_possible_mask;
	event_dev->owner = THIS_MODULE;
	event_dev->name = TIMER_NAME;
	event_dev->irq = irq;

	clockevents_config_and_register(event_dev, rate, 1, ULONG_MAX);

	pr_info("keystone timer clock @%lu Hz\n", rate);
	return 0;
err:
	clk_put(clk);
	iounmap(timer.base);
	return error;
}

TIMER_OF_DECLARE(keystone_timer, "ti,keystone-timer",
			   keystone_timer_init);

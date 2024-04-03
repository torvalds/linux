// SPDX-License-Identifier: GPL-2.0
/*
 * Cirrus Logic EP93xx timer driver.
 * Copyright (C) 2021 Nikita Shubin <nikita.shubin@maquefel.me>
 *
 * Based on a rewrite of arch/arm/mach-ep93xx/timer.c:
 */

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <asm/mach/time.h>

/*************************************************************************
 * Timer handling for EP93xx
 *************************************************************************
 * The ep93xx has four internal timers.  Timers 1, 2 (both 16 bit) and
 * 3 (32 bit) count down at 508 kHz, are self-reloading, and can generate
 * an interrupt on underflow.  Timer 4 (40 bit) counts down at 983.04 kHz,
 * is free-running, and can't generate interrupts.
 *
 * The 508 kHz timers are ideal for use for the timer interrupt, as the
 * most common values of HZ divide 508 kHz nicely.  We pick the 32 bit
 * timer (timer 3) to get as long sleep intervals as possible when using
 * CONFIG_NO_HZ.
 *
 * The higher clock rate of timer 4 makes it a better choice than the
 * other timers for use as clock source and for sched_clock(), providing
 * a stable 40 bit time base.
 *************************************************************************
 */

#define EP93XX_TIMER1_LOAD		0x00
#define EP93XX_TIMER1_VALUE		0x04
#define EP93XX_TIMER1_CONTROL		0x08
#define EP93XX_TIMER123_CONTROL_ENABLE	BIT(7)
#define EP93XX_TIMER123_CONTROL_MODE	BIT(6)
#define EP93XX_TIMER123_CONTROL_CLKSEL	BIT(3)
#define EP93XX_TIMER1_CLEAR		0x0c
#define EP93XX_TIMER2_LOAD		0x20
#define EP93XX_TIMER2_VALUE		0x24
#define EP93XX_TIMER2_CONTROL		0x28
#define EP93XX_TIMER2_CLEAR		0x2c
/*
 * This read-only register contains the low word of the time stamp debug timer
 * ( Timer4). When this register is read, the high byte of the Timer4 counter is
 * saved in the Timer4ValueHigh register.
 */
#define EP93XX_TIMER4_VALUE_LOW		0x60
#define EP93XX_TIMER4_VALUE_HIGH	0x64
#define EP93XX_TIMER4_VALUE_HIGH_ENABLE	BIT(8)
#define EP93XX_TIMER3_LOAD		0x80
#define EP93XX_TIMER3_VALUE		0x84
#define EP93XX_TIMER3_CONTROL		0x88
#define EP93XX_TIMER3_CLEAR		0x8c

#define EP93XX_TIMER123_RATE		508469
#define EP93XX_TIMER4_RATE		983040

struct ep93xx_tcu {
	void __iomem *base;
};

static struct ep93xx_tcu *ep93xx_tcu;

static u64 ep93xx_clocksource_read(struct clocksource *c)
{
	struct ep93xx_tcu *tcu = ep93xx_tcu;

	return lo_hi_readq(tcu->base + EP93XX_TIMER4_VALUE_LOW) & GENMASK_ULL(39, 0);
}

static u64 notrace ep93xx_read_sched_clock(void)
{
	return ep93xx_clocksource_read(NULL);
}

static int ep93xx_clkevt_set_next_event(unsigned long next,
					struct clock_event_device *evt)
{
	struct ep93xx_tcu *tcu = ep93xx_tcu;
	/* Default mode: periodic, off, 508 kHz */
	u32 tmode = EP93XX_TIMER123_CONTROL_MODE |
	EP93XX_TIMER123_CONTROL_CLKSEL;

	/* Clear timer */
	writel(tmode, tcu->base + EP93XX_TIMER3_CONTROL);

	/* Set next event */
	writel(next, tcu->base + EP93XX_TIMER3_LOAD);
	writel(tmode | EP93XX_TIMER123_CONTROL_ENABLE,
	       tcu->base + EP93XX_TIMER3_CONTROL);
	return 0;
}

static int ep93xx_clkevt_shutdown(struct clock_event_device *evt)
{
	struct ep93xx_tcu *tcu = ep93xx_tcu;
	/* Disable timer */
	writel(0, tcu->base + EP93XX_TIMER3_CONTROL);

	return 0;
}

static struct clock_event_device ep93xx_clockevent = {
	.name			= "timer1",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= ep93xx_clkevt_shutdown,
	.set_state_oneshot	= ep93xx_clkevt_shutdown,
	.tick_resume		= ep93xx_clkevt_shutdown,
	.set_next_event		= ep93xx_clkevt_set_next_event,
	.rating			= 300,
};

static irqreturn_t ep93xx_timer_interrupt(int irq, void *dev_id)
{
	struct ep93xx_tcu *tcu = ep93xx_tcu;
	struct clock_event_device *evt = dev_id;

	/* Writing any value clears the timer interrupt */
	writel(1, tcu->base + EP93XX_TIMER3_CLEAR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int __init ep93xx_timer_of_init(struct device_node *np)
{
	int irq;
	unsigned long flags = IRQF_TIMER | IRQF_IRQPOLL;
	struct ep93xx_tcu *tcu;
	int ret;

	tcu = kzalloc(sizeof(*tcu), GFP_KERNEL);
	if (!tcu)
		return -ENOMEM;

	tcu->base = of_iomap(np, 0);
	if (!tcu->base) {
		pr_err("Can't remap registers\n");
		ret = -ENXIO;
		goto out_free;
	}

	ep93xx_tcu = tcu;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		ret = -EINVAL;
		pr_err("EP93XX Timer Can't parse IRQ %d", irq);
		goto out_free;
	}

	/* Enable and register clocksource and sched_clock on timer 4 */
	writel(EP93XX_TIMER4_VALUE_HIGH_ENABLE,
	       tcu->base + EP93XX_TIMER4_VALUE_HIGH);
	clocksource_mmio_init(NULL, "timer4",
				EP93XX_TIMER4_RATE, 200, 40,
				ep93xx_clocksource_read);
	sched_clock_register(ep93xx_read_sched_clock, 40,
			     EP93XX_TIMER4_RATE);

	/* Set up clockevent on timer 3 */
	if (request_irq(irq, ep93xx_timer_interrupt, flags, "ep93xx timer",
		&ep93xx_clockevent))
		pr_err("Failed to request irq %d (ep93xx timer)\n", irq);

	clockevents_config_and_register(&ep93xx_clockevent,
				EP93XX_TIMER123_RATE,
				1,
				UINT_MAX);

	return 0;

out_free:
	kfree(tcu);
	return ret;
}
TIMER_OF_DECLARE(ep93xx_timer, "cirrus,ep9301-timer", ep93xx_timer_of_init);

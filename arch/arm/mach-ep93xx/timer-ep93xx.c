// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/sched_clock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/mach/time.h>
#include "soc.h"
#include "platform.h"

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
#define EP93XX_TIMER_REG(x)		(EP93XX_TIMER_BASE + (x))
#define EP93XX_TIMER1_LOAD		EP93XX_TIMER_REG(0x00)
#define EP93XX_TIMER1_VALUE		EP93XX_TIMER_REG(0x04)
#define EP93XX_TIMER1_CONTROL		EP93XX_TIMER_REG(0x08)
#define EP93XX_TIMER123_CONTROL_ENABLE	(1 << 7)
#define EP93XX_TIMER123_CONTROL_MODE	(1 << 6)
#define EP93XX_TIMER123_CONTROL_CLKSEL	(1 << 3)
#define EP93XX_TIMER1_CLEAR		EP93XX_TIMER_REG(0x0c)
#define EP93XX_TIMER2_LOAD		EP93XX_TIMER_REG(0x20)
#define EP93XX_TIMER2_VALUE		EP93XX_TIMER_REG(0x24)
#define EP93XX_TIMER2_CONTROL		EP93XX_TIMER_REG(0x28)
#define EP93XX_TIMER2_CLEAR		EP93XX_TIMER_REG(0x2c)
#define EP93XX_TIMER4_VALUE_LOW		EP93XX_TIMER_REG(0x60)
#define EP93XX_TIMER4_VALUE_HIGH	EP93XX_TIMER_REG(0x64)
#define EP93XX_TIMER4_VALUE_HIGH_ENABLE	(1 << 8)
#define EP93XX_TIMER3_LOAD		EP93XX_TIMER_REG(0x80)
#define EP93XX_TIMER3_VALUE		EP93XX_TIMER_REG(0x84)
#define EP93XX_TIMER3_CONTROL		EP93XX_TIMER_REG(0x88)
#define EP93XX_TIMER3_CLEAR		EP93XX_TIMER_REG(0x8c)

#define EP93XX_TIMER123_RATE		508469
#define EP93XX_TIMER4_RATE		983040

static u64 notrace ep93xx_read_sched_clock(void)
{
	u64 ret;

	ret = readl(EP93XX_TIMER4_VALUE_LOW);
	ret |= ((u64) (readl(EP93XX_TIMER4_VALUE_HIGH) & 0xff) << 32);
	return ret;
}

static u64 ep93xx_clocksource_read(struct clocksource *c)
{
	u64 ret;

	ret = readl(EP93XX_TIMER4_VALUE_LOW);
	ret |= ((u64) (readl(EP93XX_TIMER4_VALUE_HIGH) & 0xff) << 32);
	return (u64) ret;
}

static int ep93xx_clkevt_set_next_event(unsigned long next,
					struct clock_event_device *evt)
{
	/* Default mode: periodic, off, 508 kHz */
	u32 tmode = EP93XX_TIMER123_CONTROL_MODE |
		    EP93XX_TIMER123_CONTROL_CLKSEL;

	/* Clear timer */
	writel(tmode, EP93XX_TIMER3_CONTROL);

	/* Set next event */
	writel(next, EP93XX_TIMER3_LOAD);
	writel(tmode | EP93XX_TIMER123_CONTROL_ENABLE,
	       EP93XX_TIMER3_CONTROL);
        return 0;
}


static int ep93xx_clkevt_shutdown(struct clock_event_device *evt)
{
	/* Disable timer */
	writel(0, EP93XX_TIMER3_CONTROL);

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
	struct clock_event_device *evt = dev_id;

	/* Writing any value clears the timer interrupt */
	writel(1, EP93XX_TIMER3_CLEAR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

void __init ep93xx_timer_init(void)
{
	int irq = IRQ_EP93XX_TIMER3;
	unsigned long flags = IRQF_TIMER | IRQF_IRQPOLL;

	/* Enable and register clocksource and sched_clock on timer 4 */
	writel(EP93XX_TIMER4_VALUE_HIGH_ENABLE,
	       EP93XX_TIMER4_VALUE_HIGH);
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
					0xffffffffU);
}

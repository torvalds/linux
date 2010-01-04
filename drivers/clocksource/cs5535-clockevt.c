/*
 * Clock event driver for the CS5535/CS5536
 *
 * Copyright (C) 2006, Advanced Micro Devices, Inc.
 * Copyright (C) 2007  Andres Salomon <dilinger@debian.org>
 * Copyright (C) 2009  Andres Salomon <dilinger@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * The MFGPTs are documented in AMD Geode CS5536 Companion Device Data Book.
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/cs5535.h>
#include <linux/clockchips.h>

#define DRV_NAME "cs5535-clockevt"

static int timer_irq = CONFIG_CS5535_MFGPT_DEFAULT_IRQ;
module_param_named(irq, timer_irq, int, 0644);
MODULE_PARM_DESC(irq, "Which IRQ to use for the clock source MFGPT ticks.");

/*
 * We are using the 32.768kHz input clock - it's the only one that has the
 * ranges we find desirable.  The following table lists the suitable
 * divisors and the associated Hz, minimum interval and the maximum interval:
 *
 *  Divisor   Hz      Min Delta (s)  Max Delta (s)
 *   1        32768   .00048828125      2.000
 *   2        16384   .0009765625       4.000
 *   4         8192   .001953125        8.000
 *   8         4096   .00390625        16.000
 *   16        2048   .0078125         32.000
 *   32        1024   .015625          64.000
 *   64         512   .03125          128.000
 *  128         256   .0625           256.000
 *  256         128   .125            512.000
 */

static unsigned int cs5535_tick_mode = CLOCK_EVT_MODE_SHUTDOWN;
static struct cs5535_mfgpt_timer *cs5535_event_clock;

/* Selected from the table above */

#define MFGPT_DIVISOR 16
#define MFGPT_SCALE  4     /* divisor = 2^(scale) */
#define MFGPT_HZ  (32768 / MFGPT_DIVISOR)
#define MFGPT_PERIODIC (MFGPT_HZ / HZ)

/*
 * The MFPGT timers on the CS5536 provide us with suitable timers to use
 * as clock event sources - not as good as a HPET or APIC, but certainly
 * better than the PIT.  This isn't a general purpose MFGPT driver, but
 * a simplified one designed specifically to act as a clock event source.
 * For full details about the MFGPT, please consult the CS5536 data sheet.
 */

static void disable_timer(struct cs5535_mfgpt_timer *timer)
{
	/* avoid races by clearing CMP1 and CMP2 unconditionally */
	cs5535_mfgpt_write(timer, MFGPT_REG_SETUP,
			(uint16_t) ~MFGPT_SETUP_CNTEN | MFGPT_SETUP_CMP1 |
				MFGPT_SETUP_CMP2);
}

static void start_timer(struct cs5535_mfgpt_timer *timer, uint16_t delta)
{
	cs5535_mfgpt_write(timer, MFGPT_REG_CMP2, delta);
	cs5535_mfgpt_write(timer, MFGPT_REG_COUNTER, 0);

	cs5535_mfgpt_write(timer, MFGPT_REG_SETUP,
			MFGPT_SETUP_CNTEN | MFGPT_SETUP_CMP2);
}

static void mfgpt_set_mode(enum clock_event_mode mode,
		struct clock_event_device *evt)
{
	disable_timer(cs5535_event_clock);

	if (mode == CLOCK_EVT_MODE_PERIODIC)
		start_timer(cs5535_event_clock, MFGPT_PERIODIC);

	cs5535_tick_mode = mode;
}

static int mfgpt_next_event(unsigned long delta, struct clock_event_device *evt)
{
	start_timer(cs5535_event_clock, delta);
	return 0;
}

static struct clock_event_device cs5535_clockevent = {
	.name = DRV_NAME,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = mfgpt_set_mode,
	.set_next_event = mfgpt_next_event,
	.rating = 250,
	.cpumask = cpu_all_mask,
	.shift = 32
};

static irqreturn_t mfgpt_tick(int irq, void *dev_id)
{
	uint16_t val = cs5535_mfgpt_read(cs5535_event_clock, MFGPT_REG_SETUP);

	/* See if the interrupt was for us */
	if (!(val & (MFGPT_SETUP_SETUP | MFGPT_SETUP_CMP2 | MFGPT_SETUP_CMP1)))
		return IRQ_NONE;

	/* Turn off the clock (and clear the event) */
	disable_timer(cs5535_event_clock);

	if (cs5535_tick_mode == CLOCK_EVT_MODE_SHUTDOWN)
		return IRQ_HANDLED;

	/* Clear the counter */
	cs5535_mfgpt_write(cs5535_event_clock, MFGPT_REG_COUNTER, 0);

	/* Restart the clock in periodic mode */

	if (cs5535_tick_mode == CLOCK_EVT_MODE_PERIODIC)
		cs5535_mfgpt_write(cs5535_event_clock, MFGPT_REG_SETUP,
				MFGPT_SETUP_CNTEN | MFGPT_SETUP_CMP2);

	cs5535_clockevent.event_handler(&cs5535_clockevent);
	return IRQ_HANDLED;
}

static struct irqaction mfgptirq  = {
	.handler = mfgpt_tick,
	.flags = IRQF_DISABLED | IRQF_NOBALANCING | IRQF_TIMER,
	.name = DRV_NAME,
};

static int __init cs5535_mfgpt_init(void)
{
	struct cs5535_mfgpt_timer *timer;
	int ret;
	uint16_t val;

	timer = cs5535_mfgpt_alloc_timer(MFGPT_TIMER_ANY, MFGPT_DOMAIN_WORKING);
	if (!timer) {
		printk(KERN_ERR DRV_NAME ": Could not allocate MFPGT timer\n");
		return -ENODEV;
	}
	cs5535_event_clock = timer;

	/* Set up the IRQ on the MFGPT side */
	if (cs5535_mfgpt_setup_irq(timer, MFGPT_CMP2, &timer_irq)) {
		printk(KERN_ERR DRV_NAME ": Could not set up IRQ %d\n",
				timer_irq);
		return -EIO;
	}

	/* And register it with the kernel */
	ret = setup_irq(timer_irq, &mfgptirq);
	if (ret) {
		printk(KERN_ERR DRV_NAME ": Unable to set up the interrupt.\n");
		goto err;
	}

	/* Set the clock scale and enable the event mode for CMP2 */
	val = MFGPT_SCALE | (3 << 8);

	cs5535_mfgpt_write(cs5535_event_clock, MFGPT_REG_SETUP, val);

	/* Set up the clock event */
	cs5535_clockevent.mult = div_sc(MFGPT_HZ, NSEC_PER_SEC,
			cs5535_clockevent.shift);
	cs5535_clockevent.min_delta_ns = clockevent_delta2ns(0xF,
			&cs5535_clockevent);
	cs5535_clockevent.max_delta_ns = clockevent_delta2ns(0xFFFE,
			&cs5535_clockevent);

	printk(KERN_INFO DRV_NAME
		": Registering MFGPT timer as a clock event, using IRQ %d\n",
		timer_irq);
	clockevents_register_device(&cs5535_clockevent);

	return 0;

err:
	cs5535_mfgpt_release_irq(cs5535_event_clock, MFGPT_CMP2, &timer_irq);
	printk(KERN_ERR DRV_NAME ": Unable to set up the MFGPT clock source\n");
	return -EIO;
}

module_init(cs5535_mfgpt_init);

MODULE_AUTHOR("Andres Salomon <dilinger@collabora.co.uk>");
MODULE_DESCRIPTION("CS5535/CS5536 MFGPT clock event driver");
MODULE_LICENSE("GPL");

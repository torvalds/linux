/*
 * Driver/API for AMD Geode Multi-Function General Purpose Timers (MFGPT)
 *
 * Copyright (C) 2006, Advanced Micro Devices, Inc.
 * Copyright (C) 2007, Andres Salomon <dilinger@debian.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * The MFGPTs are documented in AMD Geode CS5536 Companion Device Data Book.
 */

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

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/geode.h>

static struct mfgpt_timer_t {
	unsigned int avail:1;
} mfgpt_timers[MFGPT_MAX_TIMERS];

/* Selected from the table above */

#define MFGPT_DIVISOR 16
#define MFGPT_SCALE  4     /* divisor = 2^(scale) */
#define MFGPT_HZ  (32768 / MFGPT_DIVISOR)
#define MFGPT_PERIODIC (MFGPT_HZ / HZ)

/* Allow for disabling of MFGPTs */
static int disable;
static int __init mfgpt_disable(char *s)
{
	disable = 1;
	return 1;
}
__setup("nomfgpt", mfgpt_disable);

/* Reset the MFGPT timers. This is required by some broken BIOSes which already
 * do the same and leave the system in an unstable state. TinyBIOS 0.98 is
 * affected at least (0.99 is OK with MFGPT workaround left to off).
 */
static int __init mfgpt_fix(char *s)
{
	u32 val, dummy;

	/* The following udocumented bit resets the MFGPT timers */
	val = 0xFF; dummy = 0;
	wrmsr(0x5140002B, val, dummy);
	return 1;
}
__setup("mfgptfix", mfgpt_fix);

/*
 * Check whether any MFGPTs are available for the kernel to use.  In most
 * cases, firmware that uses AMD's VSA code will claim all timers during
 * bootup; we certainly don't want to take them if they're already in use.
 * In other cases (such as with VSAless OpenFirmware), the system firmware
 * leaves timers available for us to use.
 */


static int timers = -1;

static void geode_mfgpt_detect(void)
{
	int i;
	u16 val;

	timers = 0;

	if (disable) {
		printk(KERN_INFO "geode-mfgpt:  MFGPT support is disabled\n");
		goto done;
	}

	if (!geode_get_dev_base(GEODE_DEV_MFGPT)) {
		printk(KERN_INFO "geode-mfgpt:  MFGPT LBAR is not set up\n");
		goto done;
	}

	for (i = 0; i < MFGPT_MAX_TIMERS; i++) {
		val = geode_mfgpt_read(i, MFGPT_REG_SETUP);
		if (!(val & MFGPT_SETUP_SETUP)) {
			mfgpt_timers[i].avail = 1;
			timers++;
		}
	}

done:
	printk(KERN_INFO "geode-mfgpt:  %d MFGPT timers available.\n", timers);
}

int geode_mfgpt_toggle_event(int timer, int cmp, int event, int enable)
{
	u32 msr, mask, value, dummy;
	int shift = (cmp == MFGPT_CMP1) ? 0 : 8;

	if (timer < 0 || timer >= MFGPT_MAX_TIMERS)
		return -EIO;

	/*
	 * The register maps for these are described in sections 6.17.1.x of
	 * the AMD Geode CS5536 Companion Device Data Book.
	 */
	switch (event) {
	case MFGPT_EVENT_RESET:
		/*
		 * XXX: According to the docs, we cannot reset timers above
		 * 6; that is, resets for 7 and 8 will be ignored.  Is this
		 * a problem?   -dilinger
		 */
		msr = MFGPT_NR_MSR;
		mask = 1 << (timer + 24);
		break;

	case MFGPT_EVENT_NMI:
		msr = MFGPT_NR_MSR;
		mask = 1 << (timer + shift);
		break;

	case MFGPT_EVENT_IRQ:
		msr = MFGPT_IRQ_MSR;
		mask = 1 << (timer + shift);
		break;

	default:
		return -EIO;
	}

	rdmsr(msr, value, dummy);

	if (enable)
		value |= mask;
	else
		value &= ~mask;

	wrmsr(msr, value, dummy);
	return 0;
}

int geode_mfgpt_set_irq(int timer, int cmp, int irq, int enable)
{
	u32 val, dummy;
	int offset;

	if (timer < 0 || timer >= MFGPT_MAX_TIMERS)
		return -EIO;

	if (geode_mfgpt_toggle_event(timer, cmp, MFGPT_EVENT_IRQ, enable))
		return -EIO;

	rdmsr(MSR_PIC_ZSEL_LOW, val, dummy);

	offset = (timer % 4) * 4;

	val &= ~((0xF << offset) | (0xF << (offset + 16)));

	if (enable) {
		val |= (irq & 0x0F) << (offset);
		val |= (irq & 0x0F) << (offset + 16);
	}

	wrmsr(MSR_PIC_ZSEL_LOW, val, dummy);
	return 0;
}

static int mfgpt_get(int timer)
{
	mfgpt_timers[timer].avail = 0;
	printk(KERN_INFO "geode-mfgpt:  Registered timer %d\n", timer);
	return timer;
}

int geode_mfgpt_alloc_timer(int timer, int domain)
{
	int i;

	if (timers == -1) {
		/* timers haven't been detected yet */
		geode_mfgpt_detect();
	}

	if (!timers)
		return -1;

	if (timer >= MFGPT_MAX_TIMERS)
		return -1;

	if (timer < 0) {
		/* Try to find an available timer */
		for (i = 0; i < MFGPT_MAX_TIMERS; i++) {
			if (mfgpt_timers[i].avail)
				return mfgpt_get(i);

			if (i == 5 && domain == MFGPT_DOMAIN_WORKING)
				break;
		}
	} else {
		/* If they requested a specific timer, try to honor that */
		if (mfgpt_timers[timer].avail)
			return mfgpt_get(timer);
	}

	/* No timers available - too bad */
	return -1;
}


#ifdef CONFIG_GEODE_MFGPT_TIMER

/*
 * The MFPGT timers on the CS5536 provide us with suitable timers to use
 * as clock event sources - not as good as a HPET or APIC, but certainly
 * better then the PIT.  This isn't a general purpose MFGPT driver, but
 * a simplified one designed specifically to act as a clock event source.
 * For full details about the MFGPT, please consult the CS5536 data sheet.
 */

#include <linux/clocksource.h>
#include <linux/clockchips.h>

static unsigned int mfgpt_tick_mode = CLOCK_EVT_MODE_SHUTDOWN;
static u16 mfgpt_event_clock;

static int irq = 7;
static int __init mfgpt_setup(char *str)
{
	get_option(&str, &irq);
	return 1;
}
__setup("mfgpt_irq=", mfgpt_setup);

static void mfgpt_disable_timer(u16 clock)
{
	/* avoid races by clearing CMP1 and CMP2 unconditionally */
	geode_mfgpt_write(clock, MFGPT_REG_SETUP, (u16) ~MFGPT_SETUP_CNTEN |
			MFGPT_SETUP_CMP1 | MFGPT_SETUP_CMP2);
}

static int mfgpt_next_event(unsigned long, struct clock_event_device *);
static void mfgpt_set_mode(enum clock_event_mode, struct clock_event_device *);

static struct clock_event_device mfgpt_clockevent = {
	.name = "mfgpt-timer",
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = mfgpt_set_mode,
	.set_next_event = mfgpt_next_event,
	.rating = 250,
	.cpumask = CPU_MASK_ALL,
	.shift = 32
};

static void mfgpt_start_timer(u16 delta)
{
	geode_mfgpt_write(mfgpt_event_clock, MFGPT_REG_CMP2, (u16) delta);
	geode_mfgpt_write(mfgpt_event_clock, MFGPT_REG_COUNTER, 0);

	geode_mfgpt_write(mfgpt_event_clock, MFGPT_REG_SETUP,
			  MFGPT_SETUP_CNTEN | MFGPT_SETUP_CMP2);
}

static void mfgpt_set_mode(enum clock_event_mode mode,
			   struct clock_event_device *evt)
{
	mfgpt_disable_timer(mfgpt_event_clock);

	if (mode == CLOCK_EVT_MODE_PERIODIC)
		mfgpt_start_timer(MFGPT_PERIODIC);

	mfgpt_tick_mode = mode;
}

static int mfgpt_next_event(unsigned long delta, struct clock_event_device *evt)
{
	mfgpt_start_timer(delta);
	return 0;
}

static irqreturn_t mfgpt_tick(int irq, void *dev_id)
{
	u16 val = geode_mfgpt_read(mfgpt_event_clock, MFGPT_REG_SETUP);

	/* See if the interrupt was for us */
	if (!(val & (MFGPT_SETUP_SETUP  | MFGPT_SETUP_CMP2 | MFGPT_SETUP_CMP1)))
		return IRQ_NONE;

	/* Turn off the clock (and clear the event) */
	mfgpt_disable_timer(mfgpt_event_clock);

	if (mfgpt_tick_mode == CLOCK_EVT_MODE_SHUTDOWN)
		return IRQ_HANDLED;

	/* Clear the counter */
	geode_mfgpt_write(mfgpt_event_clock, MFGPT_REG_COUNTER, 0);

	/* Restart the clock in periodic mode */

	if (mfgpt_tick_mode == CLOCK_EVT_MODE_PERIODIC) {
		geode_mfgpt_write(mfgpt_event_clock, MFGPT_REG_SETUP,
				  MFGPT_SETUP_CNTEN | MFGPT_SETUP_CMP2);
	}

	mfgpt_clockevent.event_handler(&mfgpt_clockevent);
	return IRQ_HANDLED;
}

static struct irqaction mfgptirq  = {
	.handler = mfgpt_tick,
	.flags = IRQF_DISABLED | IRQF_NOBALANCING,
	.mask = CPU_MASK_NONE,
	.name = "mfgpt-timer"
};

int __init mfgpt_timer_setup(void)
{
	int timer, ret;
	u16 val;

	timer = geode_mfgpt_alloc_timer(MFGPT_TIMER_ANY, MFGPT_DOMAIN_WORKING);
	if (timer < 0) {
		printk(KERN_ERR
		       "mfgpt-timer:  Could not allocate a MFPGT timer\n");
		return -ENODEV;
	}

	mfgpt_event_clock = timer;

	/* Set up the IRQ on the MFGPT side */
	if (geode_mfgpt_setup_irq(mfgpt_event_clock, MFGPT_CMP2, irq)) {
		printk(KERN_ERR "mfgpt-timer:  Could not set up IRQ %d\n", irq);
		return -EIO;
	}

	/* And register it with the kernel */
	ret = setup_irq(irq, &mfgptirq);

	if (ret) {
		printk(KERN_ERR
		       "mfgpt-timer:  Unable to set up the interrupt.\n");
		goto err;
	}

	/* Set the clock scale and enable the event mode for CMP2 */
	val = MFGPT_SCALE | (3 << 8);

	geode_mfgpt_write(mfgpt_event_clock, MFGPT_REG_SETUP, val);

	/* Set up the clock event */
	mfgpt_clockevent.mult = div_sc(MFGPT_HZ, NSEC_PER_SEC, 32);
	mfgpt_clockevent.min_delta_ns = clockevent_delta2ns(0xF,
			&mfgpt_clockevent);
	mfgpt_clockevent.max_delta_ns = clockevent_delta2ns(0xFFFE,
			&mfgpt_clockevent);

	printk(KERN_INFO
	       "mfgpt-timer:  registering the MFGPT timer as a clock event.\n");
	clockevents_register_device(&mfgpt_clockevent);

	return 0;

err:
	geode_mfgpt_release_irq(mfgpt_event_clock, MFGPT_CMP2, irq);
	printk(KERN_ERR
	       "mfgpt-timer:  Unable to set up the MFGPT clock source\n");
	return -EIO;
}

#endif

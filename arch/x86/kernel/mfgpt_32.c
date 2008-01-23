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
 * We are using the 32Khz input clock - its the only one that has the
 * ranges we find desirable.  The following table lists the suitable
 * divisors and the associated hz, minimum interval
 * and the maximum interval:
 *
 *  Divisor   Hz      Min Delta (S) Max Delta (S)
 *   1        32000     .0005          2.048
 *   2        16000      .001          4.096
 *   4         8000      .002          8.192
 *   8         4000      .004         16.384
 *   16        2000      .008         32.768
 *   32        1000      .016         65.536
 *   64         500      .032        131.072
 *  128         250      .064        262.144
 *  256         125      .128        524.288
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/geode.h>

#define F_AVAIL    0x01

static struct mfgpt_timer_t {
	int flags;
	struct module *owner;
} mfgpt_timers[MFGPT_MAX_TIMERS];

/* Selected from the table above */

#define MFGPT_DIVISOR 16
#define MFGPT_SCALE  4     /* divisor = 2^(scale) */
#define MFGPT_HZ  (32000 / MFGPT_DIVISOR)
#define MFGPT_PERIODIC (MFGPT_HZ / HZ)

#ifdef CONFIG_GEODE_MFGPT_TIMER
static int __init mfgpt_timer_setup(void);
#else
#define mfgpt_timer_setup() (0)
#endif

/* Allow for disabling of MFGPTs */
static int disable;
static int __init mfgpt_disable(char *s)
{
	disable = 1;
	return 1;
}
__setup("nomfgpt", mfgpt_disable);

/*
 * Check whether any MFGPTs are available for the kernel to use.  In most
 * cases, firmware that uses AMD's VSA code will claim all timers during
 * bootup; we certainly don't want to take them if they're already in use.
 * In other cases (such as with VSAless OpenFirmware), the system firmware
 * leaves timers available for us to use.
 */
int __init geode_mfgpt_detect(void)
{
	int count = 0, i;
	u16 val;

	if (disable) {
		printk(KERN_INFO "geode-mfgpt:  Skipping MFGPT setup\n");
		return 0;
	}

	for (i = 0; i < MFGPT_MAX_TIMERS; i++) {
		val = geode_mfgpt_read(i, MFGPT_REG_SETUP);
		if (!(val & MFGPT_SETUP_SETUP)) {
			mfgpt_timers[i].flags = F_AVAIL;
			count++;
		}
	}

	/* set up clock event device, if desired */
	i = mfgpt_timer_setup();

	return count;
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

static int mfgpt_get(int timer, struct module *owner)
{
	mfgpt_timers[timer].flags &= ~F_AVAIL;
	mfgpt_timers[timer].owner = owner;
	printk(KERN_INFO "geode-mfgpt:  Registered timer %d\n", timer);
	return timer;
}

int geode_mfgpt_alloc_timer(int timer, int domain, struct module *owner)
{
	int i;

	if (!geode_get_dev_base(GEODE_DEV_MFGPT))
		return -ENODEV;
	if (timer >= MFGPT_MAX_TIMERS)
		return -EIO;

	if (timer < 0) {
		/* Try to find an available timer */
		for (i = 0; i < MFGPT_MAX_TIMERS; i++) {
			if (mfgpt_timers[i].flags & F_AVAIL)
				return mfgpt_get(i, owner);

			if (i == 5 && domain == MFGPT_DOMAIN_WORKING)
				break;
		}
	} else {
		/* If they requested a specific timer, try to honor that */
		if (mfgpt_timers[timer].flags & F_AVAIL)
			return mfgpt_get(timer, owner);
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

static inline void mfgpt_disable_timer(u16 clock)
{
	u16 val = geode_mfgpt_read(clock, MFGPT_REG_SETUP);
	geode_mfgpt_write(clock, MFGPT_REG_SETUP, val & ~MFGPT_SETUP_CNTEN);
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

static inline void mfgpt_start_timer(u16 clock, u16 delta)
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
		mfgpt_start_timer(mfgpt_event_clock, MFGPT_PERIODIC);

	mfgpt_tick_mode = mode;
}

static int mfgpt_next_event(unsigned long delta, struct clock_event_device *evt)
{
	mfgpt_start_timer(mfgpt_event_clock, delta);
	return 0;
}

/* Assume (foolishly?), that this interrupt was due to our tick */

static irqreturn_t mfgpt_tick(int irq, void *dev_id)
{
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

static int __init mfgpt_timer_setup(void)
{
	int timer, ret;
	u16 val;

	timer = geode_mfgpt_alloc_timer(MFGPT_TIMER_ANY, MFGPT_DOMAIN_WORKING,
			THIS_MODULE);
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
	       "mfgpt-timer:  registering the MFGT timer as a clock event.\n");
	clockevents_register_device(&mfgpt_clockevent);

	return 0;

err:
	geode_mfgpt_release_irq(mfgpt_event_clock, MFGPT_CMP2, irq);
	printk(KERN_ERR
	       "mfgpt-timer:  Unable to set up the MFGPT clock source\n");
	return -EIO;
}

#endif

/*
 * Copyright (C) 2006 Jim Cromie
 *
 * This is a clocksource driver for the Geode SCx200's 1 or 27 MHz
 * high-resolution timer.  The Geode SC-1100 (at least) has a buggy
 * time stamp counter (TSC), which loses time unless 'idle=poll' is
 * given as a boot-arg. In its absence, the Generic Timekeeping code
 * will detect and de-rate the bad TSC, allowing this timer to take
 * over timekeeping duties.
 *
 * Based on work by John Stultz, and Ted Phelps (in a 2.6.12-rc6 patch)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/scx200.h>

#define NAME "scx200_hrt"

static int mhz27;
module_param(mhz27, int, 0);	/* load time only */
MODULE_PARM_DESC(mhz27, "count at 27.0 MHz (default is 1.0 MHz)");

static int ppm;
module_param(ppm, int, 0);	/* load time only */
MODULE_PARM_DESC(ppm, "+-adjust to actual XO freq (ppm)");

/* HiRes Timer configuration register address */
#define SCx200_TMCNFG_OFFSET (SCx200_TIMER_OFFSET + 5)

/* and config settings */
#define HR_TMEN (1 << 0)	/* timer interrupt enable */
#define HR_TMCLKSEL (1 << 1)	/* 1|0 counts at 27|1 MHz */
#define HR_TM27MPD (1 << 2)	/* 1 turns off input clock (power-down) */

/* The base timer frequency, * 27 if selected */
#define HRT_FREQ   1000000

static cycle_t read_hrt(struct clocksource *cs)
{
	/* Read the timer value */
	return (cycle_t) inl(scx200_cb_base + SCx200_TIMER_OFFSET);
}

#define HRT_SHIFT_1	22
#define HRT_SHIFT_27	26

static struct clocksource cs_hrt = {
	.name		= "scx200_hrt",
	.rating		= 250,
	.read		= read_hrt,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	/* mult, shift are set based on mhz27 flag */
};

static int __init init_hrt_clocksource(void)
{
	/* Make sure scx200 has initialized the configuration block */
	if (!scx200_cb_present())
		return -ENODEV;

	/* Reserve the timer's ISA io-region for ourselves */
	if (!request_region(scx200_cb_base + SCx200_TIMER_OFFSET,
			    SCx200_TIMER_SIZE,
			    "NatSemi SCx200 High-Resolution Timer")) {
		printk(KERN_WARNING NAME ": unable to lock timer region\n");
		return -ENODEV;
	}

	/* write timer config */
	outb(HR_TMEN | (mhz27 ? HR_TMCLKSEL : 0),
	     scx200_cb_base + SCx200_TMCNFG_OFFSET);

	if (mhz27) {
		cs_hrt.shift = HRT_SHIFT_27;
		cs_hrt.mult = clocksource_hz2mult((HRT_FREQ + ppm) * 27,
						  cs_hrt.shift);
	} else {
		cs_hrt.shift = HRT_SHIFT_1;
		cs_hrt.mult = clocksource_hz2mult(HRT_FREQ + ppm,
						  cs_hrt.shift);
	}
	printk(KERN_INFO "enabling scx200 high-res timer (%s MHz +%d ppm)\n",
		mhz27 ? "27":"1", ppm);

	return clocksource_register(&cs_hrt);
}

module_init(init_hrt_clocksource);

MODULE_AUTHOR("Jim Cromie <jim.cromie@gmail.com>");
MODULE_DESCRIPTION("clocksource on SCx200 HiRes Timer");
MODULE_LICENSE("GPL");

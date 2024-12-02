// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Mattias Wallin <mattias.wallin@stericsson.com> for ST-Ericsson
 * Author: Sundar Iyer for ST-Ericsson
 * sched_clock implementation is based on:
 * plat-nomadik/timer.c Linus Walleij <linus.walleij@stericsson.com>
 *
 * DBx500-PRCMU Timer
 * The PRCMU has 5 timers which are available in a always-on
 * power domain.  We use the Timer 4 for our always-on clock
 * source on DB8500.
 */
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clockchips.h>

#define RATE_32K		32768

#define TIMER_MODE_CONTINUOUS	0x1
#define TIMER_DOWNCOUNT_VAL	0xffffffff

#define PRCMU_TIMER_REF		0
#define PRCMU_TIMER_DOWNCOUNT	0x4
#define PRCMU_TIMER_MODE	0x8

static void __iomem *clksrc_dbx500_timer_base;

static u64 notrace clksrc_dbx500_prcmu_read(struct clocksource *cs)
{
	void __iomem *base = clksrc_dbx500_timer_base;
	u32 count, count2;

	do {
		count = readl_relaxed(base + PRCMU_TIMER_DOWNCOUNT);
		count2 = readl_relaxed(base + PRCMU_TIMER_DOWNCOUNT);
	} while (count2 != count);

	/* Negate because the timer is a decrementing counter */
	return ~count;
}

static struct clocksource clocksource_dbx500_prcmu = {
	.name		= "dbx500-prcmu-timer",
	.rating		= 100,
	.read		= clksrc_dbx500_prcmu_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP,
};

static int __init clksrc_dbx500_prcmu_init(struct device_node *node)
{
	clksrc_dbx500_timer_base = of_iomap(node, 0);

	/*
	 * The A9 sub system expects the timer to be configured as
	 * a continuous looping timer.
	 * The PRCMU should configure it but if it for some reason
	 * don't we do it here.
	 */
	if (readl(clksrc_dbx500_timer_base + PRCMU_TIMER_MODE) !=
	    TIMER_MODE_CONTINUOUS) {
		writel(TIMER_MODE_CONTINUOUS,
		       clksrc_dbx500_timer_base + PRCMU_TIMER_MODE);
		writel(TIMER_DOWNCOUNT_VAL,
		       clksrc_dbx500_timer_base + PRCMU_TIMER_REF);
	}
	return clocksource_register_hz(&clocksource_dbx500_prcmu, RATE_32K);
}
TIMER_OF_DECLARE(dbx500_prcmu, "stericsson,db8500-prcmu-timer-4",
		       clksrc_dbx500_prcmu_init);

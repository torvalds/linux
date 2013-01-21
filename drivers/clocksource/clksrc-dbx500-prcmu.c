/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
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
#include <linux/clockchips.h>
#include <linux/clksrc-dbx500-prcmu.h>

#include <asm/sched_clock.h>

#define RATE_32K		32768

#define TIMER_MODE_CONTINOUS	0x1
#define TIMER_DOWNCOUNT_VAL	0xffffffff

#define PRCMU_TIMER_REF		0
#define PRCMU_TIMER_DOWNCOUNT	0x4
#define PRCMU_TIMER_MODE	0x8

#define SCHED_CLOCK_MIN_WRAP 131072 /* 2^32 / 32768 */

static void __iomem *clksrc_dbx500_timer_base;

static cycle_t notrace clksrc_dbx500_prcmu_read(struct clocksource *cs)
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
	.rating		= 300,
	.read		= clksrc_dbx500_prcmu_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

#ifdef CONFIG_CLKSRC_DBX500_PRCMU_SCHED_CLOCK

static u32 notrace dbx500_prcmu_sched_clock_read(void)
{
	if (unlikely(!clksrc_dbx500_timer_base))
		return 0;

	return clksrc_dbx500_prcmu_read(&clocksource_dbx500_prcmu);
}

#endif

void __init clksrc_dbx500_prcmu_init(void __iomem *base)
{
	clksrc_dbx500_timer_base = base;

	/*
	 * The A9 sub system expects the timer to be configured as
	 * a continous looping timer.
	 * The PRCMU should configure it but if it for some reason
	 * don't we do it here.
	 */
	if (readl(clksrc_dbx500_timer_base + PRCMU_TIMER_MODE) !=
	    TIMER_MODE_CONTINOUS) {
		writel(TIMER_MODE_CONTINOUS,
		       clksrc_dbx500_timer_base + PRCMU_TIMER_MODE);
		writel(TIMER_DOWNCOUNT_VAL,
		       clksrc_dbx500_timer_base + PRCMU_TIMER_REF);
	}
#ifdef CONFIG_CLKSRC_DBX500_PRCMU_SCHED_CLOCK
	setup_sched_clock(dbx500_prcmu_sched_clock_read,
			 32, RATE_32K);
#endif
	clocksource_register_hz(&clocksource_dbx500_prcmu, RATE_32K);
}

/*
 * OMAP 32ksynctimer/counter_32k-related code
 *
 * Copyright (C) 2009 Texas Instruments
 * Copyright (C) 2010 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOTE: This timer is not the same timer as the old OMAP1 MPU timer.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>

#include <plat/common.h>
#include <plat/board.h>

#include <plat/clock.h>


/*
 * 32KHz clocksource ... always available, on pretty most chips except
 * OMAP 730 and 1510.  Other timers could be used as clocksources, with
 * higher resolution in free-running counter modes (e.g. 12 MHz xtal),
 * but systems won't necessarily want to spend resources that way.
 */

#define OMAP16XX_TIMER_32K_SYNCHRONIZED		0xfffbc410

#if !(defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP15XX))

#include <linux/clocksource.h>

/*
 * offset_32k holds the init time counter value. It is then subtracted
 * from every counter read to achieve a counter that counts time from the
 * kernel boot (needed for sched_clock()).
 */
static u32 offset_32k __read_mostly;

#ifdef CONFIG_ARCH_OMAP16XX
static cycle_t omap16xx_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP16XX_TIMER_32K_SYNCHRONIZED) - offset_32k;
}
#else
#define omap16xx_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP2420
static cycle_t omap2420_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP2420_32KSYNCT_BASE + 0x10) - offset_32k;
}
#else
#define omap2420_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP2430
static cycle_t omap2430_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP2430_32KSYNCT_BASE + 0x10) - offset_32k;
}
#else
#define omap2430_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP3
static cycle_t omap34xx_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP3430_32KSYNCT_BASE + 0x10) - offset_32k;
}
#else
#define omap34xx_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP4
static cycle_t omap44xx_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP4430_32KSYNCT_BASE + 0x10) - offset_32k;
}
#else
#define omap44xx_32k_read	NULL
#endif

/*
 * Kernel assumes that sched_clock can be called early but may not have
 * things ready yet.
 */
static cycle_t omap_32k_read_dummy(struct clocksource *cs)
{
	return 0;
}

static struct clocksource clocksource_32k = {
	.name		= "32k_counter",
	.rating		= 250,
	.read		= omap_32k_read_dummy,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 10,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Returns current time from boot in nsecs. It's OK for this to wrap
 * around for now, as it's just a relative time stamp.
 */
unsigned long long sched_clock(void)
{
	return clocksource_cyc2ns(clocksource_32k.read(&clocksource_32k),
				  clocksource_32k.mult, clocksource_32k.shift);
}

/**
 * read_persistent_clock -  Return time from a persistent clock.
 *
 * Reads the time from a source which isn't disabled during PM, the
 * 32k sync timer.  Convert the cycles elapsed since last read into
 * nsecs and adds to a monotonically increasing timespec.
 */
static struct timespec persistent_ts;
static cycles_t cycles, last_cycles;
void read_persistent_clock(struct timespec *ts)
{
	unsigned long long nsecs;
	cycles_t delta;
	struct timespec *tsp = &persistent_ts;

	last_cycles = cycles;
	cycles = clocksource_32k.read(&clocksource_32k);
	delta = cycles - last_cycles;

	nsecs = clocksource_cyc2ns(delta,
				   clocksource_32k.mult, clocksource_32k.shift);

	timespec_add_ns(tsp, nsecs);
	*ts = *tsp;
}

static int __init omap_init_clocksource_32k(void)
{
	static char err[] __initdata = KERN_ERR
			"%s: can't register clocksource!\n";

	if (cpu_is_omap16xx() || cpu_class_is_omap2()) {
		struct clk *sync_32k_ick;

		if (cpu_is_omap16xx())
			clocksource_32k.read = omap16xx_32k_read;
		else if (cpu_is_omap2420())
			clocksource_32k.read = omap2420_32k_read;
		else if (cpu_is_omap2430())
			clocksource_32k.read = omap2430_32k_read;
		else if (cpu_is_omap34xx())
			clocksource_32k.read = omap34xx_32k_read;
		else if (cpu_is_omap44xx())
			clocksource_32k.read = omap44xx_32k_read;
		else
			return -ENODEV;

		sync_32k_ick = clk_get(NULL, "omap_32ksync_ick");
		if (!IS_ERR(sync_32k_ick))
			clk_enable(sync_32k_ick);

		clocksource_32k.mult = clocksource_hz2mult(32768,
					    clocksource_32k.shift);

		offset_32k = clocksource_32k.read(&clocksource_32k);

		if (clocksource_register(&clocksource_32k))
			printk(err, clocksource_32k.name);
	}
	return 0;
}
arch_initcall(omap_init_clocksource_32k);

#endif	/* !(defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP15XX)) */


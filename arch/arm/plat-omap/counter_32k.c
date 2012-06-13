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
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clocksource.h>

#include <asm/mach/time.h>
#include <asm/sched_clock.h>

#include <plat/hardware.h>
#include <plat/common.h>
#include <plat/board.h>

#include <plat/clock.h>

/* OMAP2_32KSYNCNT_CR_OFF: offset of 32ksync counter register */
#define OMAP2_32KSYNCNT_CR_OFF		0x10

/*
 * 32KHz clocksource ... always available, on pretty most chips except
 * OMAP 730 and 1510.  Other timers could be used as clocksources, with
 * higher resolution in free-running counter modes (e.g. 12 MHz xtal),
 * but systems won't necessarily want to spend resources that way.
 */
static void __iomem *sync32k_cnt_reg;

static u32 notrace omap_32k_read_sched_clock(void)
{
	return sync32k_cnt_reg ? __raw_readl(sync32k_cnt_reg) : 0;
}

/**
 * omap_read_persistent_clock -  Return time from a persistent clock.
 *
 * Reads the time from a source which isn't disabled during PM, the
 * 32k sync timer.  Convert the cycles elapsed since last read into
 * nsecs and adds to a monotonically increasing timespec.
 */
static struct timespec persistent_ts;
static cycles_t cycles, last_cycles;
static unsigned int persistent_mult, persistent_shift;
static void omap_read_persistent_clock(struct timespec *ts)
{
	unsigned long long nsecs;
	cycles_t delta;
	struct timespec *tsp = &persistent_ts;

	last_cycles = cycles;
	cycles = sync32k_cnt_reg ? __raw_readl(sync32k_cnt_reg) : 0;
	delta = cycles - last_cycles;

	nsecs = clocksource_cyc2ns(delta, persistent_mult, persistent_shift);

	timespec_add_ns(tsp, nsecs);
	*ts = *tsp;
}

/**
 * omap_init_clocksource_32k - setup and register counter 32k as a
 * kernel clocksource
 * @pbase: base addr of counter_32k module
 * @size: size of counter_32k to map
 *
 * Returns 0 upon success or negative error code upon failure.
 *
 */
int __init omap_init_clocksource_32k(void __iomem *vbase)
{
	int ret;

	/*
	 * 32k sync Counter register offset is at 0x10
	 */
	sync32k_cnt_reg = vbase + OMAP2_32KSYNCNT_CR_OFF;

	/*
	 * 120000 rough estimate from the calculations in
	 * __clocksource_updatefreq_scale.
	 */
	clocks_calc_mult_shift(&persistent_mult, &persistent_shift,
			32768, NSEC_PER_SEC, 120000);

	ret = clocksource_mmio_init(sync32k_cnt_reg, "32k_counter", 32768,
				250, 32, clocksource_mmio_readl_up);
	if (ret) {
		pr_err("32k_counter: can't register clocksource\n");
		return ret;
	}

	setup_sched_clock(omap_32k_read_sched_clock, 32, 32768);
	register_persistent_clock(NULL, omap_read_persistent_clock);
	pr_info("OMAP clocksource: 32k_counter at 32768 Hz\n");

	return 0;
}

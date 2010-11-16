/*
 * AEMIF support for DaVinci SoCs
 *
 * Copyright (C) 2010 Texas Instruments Incorporated. http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/time.h>

#include <mach/aemif.h>

/* Timing value configuration */

#define TA(x)		((x) << 2)
#define RHOLD(x)	((x) << 4)
#define RSTROBE(x)	((x) << 7)
#define RSETUP(x)	((x) << 13)
#define WHOLD(x)	((x) << 17)
#define WSTROBE(x)	((x) << 20)
#define WSETUP(x)	((x) << 26)

#define TA_MAX		0x3
#define RHOLD_MAX	0x7
#define RSTROBE_MAX	0x3f
#define RSETUP_MAX	0xf
#define WHOLD_MAX	0x7
#define WSTROBE_MAX	0x3f
#define WSETUP_MAX	0xf

#define TIMING_MASK	(TA(TA_MAX) | \
				RHOLD(RHOLD_MAX) | \
				RSTROBE(RSTROBE_MAX) |	\
				RSETUP(RSETUP_MAX) | \
				WHOLD(WHOLD_MAX) | \
				WSTROBE(WSTROBE_MAX) | \
				WSETUP(WSETUP_MAX))

/*
 * aemif_calc_rate - calculate timing data.
 * @wanted: The cycle time needed in nanoseconds.
 * @clk: The input clock rate in kHz.
 * @max: The maximum divider value that can be programmed.
 *
 * On success, returns the calculated timing value minus 1 for easy
 * programming into AEMIF timing registers, else negative errno.
 */
static int aemif_calc_rate(int wanted, unsigned long clk, int max)
{
	int result;

	result = DIV_ROUND_UP((wanted * clk), NSEC_PER_MSEC) - 1;

	pr_debug("%s: result %d from %ld, %d\n", __func__, result, clk, wanted);

	/* It is generally OK to have a more relaxed timing than requested... */
	if (result < 0)
		result = 0;

	/* ... But configuring tighter timings is not an option. */
	else if (result > max)
		result = -EINVAL;

	return result;
}

/**
 * davinci_aemif_setup_timing - setup timing values for a given AEMIF interface
 * @t: timing values to be progammed
 * @base: The virtual base address of the AEMIF interface
 * @cs: chip-select to program the timing values for
 *
 * This function programs the given timing values (in real clock) into the
 * AEMIF registers taking the AEMIF clock into account.
 *
 * This function does not use any locking while programming the AEMIF
 * because it is expected that there is only one user of a given
 * chip-select.
 *
 * Returns 0 on success, else negative errno.
 */
int davinci_aemif_setup_timing(struct davinci_aemif_timing *t,
					void __iomem *base, unsigned cs)
{
	unsigned set, val;
	unsigned ta, rhold, rstrobe, rsetup, whold, wstrobe, wsetup;
	unsigned offset = A1CR_OFFSET + cs * 4;
	struct clk *aemif_clk;
	unsigned long clkrate;

	if (!t)
		return 0;	/* Nothing to do */

	aemif_clk = clk_get(NULL, "aemif");
	if (IS_ERR(aemif_clk))
		return PTR_ERR(aemif_clk);

	clkrate = clk_get_rate(aemif_clk);

	clkrate /= 1000;	/* turn clock into kHz for ease of use */

	ta	= aemif_calc_rate(t->ta, clkrate, TA_MAX);
	rhold	= aemif_calc_rate(t->rhold, clkrate, RHOLD_MAX);
	rstrobe	= aemif_calc_rate(t->rstrobe, clkrate, RSTROBE_MAX);
	rsetup	= aemif_calc_rate(t->rsetup, clkrate, RSETUP_MAX);
	whold	= aemif_calc_rate(t->whold, clkrate, WHOLD_MAX);
	wstrobe	= aemif_calc_rate(t->wstrobe, clkrate, WSTROBE_MAX);
	wsetup	= aemif_calc_rate(t->wsetup, clkrate, WSETUP_MAX);

	if (ta < 0 || rhold < 0 || rstrobe < 0 || rsetup < 0 ||
			whold < 0 || wstrobe < 0 || wsetup < 0) {
		pr_err("%s: cannot get suitable timings\n", __func__);
		return -EINVAL;
	}

	set = TA(ta) | RHOLD(rhold) | RSTROBE(rstrobe) | RSETUP(rsetup) |
		WHOLD(whold) | WSTROBE(wstrobe) | WSETUP(wsetup);

	val = __raw_readl(base + offset);
	val &= ~TIMING_MASK;
	val |= set;
	__raw_writel(val, base + offset);

	return 0;
}
EXPORT_SYMBOL(davinci_aemif_setup_timing);

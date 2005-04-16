/*
 *  linux/arch/arm/common/icst525.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Support functions for calculating clocks/divisors for the ICST525
 *  clock generators.  See http://www.icst.com/ for more information
 *  on these devices.
 */
#include <linux/module.h>
#include <linux/kernel.h>

#include <asm/hardware/icst525.h>

/*
 * Divisors for each OD setting.
 */
static unsigned char s2div[8] = { 10, 2, 8, 4, 5, 7, 9, 6 };

unsigned long icst525_khz(const struct icst525_params *p, struct icst525_vco vco)
{
	return p->ref * 2 * (vco.v + 8) / ((vco.r + 2) * s2div[vco.s]);
}

EXPORT_SYMBOL(icst525_khz);

/*
 * Ascending divisor S values.
 */
static unsigned char idx2s[] = { 1, 3, 4, 7, 5, 2, 6, 0 };

struct icst525_vco
icst525_khz_to_vco(const struct icst525_params *p, unsigned long freq)
{
	struct icst525_vco vco = { .s = 1, .v = p->vd_max, .r = p->rd_max };
	unsigned long f;
	unsigned int i = 0, rd, best = (unsigned int)-1;

	/*
	 * First, find the PLL output divisor such
	 * that the PLL output is within spec.
	 */
	do {
		f = freq * s2div[idx2s[i]];

		/*
		 * f must be between 10MHz and
		 *  320MHz (5V) or 200MHz (3V)
		 */
		if (f > 10000 && f <= p->vco_max)
			break;
	} while (i < ARRAY_SIZE(idx2s));

	if (i > ARRAY_SIZE(idx2s))
		return vco;

	vco.s = idx2s[i];

	/*
	 * Now find the closest divisor combination
	 * which gives a PLL output of 'f'.
	 */
	for (rd = p->rd_min; rd <= p->rd_max; rd++) {
		unsigned long fref_div, f_pll;
		unsigned int vd;
		int f_diff;

		fref_div = (2 * p->ref) / rd;

		vd = (f + fref_div / 2) / fref_div;
		if (vd < p->vd_min || vd > p->vd_max)
			continue;

		f_pll = fref_div * vd;
		f_diff = f_pll - f;
		if (f_diff < 0)
			f_diff = -f_diff;

		if ((unsigned)f_diff < best) {
			vco.v = vd - 8;
			vco.r = rd - 2;
			if (f_diff == 0)
				break;
			best = f_diff;
		}
	}

	return vco;
}

EXPORT_SYMBOL(icst525_khz_to_vco);

struct icst525_vco
icst525_ps_to_vco(const struct icst525_params *p, unsigned long period)
{
	struct icst525_vco vco = { .s = 1, .v = p->vd_max, .r = p->rd_max };
	unsigned long f, ps;
	unsigned int i = 0, rd, best = (unsigned int)-1;

	ps = 1000000000UL / p->vco_max;

	/*
	 * First, find the PLL output divisor such
	 * that the PLL output is within spec.
	 */
	do {
		f = period / s2div[idx2s[i]];

		/*
		 * f must be between 10MHz and
		 *  320MHz (5V) or 200MHz (3V)
		 */
		if (f >= ps && f < 100000)
			break;
	} while (i < ARRAY_SIZE(idx2s));

	if (i > ARRAY_SIZE(idx2s))
		return vco;

	vco.s = idx2s[i];

	ps = 500000000UL / p->ref;

	/*
	 * Now find the closest divisor combination
	 * which gives a PLL output of 'f'.
	 */
	for (rd = p->rd_min; rd <= p->rd_max; rd++) {
		unsigned long f_in_div, f_pll;
		unsigned int vd;
		int f_diff;

		f_in_div = ps * rd;

		vd = (f_in_div + f / 2) / f;
		if (vd < p->vd_min || vd > p->vd_max)
			continue;

		f_pll = (f_in_div + vd / 2) / vd;
		f_diff = f_pll - f;
		if (f_diff < 0)
			f_diff = -f_diff;

		if ((unsigned)f_diff < best) {
			vco.v = vd - 8;
			vco.r = rd - 2;
			if (f_diff == 0)
				break;
			best = f_diff;
		}
	}

	return vco;
}

EXPORT_SYMBOL(icst525_ps_to_vco);

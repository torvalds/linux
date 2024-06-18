// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/common/icst307.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 *  Support functions for calculating clocks/divisors for the ICST307
 *  clock generators.  See https://www.idt.com/ for more information
 *  on these devices.
 *
 *  This is an almost identical implementation to the ICST525 clock generator.
 *  The s2div and idx2s files are different
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/div64.h>
#include "icst.h"

/*
 * Divisors for each OD setting.
 */
const unsigned char icst307_s2div[8] = { 10, 2, 8, 4, 5, 7, 3, 6 };
const unsigned char icst525_s2div[8] = { 10, 2, 8, 4, 5, 7, 9, 6 };
EXPORT_SYMBOL(icst307_s2div);
EXPORT_SYMBOL(icst525_s2div);

unsigned long icst_hz(const struct icst_params *p, struct icst_vco vco)
{
	u64 dividend = p->ref * 2 * (u64)(vco.v + 8);
	u32 divisor = (vco.r + 2) * p->s2div[vco.s];

	do_div(dividend, divisor);
	return (unsigned long)dividend;
}

EXPORT_SYMBOL(icst_hz);

/*
 * Ascending divisor S values.
 */
const unsigned char icst307_idx2s[8] = { 1, 6, 3, 4, 7, 5, 2, 0 };
const unsigned char icst525_idx2s[8] = { 1, 3, 4, 7, 5, 2, 6, 0 };
EXPORT_SYMBOL(icst307_idx2s);
EXPORT_SYMBOL(icst525_idx2s);

struct icst_vco
icst_hz_to_vco(const struct icst_params *p, unsigned long freq)
{
	struct icst_vco vco = { .s = 1, .v = p->vd_max, .r = p->rd_max };
	unsigned long f;
	unsigned int i = 0, rd, best = (unsigned int)-1;

	/*
	 * First, find the PLL output divisor such
	 * that the PLL output is within spec.
	 */
	do {
		f = freq * p->s2div[p->idx2s[i]];

		if (f > p->vco_min && f <= p->vco_max)
			break;
		i++;
	} while (i < 8);

	if (i >= 8)
		return vco;

	vco.s = p->idx2s[i];

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

EXPORT_SYMBOL(icst_hz_to_vco);

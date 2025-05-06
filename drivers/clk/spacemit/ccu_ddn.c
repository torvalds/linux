// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 SpacemiT Technology Co. Ltd
 * Copyright (c) 2024-2025 Haylen Chu <heylenay@4d2.org>
 *
 * DDN stands for "Divider Denominator Numerator", it's M/N clock with a
 * constant x2 factor. This clock hardware follows the equation below,
 *
 *	      numerator       Fin
 *	2 * ------------- = -------
 *	     denominator      Fout
 *
 * Thus, Fout could be calculated with,
 *
 *		Fin	denominator
 *	Fout = ----- * -------------
 *		 2	 numerator
 */

#include <linux/clk-provider.h>
#include <linux/rational.h>

#include "ccu_ddn.h"

static unsigned long ccu_ddn_calc_rate(unsigned long prate,
				       unsigned long num, unsigned long den)
{
	return prate * den / 2 / num;
}

static unsigned long ccu_ddn_calc_best_rate(struct ccu_ddn *ddn,
					    unsigned long rate, unsigned long prate,
					    unsigned long *num, unsigned long *den)
{
	rational_best_approximation(rate, prate / 2,
				    ddn->den_mask >> ddn->den_shift,
				    ddn->num_mask >> ddn->num_shift,
				    den, num);
	return ccu_ddn_calc_rate(prate, *num, *den);
}

static long ccu_ddn_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	struct ccu_ddn *ddn = hw_to_ccu_ddn(hw);
	unsigned long num, den;

	return ccu_ddn_calc_best_rate(ddn, rate, *prate, &num, &den);
}

static unsigned long ccu_ddn_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct ccu_ddn *ddn = hw_to_ccu_ddn(hw);
	unsigned int val, num, den;

	val = ccu_read(&ddn->common, ctrl);

	num = (val & ddn->num_mask) >> ddn->num_shift;
	den = (val & ddn->den_mask) >> ddn->den_shift;

	return ccu_ddn_calc_rate(prate, num, den);
}

static int ccu_ddn_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long prate)
{
	struct ccu_ddn *ddn = hw_to_ccu_ddn(hw);
	unsigned long num, den;

	ccu_ddn_calc_best_rate(ddn, rate, prate, &num, &den);

	ccu_update(&ddn->common, ctrl,
		   ddn->num_mask | ddn->den_mask,
		   (num << ddn->num_shift) | (den << ddn->den_shift));

	return 0;
}

const struct clk_ops spacemit_ccu_ddn_ops = {
	.recalc_rate	= ccu_ddn_recalc_rate,
	.round_rate	= ccu_ddn_round_rate,
	.set_rate	= ccu_ddn_set_rate,
};

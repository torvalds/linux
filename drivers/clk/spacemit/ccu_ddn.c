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

static unsigned long ccu_ddn_calc_rate(unsigned long prate, unsigned long num,
				       unsigned long den, unsigned int pre_div)
{
	return prate * den / pre_div / num;
}

static unsigned long ccu_ddn_calc_best_rate(struct ccu_ddn *ddn,
					    unsigned long rate, unsigned long prate,
					    unsigned long *num, unsigned long *den)
{
	rational_best_approximation(rate, prate / ddn->pre_div,
				    ddn->den_mask >> ddn->den_shift,
				    ddn->num_mask >> ddn->num_shift,
				    den, num);
	return ccu_ddn_calc_rate(prate, *num, *den, ddn->pre_div);
}

static int ccu_ddn_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct ccu_ddn *ddn = hw_to_ccu_ddn(hw);
	unsigned long num, den;

	req->rate = ccu_ddn_calc_best_rate(ddn, req->rate,
					   req->best_parent_rate, &num, &den);

	return 0;
}

static unsigned long ccu_ddn_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct ccu_ddn *ddn = hw_to_ccu_ddn(hw);
	unsigned int val, num, den;

	val = ccu_read(&ddn->common, ctrl);

	num = (val & ddn->num_mask) >> ddn->num_shift;
	den = (val & ddn->den_mask) >> ddn->den_shift;

	return ccu_ddn_calc_rate(prate, num, den, ddn->pre_div);
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
	.determine_rate = ccu_ddn_determine_rate,
	.set_rate	= ccu_ddn_set_rate,
};

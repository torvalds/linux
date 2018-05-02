/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (c) 2016 AmLogic, Inc.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING
 *
 * BSD LICENSE
 *
 * Copyright (c) 2016 AmLogic, Inc.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * MultiPhase Locked Loops are outputs from a PLL with additional frequency
 * scaling capabilities. MPLL rates are calculated as:
 *
 * f(N2_integer, SDM_IN ) = 2.0G/(N2_integer + SDM_IN/16384)
 */

#include <linux/clk-provider.h>
#include "clkc.h"

#define SDM_DEN 16384
#define N2_MIN	4
#define N2_MAX	511

static inline struct meson_clk_mpll_data *
meson_clk_mpll_data(struct clk_regmap *clk)
{
	return (struct meson_clk_mpll_data *)clk->data;
}

static long rate_from_params(unsigned long parent_rate,
			     unsigned int sdm,
			     unsigned int n2)
{
	unsigned long divisor = (SDM_DEN * n2) + sdm;

	if (n2 < N2_MIN)
		return -EINVAL;

	return DIV_ROUND_UP_ULL((u64)parent_rate * SDM_DEN, divisor);
}

static void params_from_rate(unsigned long requested_rate,
			     unsigned long parent_rate,
			     unsigned int *sdm,
			     unsigned int *n2)
{
	uint64_t div = parent_rate;
	unsigned long rem = do_div(div, requested_rate);

	if (div < N2_MIN) {
		*n2 = N2_MIN;
		*sdm = 0;
	} else if (div > N2_MAX) {
		*n2 = N2_MAX;
		*sdm = SDM_DEN - 1;
	} else {
		*n2 = div;
		*sdm = DIV_ROUND_UP_ULL((u64)rem * SDM_DEN, requested_rate);
	}
}

static unsigned long mpll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_mpll_data *mpll = meson_clk_mpll_data(clk);
	unsigned int sdm, n2;
	long rate;

	sdm = meson_parm_read(clk->map, &mpll->sdm);
	n2 = meson_parm_read(clk->map, &mpll->n2);

	rate = rate_from_params(parent_rate, sdm, n2);
	return rate < 0 ? 0 : rate;
}

static long mpll_round_rate(struct clk_hw *hw,
			    unsigned long rate,
			    unsigned long *parent_rate)
{
	unsigned int sdm, n2;

	params_from_rate(rate, *parent_rate, &sdm, &n2);
	return rate_from_params(*parent_rate, sdm, n2);
}

static int mpll_set_rate(struct clk_hw *hw,
			 unsigned long rate,
			 unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_mpll_data *mpll = meson_clk_mpll_data(clk);
	unsigned int sdm, n2;
	unsigned long flags = 0;

	params_from_rate(rate, parent_rate, &sdm, &n2);

	if (mpll->lock)
		spin_lock_irqsave(mpll->lock, flags);
	else
		__acquire(mpll->lock);

	/* Enable and set the fractional part */
	meson_parm_write(clk->map, &mpll->sdm, sdm);
	meson_parm_write(clk->map, &mpll->sdm_en, 1);

	/* Set additional fractional part enable if required */
	if (MESON_PARM_APPLICABLE(&mpll->ssen))
		meson_parm_write(clk->map, &mpll->ssen, 1);

	/* Set the integer divider part */
	meson_parm_write(clk->map, &mpll->n2, n2);

	/* Set the magic misc bit if required */
	if (MESON_PARM_APPLICABLE(&mpll->misc))
		meson_parm_write(clk->map, &mpll->misc, 1);

	if (mpll->lock)
		spin_unlock_irqrestore(mpll->lock, flags);
	else
		__release(mpll->lock);

	return 0;
}

const struct clk_ops meson_clk_mpll_ro_ops = {
	.recalc_rate	= mpll_recalc_rate,
	.round_rate	= mpll_round_rate,
};

const struct clk_ops meson_clk_mpll_ops = {
	.recalc_rate	= mpll_recalc_rate,
	.round_rate	= mpll_round_rate,
	.set_rate	= mpll_set_rate,
};

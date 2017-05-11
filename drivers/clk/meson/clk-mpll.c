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

#define to_meson_clk_mpll(_hw) container_of(_hw, struct meson_clk_mpll, hw)

static long rate_from_params(unsigned long parent_rate,
				      unsigned long sdm,
				      unsigned long n2)
{
	unsigned long divisor = (SDM_DEN * n2) + sdm;

	if (n2 < N2_MIN)
		return -EINVAL;

	return DIV_ROUND_UP_ULL((u64)parent_rate * SDM_DEN, divisor);
}

static void params_from_rate(unsigned long requested_rate,
			     unsigned long parent_rate,
			     unsigned long *sdm,
			     unsigned long *n2)
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
		*sdm = DIV_ROUND_UP(rem * SDM_DEN, requested_rate);
	}
}

static unsigned long mpll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct meson_clk_mpll *mpll = to_meson_clk_mpll(hw);
	struct parm *p;
	unsigned long reg, sdm, n2;
	long rate;

	p = &mpll->sdm;
	reg = readl(mpll->base + p->reg_off);
	sdm = PARM_GET(p->width, p->shift, reg);

	p = &mpll->n2;
	reg = readl(mpll->base + p->reg_off);
	n2 = PARM_GET(p->width, p->shift, reg);

	rate = rate_from_params(parent_rate, sdm, n2);
	if (rate < 0)
		return 0;

	return rate;
}

static long mpll_round_rate(struct clk_hw *hw,
			    unsigned long rate,
			    unsigned long *parent_rate)
{
	unsigned long sdm, n2;

	params_from_rate(rate, *parent_rate, &sdm, &n2);
	return rate_from_params(*parent_rate, sdm, n2);
}

static int mpll_set_rate(struct clk_hw *hw,
			 unsigned long rate,
			 unsigned long parent_rate)
{
	struct meson_clk_mpll *mpll = to_meson_clk_mpll(hw);
	struct parm *p;
	unsigned long reg, sdm, n2;
	unsigned long flags = 0;

	params_from_rate(rate, parent_rate, &sdm, &n2);

	if (mpll->lock)
		spin_lock_irqsave(mpll->lock, flags);
	else
		__acquire(mpll->lock);

	p = &mpll->sdm;
	reg = readl(mpll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, sdm);
	writel(reg, mpll->base + p->reg_off);

	p = &mpll->sdm_en;
	reg = readl(mpll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, 1);
	writel(reg, mpll->base + p->reg_off);

	p = &mpll->n2;
	reg = readl(mpll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, n2);
	writel(reg, mpll->base + p->reg_off);

	if (mpll->lock)
		spin_unlock_irqrestore(mpll->lock, flags);
	else
		__release(mpll->lock);

	return 0;
}

static void mpll_enable_core(struct clk_hw *hw, int enable)
{
	struct meson_clk_mpll *mpll = to_meson_clk_mpll(hw);
	struct parm *p;
	unsigned long reg;
	unsigned long flags = 0;

	if (mpll->lock)
		spin_lock_irqsave(mpll->lock, flags);
	else
		__acquire(mpll->lock);

	p = &mpll->en;
	reg = readl(mpll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, enable ? 1 : 0);
	writel(reg, mpll->base + p->reg_off);

	if (mpll->lock)
		spin_unlock_irqrestore(mpll->lock, flags);
	else
		__release(mpll->lock);
}


static int mpll_enable(struct clk_hw *hw)
{
	mpll_enable_core(hw, 1);

	return 0;
}

static void mpll_disable(struct clk_hw *hw)
{
	mpll_enable_core(hw, 0);
}

static int mpll_is_enabled(struct clk_hw *hw)
{
	struct meson_clk_mpll *mpll = to_meson_clk_mpll(hw);
	struct parm *p;
	unsigned long reg;
	int en;

	p = &mpll->en;
	reg = readl(mpll->base + p->reg_off);
	en = PARM_GET(p->width, p->shift, reg);

	return en;
}

const struct clk_ops meson_clk_mpll_ro_ops = {
	.recalc_rate	= mpll_recalc_rate,
	.round_rate	= mpll_round_rate,
	.is_enabled	= mpll_is_enabled,
};

const struct clk_ops meson_clk_mpll_ops = {
	.recalc_rate	= mpll_recalc_rate,
	.round_rate	= mpll_round_rate,
	.set_rate	= mpll_set_rate,
	.enable		= mpll_enable,
	.disable	= mpll_disable,
	.is_enabled	= mpll_is_enabled,
};

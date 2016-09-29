/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk-provider.h>

#include "ccu_gate.h"
#include "ccu_nk.h"

struct _ccu_nk {
	unsigned long	n, min_n, max_n;
	unsigned long	k, min_k, max_k;
};

static void ccu_nk_find_best(unsigned long parent, unsigned long rate,
			     struct _ccu_nk *nk)
{
	unsigned long best_rate = 0;
	unsigned int best_k = 0, best_n = 0;
	unsigned int _k, _n;

	for (_k = nk->min_k; _k <= nk->max_k; _k++) {
		for (_n = nk->min_n; _n <= nk->max_n; _n++) {
			unsigned long tmp_rate = parent * _n * _k;

			if (tmp_rate > rate)
				continue;

			if ((rate - tmp_rate) < (rate - best_rate)) {
				best_rate = tmp_rate;
				best_k = _k;
				best_n = _n;
			}
		}
	}

	nk->k = best_k;
	nk->n = best_n;
}

static void ccu_nk_disable(struct clk_hw *hw)
{
	struct ccu_nk *nk = hw_to_ccu_nk(hw);

	return ccu_gate_helper_disable(&nk->common, nk->enable);
}

static int ccu_nk_enable(struct clk_hw *hw)
{
	struct ccu_nk *nk = hw_to_ccu_nk(hw);

	return ccu_gate_helper_enable(&nk->common, nk->enable);
}

static int ccu_nk_is_enabled(struct clk_hw *hw)
{
	struct ccu_nk *nk = hw_to_ccu_nk(hw);

	return ccu_gate_helper_is_enabled(&nk->common, nk->enable);
}

static unsigned long ccu_nk_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct ccu_nk *nk = hw_to_ccu_nk(hw);
	unsigned long rate, n, k;
	u32 reg;

	reg = readl(nk->common.base + nk->common.reg);

	n = reg >> nk->n.shift;
	n &= (1 << nk->n.width) - 1;

	k = reg >> nk->k.shift;
	k &= (1 << nk->k.width) - 1;

	rate = parent_rate * (n + 1) * (k + 1);

	if (nk->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate /= nk->fixed_post_div;

	return rate;
}

static long ccu_nk_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *parent_rate)
{
	struct ccu_nk *nk = hw_to_ccu_nk(hw);
	struct _ccu_nk _nk;

	if (nk->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate *= nk->fixed_post_div;

	_nk.min_n = 1;
	_nk.max_n = 1 << nk->n.width;
	_nk.min_k = 1;
	_nk.max_k = 1 << nk->k.width;

	ccu_nk_find_best(*parent_rate, rate, &_nk);
	rate = *parent_rate * _nk.n * _nk.k;

	if (nk->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate = rate / nk->fixed_post_div;

	return rate;
}

static int ccu_nk_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct ccu_nk *nk = hw_to_ccu_nk(hw);
	unsigned long flags;
	struct _ccu_nk _nk;
	u32 reg;

	if (nk->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate = rate * nk->fixed_post_div;

	_nk.min_n = 1;
	_nk.max_n = 1 << nk->n.width;
	_nk.min_k = 1;
	_nk.max_k = 1 << nk->k.width;

	ccu_nk_find_best(parent_rate, rate, &_nk);

	spin_lock_irqsave(nk->common.lock, flags);

	reg = readl(nk->common.base + nk->common.reg);
	reg &= ~GENMASK(nk->n.width + nk->n.shift - 1, nk->n.shift);
	reg &= ~GENMASK(nk->k.width + nk->k.shift - 1, nk->k.shift);

	writel(reg | ((_nk.k - 1) << nk->k.shift) | ((_nk.n - 1) << nk->n.shift),
	       nk->common.base + nk->common.reg);

	spin_unlock_irqrestore(nk->common.lock, flags);

	ccu_helper_wait_for_lock(&nk->common, nk->lock);

	return 0;
}

const struct clk_ops ccu_nk_ops = {
	.disable	= ccu_nk_disable,
	.enable		= ccu_nk_enable,
	.is_enabled	= ccu_nk_is_enabled,

	.recalc_rate	= ccu_nk_recalc_rate,
	.round_rate	= ccu_nk_round_rate,
	.set_rate	= ccu_nk_set_rate,
};

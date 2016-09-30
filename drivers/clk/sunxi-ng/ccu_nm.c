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

#include "ccu_frac.h"
#include "ccu_gate.h"
#include "ccu_nm.h"

struct _ccu_nm {
	unsigned long	n, min_n, max_n;
	unsigned long	m, min_m, max_m;
};

static void ccu_nm_find_best(unsigned long parent, unsigned long rate,
			     struct _ccu_nm *nm)
{
	unsigned long best_rate = 0;
	unsigned long best_n = 0, best_m = 0;
	unsigned long _n, _m;

	for (_n = nm->min_n; _n <= nm->max_n; _n++) {
		for (_m = nm->min_m; _m <= nm->max_m; _m++) {
			unsigned long tmp_rate = parent * _n  / _m;

			if (tmp_rate > rate)
				continue;

			if ((rate - tmp_rate) < (rate - best_rate)) {
				best_rate = tmp_rate;
				best_n = _n;
				best_m = _m;
			}
		}
	}

	nm->n = best_n;
	nm->m = best_m;
}

static void ccu_nm_disable(struct clk_hw *hw)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);

	return ccu_gate_helper_disable(&nm->common, nm->enable);
}

static int ccu_nm_enable(struct clk_hw *hw)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);

	return ccu_gate_helper_enable(&nm->common, nm->enable);
}

static int ccu_nm_is_enabled(struct clk_hw *hw)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);

	return ccu_gate_helper_is_enabled(&nm->common, nm->enable);
}

static unsigned long ccu_nm_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);
	unsigned long n, m;
	u32 reg;

	if (ccu_frac_helper_is_enabled(&nm->common, &nm->frac))
		return ccu_frac_helper_read_rate(&nm->common, &nm->frac);

	reg = readl(nm->common.base + nm->common.reg);

	n = reg >> nm->n.shift;
	n &= (1 << nm->n.width) - 1;

	m = reg >> nm->m.shift;
	m &= (1 << nm->m.width) - 1;

	return parent_rate * (n + 1) / (m + 1);
}

static long ccu_nm_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *parent_rate)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);
	struct _ccu_nm _nm;

	_nm.min_n = nm->n.min;
	_nm.max_n = 1 << nm->n.width;
	_nm.min_m = 1;
	_nm.max_m = nm->m.max ?: 1 << nm->m.width;

	ccu_nm_find_best(*parent_rate, rate, &_nm);

	return *parent_rate * _nm.n / _nm.m;
}

static int ccu_nm_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);
	struct _ccu_nm _nm;
	unsigned long flags;
	u32 reg;

	if (ccu_frac_helper_has_rate(&nm->common, &nm->frac, rate))
		return ccu_frac_helper_set_rate(&nm->common, &nm->frac, rate);
	else
		ccu_frac_helper_disable(&nm->common, &nm->frac);

	_nm.min_n = 1;
	_nm.max_n = 1 << nm->n.width;
	_nm.min_m = 1;
	_nm.max_m = nm->m.max ?: 1 << nm->m.width;

	ccu_nm_find_best(parent_rate, rate, &_nm);

	spin_lock_irqsave(nm->common.lock, flags);

	reg = readl(nm->common.base + nm->common.reg);
	reg &= ~GENMASK(nm->n.width + nm->n.shift - 1, nm->n.shift);
	reg &= ~GENMASK(nm->m.width + nm->m.shift - 1, nm->m.shift);

	writel(reg | ((_nm.m - 1) << nm->m.shift) | ((_nm.n - 1) << nm->n.shift),
	       nm->common.base + nm->common.reg);

	spin_unlock_irqrestore(nm->common.lock, flags);

	ccu_helper_wait_for_lock(&nm->common, nm->lock);

	return 0;
}

const struct clk_ops ccu_nm_ops = {
	.disable	= ccu_nm_disable,
	.enable		= ccu_nm_enable,
	.is_enabled	= ccu_nm_is_enabled,

	.recalc_rate	= ccu_nm_recalc_rate,
	.round_rate	= ccu_nm_round_rate,
	.set_rate	= ccu_nm_set_rate,
};

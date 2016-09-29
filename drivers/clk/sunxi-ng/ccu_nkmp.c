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
#include "ccu_nkmp.h"

struct _ccu_nkmp {
	unsigned long	n, max_n;
	unsigned long	k, max_k;
	unsigned long	m, max_m;
	unsigned long	p, max_p;
};

static void ccu_nkmp_find_best(unsigned long parent, unsigned long rate,
			       struct _ccu_nkmp *nkmp)
{
	unsigned long best_rate = 0;
	unsigned long best_n = 0, best_k = 0, best_m = 0, best_p = 0;
	unsigned long _n, _k, _m, _p;

	for (_k = 1; _k <= nkmp->max_k; _k++) {
		for (_n = 1; _n <= nkmp->max_n; _n++) {
			for (_m = 1; _n <= nkmp->max_m; _m++) {
				for (_p = 1; _p <= nkmp->max_p; _p <<= 1) {
					unsigned long tmp_rate;

					tmp_rate = parent * _n * _k / (_m * _p);

					if (tmp_rate > rate)
						continue;

					if ((rate - tmp_rate) < (rate - best_rate)) {
						best_rate = tmp_rate;
						best_n = _n;
						best_k = _k;
						best_m = _m;
						best_p = _p;
					}
				}
			}
		}
	}

	nkmp->n = best_n;
	nkmp->k = best_k;
	nkmp->m = best_m;
	nkmp->p = best_p;
}

static void ccu_nkmp_disable(struct clk_hw *hw)
{
	struct ccu_nkmp *nkmp = hw_to_ccu_nkmp(hw);

	return ccu_gate_helper_disable(&nkmp->common, nkmp->enable);
}

static int ccu_nkmp_enable(struct clk_hw *hw)
{
	struct ccu_nkmp *nkmp = hw_to_ccu_nkmp(hw);

	return ccu_gate_helper_enable(&nkmp->common, nkmp->enable);
}

static int ccu_nkmp_is_enabled(struct clk_hw *hw)
{
	struct ccu_nkmp *nkmp = hw_to_ccu_nkmp(hw);

	return ccu_gate_helper_is_enabled(&nkmp->common, nkmp->enable);
}

static unsigned long ccu_nkmp_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct ccu_nkmp *nkmp = hw_to_ccu_nkmp(hw);
	unsigned long n, m, k, p;
	u32 reg;

	reg = readl(nkmp->common.base + nkmp->common.reg);

	n = reg >> nkmp->n.shift;
	n &= (1 << nkmp->n.width) - 1;

	k = reg >> nkmp->k.shift;
	k &= (1 << nkmp->k.width) - 1;

	m = reg >> nkmp->m.shift;
	m &= (1 << nkmp->m.width) - 1;

	p = reg >> nkmp->p.shift;
	p &= (1 << nkmp->p.width) - 1;

	return (parent_rate * (n + 1) * (k + 1) >> p) / (m + 1);
}

static long ccu_nkmp_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *parent_rate)
{
	struct ccu_nkmp *nkmp = hw_to_ccu_nkmp(hw);
	struct _ccu_nkmp _nkmp;

	_nkmp.max_n = 1 << nkmp->n.width;
	_nkmp.max_k = 1 << nkmp->k.width;
	_nkmp.max_m = nkmp->m.max ?: 1 << nkmp->m.width;
	_nkmp.max_p = nkmp->p.max ?: 1 << ((1 << nkmp->p.width) - 1);

	ccu_nkmp_find_best(*parent_rate, rate, &_nkmp);

	return *parent_rate * _nkmp.n * _nkmp.k / (_nkmp.m * _nkmp.p);
}

static int ccu_nkmp_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct ccu_nkmp *nkmp = hw_to_ccu_nkmp(hw);
	struct _ccu_nkmp _nkmp;
	unsigned long flags;
	u32 reg;

	_nkmp.max_n = 1 << nkmp->n.width;
	_nkmp.max_k = 1 << nkmp->k.width;
	_nkmp.max_m = nkmp->m.max ?: 1 << nkmp->m.width;
	_nkmp.max_p = nkmp->p.max ?: 1 << ((1 << nkmp->p.width) - 1);

	ccu_nkmp_find_best(parent_rate, rate, &_nkmp);

	spin_lock_irqsave(nkmp->common.lock, flags);

	reg = readl(nkmp->common.base + nkmp->common.reg);
	reg &= ~GENMASK(nkmp->n.width + nkmp->n.shift - 1, nkmp->n.shift);
	reg &= ~GENMASK(nkmp->k.width + nkmp->k.shift - 1, nkmp->k.shift);
	reg &= ~GENMASK(nkmp->m.width + nkmp->m.shift - 1, nkmp->m.shift);
	reg &= ~GENMASK(nkmp->p.width + nkmp->p.shift - 1, nkmp->p.shift);

	reg |= (_nkmp.n - 1) << nkmp->n.shift;
	reg |= (_nkmp.k - 1) << nkmp->k.shift;
	reg |= (_nkmp.m - 1) << nkmp->m.shift;
	reg |= ilog2(_nkmp.p) << nkmp->p.shift;

	writel(reg, nkmp->common.base + nkmp->common.reg);

	spin_unlock_irqrestore(nkmp->common.lock, flags);

	ccu_helper_wait_for_lock(&nkmp->common, nkmp->lock);

	return 0;
}

const struct clk_ops ccu_nkmp_ops = {
	.disable	= ccu_nkmp_disable,
	.enable		= ccu_nkmp_enable,
	.is_enabled	= ccu_nkmp_is_enabled,

	.recalc_rate	= ccu_nkmp_recalc_rate,
	.round_rate	= ccu_nkmp_round_rate,
	.set_rate	= ccu_nkmp_set_rate,
};

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>

#include "ccu_gate.h"
#include "ccu_nkm.h"

struct _ccu_nkm {
	unsigned long	n, min_n, max_n;
	unsigned long	k, min_k, max_k;
	unsigned long	m, min_m, max_m;
};

static unsigned long ccu_nkm_find_best(unsigned long parent, unsigned long rate,
				       struct _ccu_nkm *nkm)
{
	unsigned long best_rate = 0;
	unsigned long best_n = 0, best_k = 0, best_m = 0;
	unsigned long _n, _k, _m;

	for (_k = nkm->min_k; _k <= nkm->max_k; _k++) {
		for (_n = nkm->min_n; _n <= nkm->max_n; _n++) {
			for (_m = nkm->min_m; _m <= nkm->max_m; _m++) {
				unsigned long tmp_rate;

				tmp_rate = parent * _n * _k / _m;

				if (tmp_rate > rate)
					continue;
				if ((rate - tmp_rate) < (rate - best_rate)) {
					best_rate = tmp_rate;
					best_n = _n;
					best_k = _k;
					best_m = _m;
				}
			}
		}
	}

	nkm->n = best_n;
	nkm->k = best_k;
	nkm->m = best_m;

	return best_rate;
}

static void ccu_nkm_disable(struct clk_hw *hw)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);

	return ccu_gate_helper_disable(&nkm->common, nkm->enable);
}

static int ccu_nkm_enable(struct clk_hw *hw)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);

	return ccu_gate_helper_enable(&nkm->common, nkm->enable);
}

static int ccu_nkm_is_enabled(struct clk_hw *hw)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);

	return ccu_gate_helper_is_enabled(&nkm->common, nkm->enable);
}

static unsigned long ccu_nkm_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);
	unsigned long n, m, k, rate;
	u32 reg;

	reg = readl(nkm->common.base + nkm->common.reg);

	n = reg >> nkm->n.shift;
	n &= (1 << nkm->n.width) - 1;
	n += nkm->n.offset;
	if (!n)
		n++;

	k = reg >> nkm->k.shift;
	k &= (1 << nkm->k.width) - 1;
	k += nkm->k.offset;
	if (!k)
		k++;

	m = reg >> nkm->m.shift;
	m &= (1 << nkm->m.width) - 1;
	m += nkm->m.offset;
	if (!m)
		m++;

	rate = parent_rate * n  * k / m;

	if (nkm->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate /= nkm->fixed_post_div;

	return rate;
}

static unsigned long ccu_nkm_round_rate(struct ccu_mux_internal *mux,
					struct clk_hw *hw,
					unsigned long *parent_rate,
					unsigned long rate,
					void *data)
{
	struct ccu_nkm *nkm = data;
	struct _ccu_nkm _nkm;

	_nkm.min_n = nkm->n.min ?: 1;
	_nkm.max_n = nkm->n.max ?: 1 << nkm->n.width;
	_nkm.min_k = nkm->k.min ?: 1;
	_nkm.max_k = nkm->k.max ?: 1 << nkm->k.width;
	_nkm.min_m = 1;
	_nkm.max_m = nkm->m.max ?: 1 << nkm->m.width;

	if (nkm->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate *= nkm->fixed_post_div;

	rate = ccu_nkm_find_best(*parent_rate, rate, &_nkm);

	if (nkm->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate /= nkm->fixed_post_div;

	return rate;
}

static int ccu_nkm_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);

	return ccu_mux_helper_determine_rate(&nkm->common, &nkm->mux,
					     req, ccu_nkm_round_rate, nkm);
}

static int ccu_nkm_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);
	struct _ccu_nkm _nkm;
	unsigned long flags;
	u32 reg;

	if (nkm->common.features & CCU_FEATURE_FIXED_POSTDIV)
		rate *= nkm->fixed_post_div;

	_nkm.min_n = nkm->n.min ?: 1;
	_nkm.max_n = nkm->n.max ?: 1 << nkm->n.width;
	_nkm.min_k = nkm->k.min ?: 1;
	_nkm.max_k = nkm->k.max ?: 1 << nkm->k.width;
	_nkm.min_m = 1;
	_nkm.max_m = nkm->m.max ?: 1 << nkm->m.width;

	ccu_nkm_find_best(parent_rate, rate, &_nkm);

	spin_lock_irqsave(nkm->common.lock, flags);

	reg = readl(nkm->common.base + nkm->common.reg);
	reg &= ~GENMASK(nkm->n.width + nkm->n.shift - 1, nkm->n.shift);
	reg &= ~GENMASK(nkm->k.width + nkm->k.shift - 1, nkm->k.shift);
	reg &= ~GENMASK(nkm->m.width + nkm->m.shift - 1, nkm->m.shift);

	reg |= (_nkm.n - nkm->n.offset) << nkm->n.shift;
	reg |= (_nkm.k - nkm->k.offset) << nkm->k.shift;
	reg |= (_nkm.m - nkm->m.offset) << nkm->m.shift;
	writel(reg, nkm->common.base + nkm->common.reg);

	spin_unlock_irqrestore(nkm->common.lock, flags);

	ccu_helper_wait_for_lock(&nkm->common, nkm->lock);

	return 0;
}

static u8 ccu_nkm_get_parent(struct clk_hw *hw)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);

	return ccu_mux_helper_get_parent(&nkm->common, &nkm->mux);
}

static int ccu_nkm_set_parent(struct clk_hw *hw, u8 index)
{
	struct ccu_nkm *nkm = hw_to_ccu_nkm(hw);

	return ccu_mux_helper_set_parent(&nkm->common, &nkm->mux, index);
}

const struct clk_ops ccu_nkm_ops = {
	.disable	= ccu_nkm_disable,
	.enable		= ccu_nkm_enable,
	.is_enabled	= ccu_nkm_is_enabled,

	.get_parent	= ccu_nkm_get_parent,
	.set_parent	= ccu_nkm_set_parent,

	.determine_rate	= ccu_nkm_determine_rate,
	.recalc_rate	= ccu_nkm_recalc_rate,
	.set_rate	= ccu_nkm_set_rate,
};
EXPORT_SYMBOL_NS_GPL(ccu_nkm_ops, SUNXI_CCU);

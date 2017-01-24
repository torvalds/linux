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
#include "ccu_mult.h"

struct _ccu_mult {
	unsigned long	mult, min, max;
};

static void ccu_mult_find_best(unsigned long parent, unsigned long rate,
			       struct _ccu_mult *mult)
{
	int _mult;

	_mult = rate / parent;
	if (_mult < mult->min)
		_mult = mult->min;

	if (_mult > mult->max)
		_mult = mult->max;

	mult->mult = _mult;
}

static unsigned long ccu_mult_round_rate(struct ccu_mux_internal *mux,
					unsigned long parent_rate,
					unsigned long rate,
					void *data)
{
	struct ccu_mult *cm = data;
	struct _ccu_mult _cm;

	_cm.min = 1;
	_cm.max = 1 << cm->mult.width;
	ccu_mult_find_best(parent_rate, rate, &_cm);

	return parent_rate * _cm.mult;
}

static void ccu_mult_disable(struct clk_hw *hw)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);

	return ccu_gate_helper_disable(&cm->common, cm->enable);
}

static int ccu_mult_enable(struct clk_hw *hw)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);

	return ccu_gate_helper_enable(&cm->common, cm->enable);
}

static int ccu_mult_is_enabled(struct clk_hw *hw)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);

	return ccu_gate_helper_is_enabled(&cm->common, cm->enable);
}

static unsigned long ccu_mult_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);
	unsigned long val;
	u32 reg;

	reg = readl(cm->common.base + cm->common.reg);
	val = reg >> cm->mult.shift;
	val &= (1 << cm->mult.width) - 1;

	ccu_mux_helper_adjust_parent_for_prediv(&cm->common, &cm->mux, -1,
						&parent_rate);

	return parent_rate * (val + 1);
}

static int ccu_mult_determine_rate(struct clk_hw *hw,
				struct clk_rate_request *req)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);

	return ccu_mux_helper_determine_rate(&cm->common, &cm->mux,
					     req, ccu_mult_round_rate, cm);
}

static int ccu_mult_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);
	struct _ccu_mult _cm;
	unsigned long flags;
	u32 reg;

	ccu_mux_helper_adjust_parent_for_prediv(&cm->common, &cm->mux, -1,
						&parent_rate);

	_cm.min = cm->mult.min;
	_cm.max = 1 << cm->mult.width;
	ccu_mult_find_best(parent_rate, rate, &_cm);

	spin_lock_irqsave(cm->common.lock, flags);

	reg = readl(cm->common.base + cm->common.reg);
	reg &= ~GENMASK(cm->mult.width + cm->mult.shift - 1, cm->mult.shift);

	writel(reg | ((_cm.mult - 1) << cm->mult.shift),
	       cm->common.base + cm->common.reg);

	spin_unlock_irqrestore(cm->common.lock, flags);

	return 0;
}

static u8 ccu_mult_get_parent(struct clk_hw *hw)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);

	return ccu_mux_helper_get_parent(&cm->common, &cm->mux);
}

static int ccu_mult_set_parent(struct clk_hw *hw, u8 index)
{
	struct ccu_mult *cm = hw_to_ccu_mult(hw);

	return ccu_mux_helper_set_parent(&cm->common, &cm->mux, index);
}

const struct clk_ops ccu_mult_ops = {
	.disable	= ccu_mult_disable,
	.enable		= ccu_mult_enable,
	.is_enabled	= ccu_mult_is_enabled,

	.get_parent	= ccu_mult_get_parent,
	.set_parent	= ccu_mult_set_parent,

	.determine_rate	= ccu_mult_determine_rate,
	.recalc_rate	= ccu_mult_recalc_rate,
	.set_rate	= ccu_mult_set_rate,
};

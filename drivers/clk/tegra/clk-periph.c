// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>

#include "clk.h"

static u8 clk_periph_get_parent(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *mux_ops = periph->mux_ops;
	struct clk_hw *mux_hw = &periph->mux.hw;

	__clk_hw_set_clk(mux_hw, hw);

	return mux_ops->get_parent(mux_hw);
}

static int clk_periph_set_parent(struct clk_hw *hw, u8 index)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *mux_ops = periph->mux_ops;
	struct clk_hw *mux_hw = &periph->mux.hw;

	__clk_hw_set_clk(mux_hw, hw);

	return mux_ops->set_parent(mux_hw, index);
}

static unsigned long clk_periph_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *div_ops = periph->div_ops;
	struct clk_hw *div_hw = &periph->divider.hw;

	__clk_hw_set_clk(div_hw, hw);

	return div_ops->recalc_rate(div_hw, parent_rate);
}

static int clk_periph_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *div_ops = periph->div_ops;
	struct clk_hw *div_hw = &periph->divider.hw;
	unsigned long rate;

	__clk_hw_set_clk(div_hw, hw);

	rate = div_ops->round_rate(div_hw, req->rate, &req->best_parent_rate);
	if (rate < 0)
		return rate;

	req->rate = rate;
	return 0;
}

static int clk_periph_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *div_ops = periph->div_ops;
	struct clk_hw *div_hw = &periph->divider.hw;

	__clk_hw_set_clk(div_hw, hw);

	return div_ops->set_rate(div_hw, rate, parent_rate);
}

static int clk_periph_is_enabled(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *gate_ops = periph->gate_ops;
	struct clk_hw *gate_hw = &periph->gate.hw;

	__clk_hw_set_clk(gate_hw, hw);

	return gate_ops->is_enabled(gate_hw);
}

static int clk_periph_enable(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *gate_ops = periph->gate_ops;
	struct clk_hw *gate_hw = &periph->gate.hw;

	__clk_hw_set_clk(gate_hw, hw);

	return gate_ops->enable(gate_hw);
}

static void clk_periph_disable(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *gate_ops = periph->gate_ops;
	struct clk_hw *gate_hw = &periph->gate.hw;

	gate_ops->disable(gate_hw);
}

static void clk_periph_disable_unused(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *gate_ops = periph->gate_ops;
	struct clk_hw *gate_hw = &periph->gate.hw;

	gate_ops->disable_unused(gate_hw);
}

static void clk_periph_restore_context(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *div_ops = periph->div_ops;
	struct clk_hw *div_hw = &periph->divider.hw;
	int parent_id;

	parent_id = clk_hw_get_parent_index(hw);
	if (WARN_ON(parent_id < 0))
		return;

	if (!(periph->gate.flags & TEGRA_PERIPH_NO_DIV))
		div_ops->restore_context(div_hw);

	clk_periph_set_parent(hw, parent_id);
}

const struct clk_ops tegra_clk_periph_ops = {
	.get_parent = clk_periph_get_parent,
	.set_parent = clk_periph_set_parent,
	.recalc_rate = clk_periph_recalc_rate,
	.determine_rate = clk_periph_determine_rate,
	.set_rate = clk_periph_set_rate,
	.is_enabled = clk_periph_is_enabled,
	.enable = clk_periph_enable,
	.disable = clk_periph_disable,
	.disable_unused = clk_periph_disable_unused,
	.restore_context = clk_periph_restore_context,
};

static const struct clk_ops tegra_clk_periph_nodiv_ops = {
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.get_parent = clk_periph_get_parent,
	.set_parent = clk_periph_set_parent,
	.is_enabled = clk_periph_is_enabled,
	.enable = clk_periph_enable,
	.disable = clk_periph_disable,
	.disable_unused = clk_periph_disable_unused,
	.restore_context = clk_periph_restore_context,
};

static const struct clk_ops tegra_clk_periph_no_gate_ops = {
	.get_parent = clk_periph_get_parent,
	.set_parent = clk_periph_set_parent,
	.recalc_rate = clk_periph_recalc_rate,
	.determine_rate = clk_periph_determine_rate,
	.set_rate = clk_periph_set_rate,
	.restore_context = clk_periph_restore_context,
};

static struct clk *_tegra_clk_register_periph(const char *name,
			const char * const *parent_names, int num_parents,
			struct tegra_clk_periph *periph,
			void __iomem *clk_base, u32 offset,
			unsigned long flags)
{
	struct clk *clk;
	struct clk_init_data init;
	const struct tegra_clk_periph_regs *bank;
	bool div = !(periph->gate.flags & TEGRA_PERIPH_NO_DIV);

	if (periph->gate.flags & TEGRA_PERIPH_NO_DIV) {
		flags |= CLK_SET_RATE_PARENT;
		init.ops = &tegra_clk_periph_nodiv_ops;
	} else if (periph->gate.flags & TEGRA_PERIPH_NO_GATE)
		init.ops = &tegra_clk_periph_no_gate_ops;
	else
		init.ops = &tegra_clk_periph_ops;

	init.name = name;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	bank = get_reg_bank(periph->gate.clk_num);
	if (!bank)
		return ERR_PTR(-EINVAL);

	/* Data in .init is copied by clk_register(), so stack variable OK */
	periph->hw.init = &init;
	periph->magic = TEGRA_CLK_PERIPH_MAGIC;
	periph->mux.reg = clk_base + offset;
	periph->divider.reg = div ? (clk_base + offset) : NULL;
	periph->gate.clk_base = clk_base;
	periph->gate.regs = bank;
	periph->gate.enable_refcnt = periph_clk_enb_refcnt;

	clk = clk_register(NULL, &periph->hw);
	if (IS_ERR(clk))
		return clk;

	periph->mux.hw.clk = clk;
	periph->divider.hw.clk = div ? clk : NULL;
	periph->gate.hw.clk = clk;

	return clk;
}

struct clk *tegra_clk_register_periph(const char *name,
		const char * const *parent_names, int num_parents,
		struct tegra_clk_periph *periph, void __iomem *clk_base,
		u32 offset, unsigned long flags)
{
	return _tegra_clk_register_periph(name, parent_names, num_parents,
			periph, clk_base, offset, flags);
}

struct clk *tegra_clk_register_periph_nodiv(const char *name,
		const char * const *parent_names, int num_parents,
		struct tegra_clk_periph *periph, void __iomem *clk_base,
		u32 offset)
{
	periph->gate.flags |= TEGRA_PERIPH_NO_DIV;
	return _tegra_clk_register_periph(name, parent_names, num_parents,
			periph, clk_base, offset, CLK_SET_RATE_PARENT);
}

struct clk *tegra_clk_register_periph_data(void __iomem *clk_base,
					   struct tegra_periph_init_data *init)
{
	return _tegra_clk_register_periph(init->name, init->p.parent_names,
					  init->num_parents, &init->periph,
					  clk_base, init->offset, init->flags);
}

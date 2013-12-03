/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

	mux_hw->clk = hw->clk;

	return mux_ops->get_parent(mux_hw);
}

static int clk_periph_set_parent(struct clk_hw *hw, u8 index)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *mux_ops = periph->mux_ops;
	struct clk_hw *mux_hw = &periph->mux.hw;

	mux_hw->clk = hw->clk;

	return mux_ops->set_parent(mux_hw, index);
}

static unsigned long clk_periph_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *div_ops = periph->div_ops;
	struct clk_hw *div_hw = &periph->divider.hw;

	div_hw->clk = hw->clk;

	return div_ops->recalc_rate(div_hw, parent_rate);
}

static long clk_periph_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *div_ops = periph->div_ops;
	struct clk_hw *div_hw = &periph->divider.hw;

	div_hw->clk = hw->clk;

	return div_ops->round_rate(div_hw, rate, prate);
}

static int clk_periph_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *div_ops = periph->div_ops;
	struct clk_hw *div_hw = &periph->divider.hw;

	div_hw->clk = hw->clk;

	return div_ops->set_rate(div_hw, rate, parent_rate);
}

static int clk_periph_is_enabled(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *gate_ops = periph->gate_ops;
	struct clk_hw *gate_hw = &periph->gate.hw;

	gate_hw->clk = hw->clk;

	return gate_ops->is_enabled(gate_hw);
}

static int clk_periph_enable(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *gate_ops = periph->gate_ops;
	struct clk_hw *gate_hw = &periph->gate.hw;

	gate_hw->clk = hw->clk;

	return gate_ops->enable(gate_hw);
}

static void clk_periph_disable(struct clk_hw *hw)
{
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	const struct clk_ops *gate_ops = periph->gate_ops;
	struct clk_hw *gate_hw = &periph->gate.hw;

	gate_ops->disable(gate_hw);
}

void tegra_periph_reset_deassert(struct clk *c)
{
	struct clk_hw *hw = __clk_get_hw(c);
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	struct tegra_clk_periph_gate *gate;

	if (periph->magic != TEGRA_CLK_PERIPH_MAGIC) {
		gate = to_clk_periph_gate(hw);
		if (gate->magic != TEGRA_CLK_PERIPH_GATE_MAGIC) {
			WARN_ON(1);
			return;
		}
	} else {
		gate = &periph->gate;
	}

	tegra_periph_reset(gate, 0);
}
EXPORT_SYMBOL(tegra_periph_reset_deassert);

void tegra_periph_reset_assert(struct clk *c)
{
	struct clk_hw *hw = __clk_get_hw(c);
	struct tegra_clk_periph *periph = to_clk_periph(hw);
	struct tegra_clk_periph_gate *gate;

	if (periph->magic != TEGRA_CLK_PERIPH_MAGIC) {
		gate = to_clk_periph_gate(hw);
		if (gate->magic != TEGRA_CLK_PERIPH_GATE_MAGIC) {
			WARN_ON(1);
			return;
		}
	} else {
		gate = &periph->gate;
	}

	tegra_periph_reset(gate, 1);
}
EXPORT_SYMBOL(tegra_periph_reset_assert);

const struct clk_ops tegra_clk_periph_ops = {
	.get_parent = clk_periph_get_parent,
	.set_parent = clk_periph_set_parent,
	.recalc_rate = clk_periph_recalc_rate,
	.round_rate = clk_periph_round_rate,
	.set_rate = clk_periph_set_rate,
	.is_enabled = clk_periph_is_enabled,
	.enable = clk_periph_enable,
	.disable = clk_periph_disable,
};

const struct clk_ops tegra_clk_periph_nodiv_ops = {
	.get_parent = clk_periph_get_parent,
	.set_parent = clk_periph_set_parent,
	.is_enabled = clk_periph_is_enabled,
	.enable = clk_periph_enable,
	.disable = clk_periph_disable,
};

const struct clk_ops tegra_clk_periph_no_gate_ops = {
	.get_parent = clk_periph_get_parent,
	.set_parent = clk_periph_set_parent,
	.recalc_rate = clk_periph_recalc_rate,
	.round_rate = clk_periph_round_rate,
	.set_rate = clk_periph_set_rate,
};

static struct clk *_tegra_clk_register_periph(const char *name,
			const char **parent_names, int num_parents,
			struct tegra_clk_periph *periph,
			void __iomem *clk_base, u32 offset,
			unsigned long flags)
{
	struct clk *clk;
	struct clk_init_data init;
	struct tegra_clk_periph_regs *bank;
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
		const char **parent_names, int num_parents,
		struct tegra_clk_periph *periph, void __iomem *clk_base,
		u32 offset, unsigned long flags)
{
	return _tegra_clk_register_periph(name, parent_names, num_parents,
			periph, clk_base, offset, flags);
}

struct clk *tegra_clk_register_periph_nodiv(const char *name,
		const char **parent_names, int num_parents,
		struct tegra_clk_periph *periph, void __iomem *clk_base,
		u32 offset)
{
	periph->gate.flags |= TEGRA_PERIPH_NO_DIV;
	return _tegra_clk_register_periph(name, parent_names, num_parents,
			periph, clk_base, offset, CLK_SET_RATE_PARENT);
}

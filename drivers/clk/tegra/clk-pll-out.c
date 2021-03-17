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

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>

#include "clk.h"

#define pll_out_enb(p) (BIT(p->enb_bit_idx))
#define pll_out_rst(p) (BIT(p->rst_bit_idx))

static int clk_pll_out_is_enabled(struct clk_hw *hw)
{
	struct tegra_clk_pll_out *pll_out = to_clk_pll_out(hw);
	u32 val = readl_relaxed(pll_out->reg);
	int state;

	state = (val & pll_out_enb(pll_out)) ? 1 : 0;
	if (!(val & (pll_out_rst(pll_out))))
		state = 0;
	return state;
}

static int clk_pll_out_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll_out *pll_out = to_clk_pll_out(hw);
	unsigned long flags = 0;
	u32 val;

	if (pll_out->lock)
		spin_lock_irqsave(pll_out->lock, flags);

	val = readl_relaxed(pll_out->reg);

	val |= (pll_out_enb(pll_out) | pll_out_rst(pll_out));

	writel_relaxed(val, pll_out->reg);
	udelay(2);

	if (pll_out->lock)
		spin_unlock_irqrestore(pll_out->lock, flags);

	return 0;
}

static void clk_pll_out_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll_out *pll_out = to_clk_pll_out(hw);
	unsigned long flags = 0;
	u32 val;

	if (pll_out->lock)
		spin_lock_irqsave(pll_out->lock, flags);

	val = readl_relaxed(pll_out->reg);

	val &= ~(pll_out_enb(pll_out) | pll_out_rst(pll_out));

	writel_relaxed(val, pll_out->reg);
	udelay(2);

	if (pll_out->lock)
		spin_unlock_irqrestore(pll_out->lock, flags);
}

const struct clk_ops tegra_clk_pll_out_ops = {
	.is_enabled = clk_pll_out_is_enabled,
	.enable = clk_pll_out_enable,
	.disable = clk_pll_out_disable,
};

struct clk *tegra_clk_register_pll_out(const char *name,
		const char *parent_name, void __iomem *reg, u8 enb_bit_idx,
		u8 rst_bit_idx, unsigned long flags, u8 pll_out_flags,
		spinlock_t *lock)
{
	struct tegra_clk_pll_out *pll_out;
	struct clk *clk;
	struct clk_init_data init;

	pll_out = kzalloc(sizeof(*pll_out), GFP_KERNEL);
	if (!pll_out)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &tegra_clk_pll_out_ops;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	init.flags = flags;

	pll_out->reg = reg;
	pll_out->enb_bit_idx = enb_bit_idx;
	pll_out->rst_bit_idx = rst_bit_idx;
	pll_out->flags = pll_out_flags;
	pll_out->lock = lock;

	/* Data in .init is copied by clk_register(), so stack variable OK */
	pll_out->hw.init = &init;

	clk = clk_register(NULL, &pll_out->hw);
	if (IS_ERR(clk))
		kfree(pll_out);

	return clk;
}

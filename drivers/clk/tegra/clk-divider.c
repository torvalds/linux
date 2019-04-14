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
#include <linux/slab.h>
#include <linux/clk-provider.h>

#include "clk.h"

#define pll_out_override(p) (BIT((p->shift - 6)))
#define div_mask(d) ((1 << (d->width)) - 1)
#define get_mul(d) (1 << d->frac_width)
#define get_max_div(d) div_mask(d)

#define PERIPH_CLK_UART_DIV_ENB BIT(24)

static int get_div(struct tegra_clk_frac_div *divider, unsigned long rate,
		   unsigned long parent_rate)
{
	int div;

	div = div_frac_get(rate, parent_rate, divider->width,
			   divider->frac_width, divider->flags);

	if (div < 0)
		return 0;

	return div;
}

static unsigned long clk_frac_div_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct tegra_clk_frac_div *divider = to_clk_frac_div(hw);
	u32 reg;
	int div, mul;
	u64 rate = parent_rate;

	reg = readl_relaxed(divider->reg) >> divider->shift;
	div = reg & div_mask(divider);

	mul = get_mul(divider);
	div += mul;

	rate *= mul;
	rate += div - 1;
	do_div(rate, div);

	return rate;
}

static long clk_frac_div_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	struct tegra_clk_frac_div *divider = to_clk_frac_div(hw);
	int div, mul;
	unsigned long output_rate = *prate;

	if (!rate)
		return output_rate;

	div = get_div(divider, rate, output_rate);
	if (div < 0)
		return *prate;

	mul = get_mul(divider);

	return DIV_ROUND_UP(output_rate * mul, div + mul);
}

static int clk_frac_div_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct tegra_clk_frac_div *divider = to_clk_frac_div(hw);
	int div;
	unsigned long flags = 0;
	u32 val;

	div = get_div(divider, rate, parent_rate);
	if (div < 0)
		return div;

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);

	val = readl_relaxed(divider->reg);
	val &= ~(div_mask(divider) << divider->shift);
	val |= div << divider->shift;

	if (divider->flags & TEGRA_DIVIDER_UART) {
		if (div)
			val |= PERIPH_CLK_UART_DIV_ENB;
		else
			val &= ~PERIPH_CLK_UART_DIV_ENB;
	}

	if (divider->flags & TEGRA_DIVIDER_FIXED)
		val |= pll_out_override(divider);

	writel_relaxed(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);

	return 0;
}

const struct clk_ops tegra_clk_frac_div_ops = {
	.recalc_rate = clk_frac_div_recalc_rate,
	.set_rate = clk_frac_div_set_rate,
	.round_rate = clk_frac_div_round_rate,
};

struct clk *tegra_clk_register_divider(const char *name,
		const char *parent_name, void __iomem *reg,
		unsigned long flags, u8 clk_divider_flags, u8 shift, u8 width,
		u8 frac_width, spinlock_t *lock)
{
	struct tegra_clk_frac_div *divider;
	struct clk *clk;
	struct clk_init_data init;

	divider = kzalloc(sizeof(*divider), GFP_KERNEL);
	if (!divider) {
		pr_err("%s: could not allocate fractional divider clk\n",
		       __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &tegra_clk_frac_div_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	divider->reg = reg;
	divider->shift = shift;
	divider->width = width;
	divider->frac_width = frac_width;
	divider->lock = lock;
	divider->flags = clk_divider_flags;

	/* Data in .init is copied by clk_register(), so stack variable OK */
	divider->hw.init = &init;

	clk = clk_register(NULL, &divider->hw);
	if (IS_ERR(clk))
		kfree(divider);

	return clk;
}

static const struct clk_div_table mc_div_table[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 1 },
	{ .val = 0, .div = 0 },
};

struct clk *tegra_clk_register_mc(const char *name, const char *parent_name,
				  void __iomem *reg, spinlock_t *lock)
{
	return clk_register_divider_table(NULL, name, parent_name,
					  CLK_IS_CRITICAL,
					  reg, 16, 1, CLK_DIVIDER_READ_ONLY,
					  mc_div_table, lock);
}

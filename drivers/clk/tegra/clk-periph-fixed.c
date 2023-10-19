// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>

#include "clk.h"

static inline struct tegra_clk_periph_fixed *
to_tegra_clk_periph_fixed(struct clk_hw *hw)
{
	return container_of(hw, struct tegra_clk_periph_fixed, hw);
}

static int tegra_clk_periph_fixed_is_enabled(struct clk_hw *hw)
{
	struct tegra_clk_periph_fixed *fixed = to_tegra_clk_periph_fixed(hw);
	u32 mask = 1 << (fixed->num % 32), value;

	value = readl(fixed->base + fixed->regs->enb_reg);
	if (value & mask) {
		value = readl(fixed->base + fixed->regs->rst_reg);
		if ((value & mask) == 0)
			return 1;
	}

	return 0;
}

static int tegra_clk_periph_fixed_enable(struct clk_hw *hw)
{
	struct tegra_clk_periph_fixed *fixed = to_tegra_clk_periph_fixed(hw);
	u32 mask = 1 << (fixed->num % 32);

	writel(mask, fixed->base + fixed->regs->enb_set_reg);

	return 0;
}

static void tegra_clk_periph_fixed_disable(struct clk_hw *hw)
{
	struct tegra_clk_periph_fixed *fixed = to_tegra_clk_periph_fixed(hw);
	u32 mask = 1 << (fixed->num % 32);

	writel(mask, fixed->base + fixed->regs->enb_clr_reg);
}

static unsigned long
tegra_clk_periph_fixed_recalc_rate(struct clk_hw *hw,
				   unsigned long parent_rate)
{
	struct tegra_clk_periph_fixed *fixed = to_tegra_clk_periph_fixed(hw);
	unsigned long long rate;

	rate = (unsigned long long)parent_rate * fixed->mul;
	do_div(rate, fixed->div);

	return (unsigned long)rate;
}

static const struct clk_ops tegra_clk_periph_fixed_ops = {
	.is_enabled = tegra_clk_periph_fixed_is_enabled,
	.enable = tegra_clk_periph_fixed_enable,
	.disable = tegra_clk_periph_fixed_disable,
	.recalc_rate = tegra_clk_periph_fixed_recalc_rate,
};

struct clk *tegra_clk_register_periph_fixed(const char *name,
					    const char *parent,
					    unsigned long flags,
					    void __iomem *base,
					    unsigned int mul,
					    unsigned int div,
					    unsigned int num)
{
	const struct tegra_clk_periph_regs *regs;
	struct tegra_clk_periph_fixed *fixed;
	struct clk_init_data init;
	struct clk *clk;

	regs = get_reg_bank(num);
	if (!regs)
		return ERR_PTR(-EINVAL);

	fixed = kzalloc(sizeof(*fixed), GFP_KERNEL);
	if (!fixed)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = flags;
	init.parent_names = parent ? &parent : NULL;
	init.num_parents = parent ? 1 : 0;
	init.ops = &tegra_clk_periph_fixed_ops;

	fixed->base = base;
	fixed->regs = regs;
	fixed->mul = mul;
	fixed->div = div;
	fixed->num = num;

	fixed->hw.init = &init;

	clk = clk_register(NULL, &fixed->hw);
	if (IS_ERR(clk))
		kfree(fixed);

	return clk;
}

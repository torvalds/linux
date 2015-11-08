/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * based on
 *
 * samsung/clk.c
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reboot.h>
#include "clk.h"

/**
 * Register a clock branch.
 * Most clock branches have a form like
 *
 * src1 --|--\
 *        |M |--[GATE]-[DIV]-
 * src2 --|--/
 *
 * sometimes without one of those components.
 */
static struct clk *rockchip_clk_register_branch(const char *name,
		const char *const *parent_names, u8 num_parents, void __iomem *base,
		int muxdiv_offset, u8 mux_shift, u8 mux_width, u8 mux_flags,
		u8 div_shift, u8 div_width, u8 div_flags,
		struct clk_div_table *div_table, int gate_offset,
		u8 gate_shift, u8 gate_flags, unsigned long flags,
		spinlock_t *lock)
{
	struct clk *clk;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;
	const struct clk_ops *mux_ops = NULL, *div_ops = NULL,
			     *gate_ops = NULL;

	if (num_parents > 1) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->reg = base + muxdiv_offset;
		mux->shift = mux_shift;
		mux->mask = BIT(mux_width) - 1;
		mux->flags = mux_flags;
		mux->lock = lock;
		mux_ops = (mux_flags & CLK_MUX_READ_ONLY) ? &clk_mux_ro_ops
							: &clk_mux_ops;
	}

	if (gate_offset >= 0) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate)
			return ERR_PTR(-ENOMEM);

		gate->flags = gate_flags;
		gate->reg = base + gate_offset;
		gate->bit_idx = gate_shift;
		gate->lock = lock;
		gate_ops = &clk_gate_ops;
	}

	if (div_width > 0) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div)
			return ERR_PTR(-ENOMEM);

		div->flags = div_flags;
		div->reg = base + muxdiv_offset;
		div->shift = div_shift;
		div->width = div_width;
		div->lock = lock;
		div->table = div_table;
		div_ops = &clk_divider_ops;
	}

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
				     mux ? &mux->hw : NULL, mux_ops,
				     div ? &div->hw : NULL, div_ops,
				     gate ? &gate->hw : NULL, gate_ops,
				     flags);

	return clk;
}

static struct clk *rockchip_clk_register_frac_branch(const char *name,
		const char *const *parent_names, u8 num_parents,
		void __iomem *base, int muxdiv_offset, u8 div_flags,
		int gate_offset, u8 gate_shift, u8 gate_flags,
		unsigned long flags, spinlock_t *lock)
{
	struct clk *clk;
	struct clk_gate *gate = NULL;
	struct clk_fractional_divider *div = NULL;
	const struct clk_ops *div_ops = NULL, *gate_ops = NULL;

	if (gate_offset >= 0) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate)
			return ERR_PTR(-ENOMEM);

		gate->flags = gate_flags;
		gate->reg = base + gate_offset;
		gate->bit_idx = gate_shift;
		gate->lock = lock;
		gate_ops = &clk_gate_ops;
	}

	if (muxdiv_offset < 0)
		return ERR_PTR(-EINVAL);

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	div->flags = div_flags;
	div->reg = base + muxdiv_offset;
	div->mshift = 16;
	div->mwidth = 16;
	div->mmask = GENMASK(div->mwidth - 1, 0) << div->mshift;
	div->nshift = 0;
	div->nwidth = 16;
	div->nmask = GENMASK(div->nwidth - 1, 0) << div->nshift;
	div->lock = lock;
	div_ops = &clk_fractional_divider_ops;

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
				     NULL, NULL,
				     &div->hw, div_ops,
				     gate ? &gate->hw : NULL, gate_ops,
				     flags);

	return clk;
}

static DEFINE_SPINLOCK(clk_lock);
static struct clk **clk_table;
static void __iomem *reg_base;
static struct clk_onecell_data clk_data;
static struct device_node *cru_node;
static struct regmap *grf;

void __init rockchip_clk_init(struct device_node *np, void __iomem *base,
			      unsigned long nr_clks)
{
	reg_base = base;
	cru_node = np;
	grf = ERR_PTR(-EPROBE_DEFER);

	clk_table = kcalloc(nr_clks, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_table)
		pr_err("%s: could not allocate clock lookup table\n", __func__);

	clk_data.clks = clk_table;
	clk_data.clk_num = nr_clks;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

struct regmap *rockchip_clk_get_grf(void)
{
	if (IS_ERR(grf))
		grf = syscon_regmap_lookup_by_phandle(cru_node, "rockchip,grf");
	return grf;
}

void rockchip_clk_add_lookup(struct clk *clk, unsigned int id)
{
	if (clk_table && id)
		clk_table[id] = clk;
}

void __init rockchip_clk_register_plls(struct rockchip_pll_clock *list,
				unsigned int nr_pll, int grf_lock_offset)
{
	struct clk *clk;
	int idx;

	for (idx = 0; idx < nr_pll; idx++, list++) {
		clk = rockchip_clk_register_pll(list->type, list->name,
				list->parent_names, list->num_parents,
				reg_base, list->con_offset, grf_lock_offset,
				list->lock_shift, list->mode_offset,
				list->mode_shift, list->rate_table,
				list->pll_flags, &clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		rockchip_clk_add_lookup(clk, list->id);
	}
}

void __init rockchip_clk_register_branches(
				      struct rockchip_clk_branch *list,
				      unsigned int nr_clk)
{
	struct clk *clk = NULL;
	unsigned int idx;
	unsigned long flags;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		flags = list->flags;

		/* catch simple muxes */
		switch (list->branch_type) {
		case branch_mux:
			clk = clk_register_mux(NULL, list->name,
				list->parent_names, list->num_parents,
				flags, reg_base + list->muxdiv_offset,
				list->mux_shift, list->mux_width,
				list->mux_flags, &clk_lock);
			break;
		case branch_divider:
			if (list->div_table)
				clk = clk_register_divider_table(NULL,
					list->name, list->parent_names[0],
					flags, reg_base + list->muxdiv_offset,
					list->div_shift, list->div_width,
					list->div_flags, list->div_table,
					&clk_lock);
			else
				clk = clk_register_divider(NULL, list->name,
					list->parent_names[0], flags,
					reg_base + list->muxdiv_offset,
					list->div_shift, list->div_width,
					list->div_flags, &clk_lock);
			break;
		case branch_fraction_divider:
			clk = rockchip_clk_register_frac_branch(list->name,
				list->parent_names, list->num_parents,
				reg_base, list->muxdiv_offset, list->div_flags,
				list->gate_offset, list->gate_shift,
				list->gate_flags, flags, &clk_lock);
			break;
		case branch_gate:
			flags |= CLK_SET_RATE_PARENT;

			clk = clk_register_gate(NULL, list->name,
				list->parent_names[0], flags,
				reg_base + list->gate_offset,
				list->gate_shift, list->gate_flags, &clk_lock);
			break;
		case branch_composite:
			clk = rockchip_clk_register_branch(list->name,
				list->parent_names, list->num_parents,
				reg_base, list->muxdiv_offset, list->mux_shift,
				list->mux_width, list->mux_flags,
				list->div_shift, list->div_width,
				list->div_flags, list->div_table,
				list->gate_offset, list->gate_shift,
				list->gate_flags, flags, &clk_lock);
			break;
		case branch_mmc:
			clk = rockchip_clk_register_mmc(
				list->name,
				list->parent_names, list->num_parents,
				reg_base + list->muxdiv_offset,
				list->div_shift
			);
			break;
		case branch_inverter:
			clk = rockchip_clk_register_inverter(
				list->name, list->parent_names,
				list->num_parents,
				reg_base + list->muxdiv_offset,
				list->div_shift, list->div_flags, &clk_lock);
			break;
		}

		/* none of the cases above matched */
		if (!clk) {
			pr_err("%s: unknown clock type %d\n",
			       __func__, list->branch_type);
			continue;
		}

		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s: %ld\n",
			       __func__, list->name, PTR_ERR(clk));
			continue;
		}

		rockchip_clk_add_lookup(clk, list->id);
	}
}

void __init rockchip_clk_register_armclk(unsigned int lookup_id,
			const char *name, const char *const *parent_names,
			u8 num_parents,
			const struct rockchip_cpuclk_reg_data *reg_data,
			const struct rockchip_cpuclk_rate_table *rates,
			int nrates)
{
	struct clk *clk;

	clk = rockchip_clk_register_cpuclk(name, parent_names, num_parents,
					   reg_data, rates, nrates, reg_base,
					   &clk_lock);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register clock %s: %ld\n",
		       __func__, name, PTR_ERR(clk));
		return;
	}

	rockchip_clk_add_lookup(clk, lookup_id);
}

void __init rockchip_clk_protect_critical(const char *const clocks[],
					  int nclocks)
{
	int i;

	/* Protect the clocks that needs to stay on */
	for (i = 0; i < nclocks; i++) {
		struct clk *clk = __clk_lookup(clocks[i]);

		if (clk)
			clk_prepare_enable(clk);
	}
}

static unsigned int reg_restart;
static int rockchip_restart_notify(struct notifier_block *this,
				   unsigned long mode, void *cmd)
{
	writel(0xfdb9, reg_base + reg_restart);
	return NOTIFY_DONE;
}

static struct notifier_block rockchip_restart_handler = {
	.notifier_call = rockchip_restart_notify,
	.priority = 128,
};

void __init rockchip_register_restart_notifier(unsigned int reg)
{
	int ret;

	reg_restart = reg;
	ret = register_restart_handler(&rockchip_restart_handler);
	if (ret)
		pr_err("%s: cannot register restart handler, %d\n",
		       __func__, ret);
}

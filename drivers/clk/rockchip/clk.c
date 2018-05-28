/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
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
#include <linux/rational.h>
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
			goto err_gate;

		gate->flags = gate_flags;
		gate->reg = base + gate_offset;
		gate->bit_idx = gate_shift;
		gate->lock = lock;
		gate_ops = &clk_gate_ops;
	}

	if (div_width > 0) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div)
			goto err_div;

		div->flags = div_flags;
		div->reg = base + muxdiv_offset;
		div->shift = div_shift;
		div->width = div_width;
		div->lock = lock;
		div->table = div_table;
		div_ops = (div_flags & CLK_DIVIDER_READ_ONLY)
						? &clk_divider_ro_ops
						: &clk_divider_ops;
	}

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
				     mux ? &mux->hw : NULL, mux_ops,
				     div ? &div->hw : NULL, div_ops,
				     gate ? &gate->hw : NULL, gate_ops,
				     flags);

	return clk;
err_div:
	kfree(gate);
err_gate:
	kfree(mux);
	return ERR_PTR(-ENOMEM);
}

struct rockchip_clk_frac {
	struct notifier_block			clk_nb;
	struct clk_fractional_divider		div;
	struct clk_gate				gate;

	struct clk_mux				mux;
	const struct clk_ops			*mux_ops;
	int					mux_frac_idx;

	int					rate_change_idx;
};

#define to_rockchip_clk_frac_nb(nb) \
			container_of(nb, struct rockchip_clk_frac, clk_nb)

static int rockchip_clk_frac_notifier_cb(struct notifier_block *nb,
					 unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct rockchip_clk_frac *frac = to_rockchip_clk_frac_nb(nb);
	struct clk_mux *frac_mux = &frac->mux;
	int ret = 0;

	pr_debug("%s: event %lu, old_rate %lu, new_rate: %lu\n",
		 __func__, event, ndata->old_rate, ndata->new_rate);
	if (event == PRE_RATE_CHANGE) {
		frac->rate_change_idx = frac->mux_ops->get_parent(&frac_mux->hw);
		if (frac->rate_change_idx != frac->mux_frac_idx) {
			frac->mux_ops->set_parent(&frac_mux->hw, frac->mux_frac_idx);
		}
	}

	return notifier_from_errno(ret);
}

/**
 * fractional divider must set that denominator is 20 times larger than
 * numerator to generate precise clock frequency.
 */
static void rockchip_fractional_approximation(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate,
		unsigned long *m, unsigned long *n)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);
	unsigned long p_rate, p_parent_rate;
	struct clk_hw *p_parent;
	unsigned long scale;
	u32 div;

	p_rate = clk_hw_get_rate(clk_hw_get_parent(hw));
	if (((rate * 20 > p_rate) && (p_rate % rate != 0)) ||
	    (fd->max_prate && fd->max_prate < p_rate)) {
		p_parent = clk_hw_get_parent(clk_hw_get_parent(hw));
		if (!p_parent) {
			*parent_rate = p_rate;
		} else {
			p_parent_rate = clk_hw_get_rate(p_parent);
			*parent_rate = p_parent_rate;
			if (fd->max_prate && p_parent_rate > fd->max_prate) {
				div = DIV_ROUND_UP(p_parent_rate,
						   fd->max_prate);
				*parent_rate = p_parent_rate / div;
			}
		}

		if (*parent_rate < rate * 20) {
			pr_err("%s parent_rate(%ld) is low than rate(%ld)*20, fractional div is not allowed\n",
			       clk_hw_get_name(hw), *parent_rate, rate);
			*m = 0;
			*n = 1;
			return;
		}
	}

	/*
	 * Get rate closer to *parent_rate to guarantee there is no overflow
	 * for m and n. In the result it will be the nearest rate left shifted
	 * by (scale - fd->nwidth) bits.
	 */
	scale = fls_long(*parent_rate / rate - 1);
	if (scale > fd->nwidth)
		rate <<= scale - fd->nwidth;

	rational_best_approximation(rate, *parent_rate,
				    GENMASK(fd->mwidth - 1, 0),
				    GENMASK(fd->nwidth - 1, 0),
				    m, n);
}

static struct clk *rockchip_clk_register_frac_branch(
		struct rockchip_clk_provider *ctx, const char *name,
		const char *const *parent_names, u8 num_parents,
		void __iomem *base, int muxdiv_offset, u8 div_flags,
		int gate_offset, u8 gate_shift, u8 gate_flags,
		unsigned long flags, struct rockchip_clk_branch *child,
		unsigned long max_prate, spinlock_t *lock)
{
	struct rockchip_clk_frac *frac;
	struct clk *clk;
	struct clk_gate *gate = NULL;
	struct clk_fractional_divider *div = NULL;
	const struct clk_ops *div_ops = NULL, *gate_ops = NULL;

	if (muxdiv_offset < 0)
		return ERR_PTR(-EINVAL);

	if (child && child->branch_type != branch_mux) {
		pr_err("%s: fractional child clock for %s can only be a mux\n",
		       __func__, name);
		return ERR_PTR(-EINVAL);
	}

	frac = kzalloc(sizeof(*frac), GFP_KERNEL);
	if (!frac)
		return ERR_PTR(-ENOMEM);

	if (gate_offset >= 0) {
		gate = &frac->gate;
		gate->flags = gate_flags;
		gate->reg = base + gate_offset;
		gate->bit_idx = gate_shift;
		gate->lock = lock;
		gate_ops = &clk_gate_ops;
	}

	div = &frac->div;
	div->flags = div_flags;
	div->reg = base + muxdiv_offset;
	div->mshift = 16;
	div->mwidth = 16;
	div->mmask = GENMASK(div->mwidth - 1, 0) << div->mshift;
	div->nshift = 0;
	div->nwidth = 16;
	div->nmask = GENMASK(div->nwidth - 1, 0) << div->nshift;
	div->lock = lock;
	div->approximation = rockchip_fractional_approximation;
	div->max_prate = max_prate;
	div_ops = &clk_fractional_divider_ops;

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
				     NULL, NULL,
				     &div->hw, div_ops,
				     gate ? &gate->hw : NULL, gate_ops,
				     flags | CLK_SET_RATE_UNGATE);
	if (IS_ERR(clk)) {
		kfree(frac);
		return clk;
	}

	if (child) {
		struct clk_mux *frac_mux = &frac->mux;
		struct clk_init_data init;
		struct clk *mux_clk;
		int i, ret;

		frac->mux_frac_idx = -1;
		for (i = 0; i < child->num_parents; i++) {
			if (!strcmp(name, child->parent_names[i])) {
				pr_debug("%s: found fractional parent in mux at pos %d\n",
					 __func__, i);
				frac->mux_frac_idx = i;
				break;
			}
		}

		frac->mux_ops = &clk_mux_ops;
		frac->clk_nb.notifier_call = rockchip_clk_frac_notifier_cb;

		frac_mux->reg = base + child->muxdiv_offset;
		frac_mux->shift = child->mux_shift;
		frac_mux->mask = BIT(child->mux_width) - 1;
		frac_mux->flags = child->mux_flags;
		frac_mux->lock = lock;
		frac_mux->hw.init = &init;

		init.name = child->name;
		init.flags = child->flags | CLK_SET_RATE_PARENT;
		init.ops = frac->mux_ops;
		init.parent_names = child->parent_names;
		init.num_parents = child->num_parents;

		mux_clk = clk_register(NULL, &frac_mux->hw);
		if (IS_ERR(mux_clk))
			return clk;

		rockchip_clk_add_lookup(ctx, mux_clk, child->id);

		/* notifier on the fraction divider to catch rate changes */
		if (frac->mux_frac_idx >= 0) {
			ret = clk_notifier_register(clk, &frac->clk_nb);
			if (ret)
				pr_err("%s: failed to register clock notifier for %s\n",
						__func__, name);
		} else {
			pr_warn("%s: could not find %s as parent of %s, rate changes may not work\n",
				__func__, name, child->name);
		}
	}

	return clk;
}

static struct clk *rockchip_clk_register_factor_branch(const char *name,
		const char *const *parent_names, u8 num_parents,
		void __iomem *base, unsigned int mult, unsigned int div,
		int gate_offset, u8 gate_shift, u8 gate_flags,
		unsigned long flags, spinlock_t *lock)
{
	struct clk *clk;
	struct clk_gate *gate = NULL;
	struct clk_fixed_factor *fix = NULL;

	/* without gate, register a simple factor clock */
	if (gate_offset == 0) {
		return clk_register_fixed_factor(NULL, name,
				parent_names[0], flags, mult,
				div);
	}

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->flags = gate_flags;
	gate->reg = base + gate_offset;
	gate->bit_idx = gate_shift;
	gate->lock = lock;

	fix = kzalloc(sizeof(*fix), GFP_KERNEL);
	if (!fix) {
		kfree(gate);
		return ERR_PTR(-ENOMEM);
	}

	fix->mult = mult;
	fix->div = div;

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
				     NULL, NULL,
				     &fix->hw, &clk_fixed_factor_ops,
				     &gate->hw, &clk_gate_ops, flags);
	if (IS_ERR(clk)) {
		kfree(fix);
		kfree(gate);
	}

	return clk;
}

struct rockchip_clk_provider * __init rockchip_clk_init(struct device_node *np,
			void __iomem *base, unsigned long nr_clks)
{
	struct rockchip_clk_provider *ctx;
	struct clk **clk_table;
	int i;

	ctx = kzalloc(sizeof(struct rockchip_clk_provider), GFP_KERNEL);
	if (!ctx) {
		pr_err("%s: Could not allocate clock provider context\n",
			__func__);
		return ERR_PTR(-ENOMEM);
	}

	clk_table = kcalloc(nr_clks, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_table) {
		pr_err("%s: Could not allocate clock lookup table\n",
			__func__);
		goto err_free;
	}

	for (i = 0; i < nr_clks; ++i)
		clk_table[i] = ERR_PTR(-ENOENT);

	ctx->reg_base = base;
	ctx->clk_data.clks = clk_table;
	ctx->clk_data.clk_num = nr_clks;
	ctx->cru_node = np;
	ctx->grf = ERR_PTR(-EPROBE_DEFER);
	spin_lock_init(&ctx->lock);
	ctx->grf = syscon_regmap_lookup_by_phandle(ctx->cru_node,
						   "rockchip,grf");
	ctx->boost = syscon_regmap_lookup_by_phandle(ctx->cru_node,
						   "rockchip,boost");

	return ctx;

err_free:
	kfree(ctx);
	return ERR_PTR(-ENOMEM);
}

void __init rockchip_clk_of_add_provider(struct device_node *np,
				struct rockchip_clk_provider *ctx)
{
	if (of_clk_add_provider(np, of_clk_src_onecell_get,
				&ctx->clk_data))
		pr_err("%s: could not register clk provider\n", __func__);
}

struct regmap *rockchip_clk_get_grf(struct rockchip_clk_provider *ctx)
{
	if (IS_ERR(ctx->grf))
		ctx->grf = syscon_regmap_lookup_by_phandle(ctx->cru_node, "rockchip,grf");
	return ctx->grf;
}

void rockchip_clk_add_lookup(struct rockchip_clk_provider *ctx,
			     struct clk *clk, unsigned int id)
{
	if (ctx->clk_data.clks && id)
		ctx->clk_data.clks[id] = clk;
}

void __init rockchip_clk_register_plls(struct rockchip_clk_provider *ctx,
				struct rockchip_pll_clock *list,
				unsigned int nr_pll, int grf_lock_offset)
{
	struct clk *clk;
	int idx;

	for (idx = 0; idx < nr_pll; idx++, list++) {
		clk = rockchip_clk_register_pll(ctx, list->type, list->name,
				list->parent_names, list->num_parents,
				list->con_offset, grf_lock_offset,
				list->lock_shift, list->mode_offset,
				list->mode_shift, list->rate_table,
				list->flags, list->pll_flags,
				list->boost_enabled);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		rockchip_clk_add_lookup(ctx, clk, list->id);
	}
}

void __init rockchip_clk_register_branches(
				      struct rockchip_clk_provider *ctx,
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
				flags, ctx->reg_base + list->muxdiv_offset,
				list->mux_shift, list->mux_width,
				list->mux_flags, &ctx->lock);
			break;
		case branch_muxgrf:
			clk = rockchip_clk_register_muxgrf(list->name,
				list->parent_names, list->num_parents,
				flags, ctx->grf, list->muxdiv_offset,
				list->mux_shift, list->mux_width,
				list->mux_flags);
			break;
		case branch_divider:
			if (list->div_table)
				clk = clk_register_divider_table(NULL,
					list->name, list->parent_names[0],
					flags, ctx->reg_base + list->muxdiv_offset,
					list->div_shift, list->div_width,
					list->div_flags, list->div_table,
					&ctx->lock);
			else
				clk = clk_register_divider(NULL, list->name,
					list->parent_names[0], flags,
					ctx->reg_base + list->muxdiv_offset,
					list->div_shift, list->div_width,
					list->div_flags, &ctx->lock);
			break;
		case branch_fraction_divider:
			clk = rockchip_clk_register_frac_branch(ctx, list->name,
				list->parent_names, list->num_parents,
				ctx->reg_base, list->muxdiv_offset, list->div_flags,
				list->gate_offset, list->gate_shift,
				list->gate_flags, flags, list->child,
				list->max_prate, &ctx->lock);
			break;
		case branch_gate:
			flags |= CLK_SET_RATE_PARENT;

			clk = clk_register_gate(NULL, list->name,
				list->parent_names[0], flags,
				ctx->reg_base + list->gate_offset,
				list->gate_shift, list->gate_flags, &ctx->lock);
			break;
		case branch_composite:
			clk = rockchip_clk_register_branch(list->name,
				list->parent_names, list->num_parents,
				ctx->reg_base, list->muxdiv_offset, list->mux_shift,
				list->mux_width, list->mux_flags,
				list->div_shift, list->div_width,
				list->div_flags, list->div_table,
				list->gate_offset, list->gate_shift,
				list->gate_flags, flags, &ctx->lock);
			break;
		case branch_mmc:
			clk = rockchip_clk_register_mmc(
				list->name,
				list->parent_names, list->num_parents,
				ctx->reg_base + list->muxdiv_offset,
				list->div_shift
			);
			break;
		case branch_inverter:
			clk = rockchip_clk_register_inverter(
				list->name, list->parent_names,
				list->num_parents,
				ctx->reg_base + list->muxdiv_offset,
				list->div_shift, list->div_flags, &ctx->lock);
			break;
		case branch_factor:
			clk = rockchip_clk_register_factor_branch(
				list->name, list->parent_names,
				list->num_parents, ctx->reg_base,
				list->div_shift, list->div_width,
				list->gate_offset, list->gate_shift,
				list->gate_flags, flags, &ctx->lock);
			break;
		case branch_ddrc:
			clk = rockchip_clk_register_ddrclk(
				list->name, list->flags,
				list->parent_names, list->num_parents,
				list->muxdiv_offset, list->mux_shift,
				list->mux_width, list->div_shift,
				list->div_width, list->div_flags,
				ctx->reg_base);
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

		rockchip_clk_add_lookup(ctx, clk, list->id);
	}
}

void __init rockchip_clk_register_armclk(struct rockchip_clk_provider *ctx,
			unsigned int lookup_id,
			const char *name, const char *const *parent_names,
			u8 num_parents,
			const struct rockchip_cpuclk_reg_data *reg_data,
			const struct rockchip_cpuclk_rate_table *rates,
			int nrates)
{
	struct clk *clk;

	clk = rockchip_clk_register_cpuclk(name, parent_names, num_parents,
					   reg_data, rates, nrates, ctx->reg_base,
					   &ctx->lock);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register clock %s: %ld\n",
		       __func__, name, PTR_ERR(clk));
		return;
	}

	rockchip_clk_add_lookup(ctx, clk, lookup_id);
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

static void __iomem *rst_base;
static unsigned int reg_restart;
static void (*cb_restart)(void);
static int rockchip_restart_notify(struct notifier_block *this,
				   unsigned long mode, void *cmd)
{
	if (cb_restart)
		cb_restart();

	writel(0xfdb9, rst_base + reg_restart);
	return NOTIFY_DONE;
}

static struct notifier_block rockchip_restart_handler = {
	.notifier_call = rockchip_restart_notify,
	.priority = 128,
};

void __init rockchip_register_restart_notifier(struct rockchip_clk_provider *ctx,
					       unsigned int reg, void (*cb)(void))
{
	int ret;

	rst_base = ctx->reg_base;
	reg_restart = reg;
	cb_restart = cb;
	ret = register_restart_handler(&rockchip_restart_handler);
	if (ret)
		pr_err("%s: cannot register restart handler, %d\n",
		       __func__, ret);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>

#include "clk.h"

#define SUPER_STATE_IDLE 0
#define SUPER_STATE_RUN 1
#define SUPER_STATE_IRQ 2
#define SUPER_STATE_FIQ 3

#define SUPER_STATE_SHIFT 28
#define SUPER_STATE_MASK ((BIT(SUPER_STATE_IDLE) | BIT(SUPER_STATE_RUN) | \
			   BIT(SUPER_STATE_IRQ) | BIT(SUPER_STATE_FIQ))	\
			  << SUPER_STATE_SHIFT)

#define SUPER_LP_DIV2_BYPASS (1 << 16)

#define super_state(s) (BIT(s) << SUPER_STATE_SHIFT)
#define super_state_to_src_shift(m, s) ((m->width * s))
#define super_state_to_src_mask(m) (((1 << m->width) - 1))

static u8 clk_super_get_parent(struct clk_hw *hw)
{
	struct tegra_clk_super_mux *mux = to_clk_super_mux(hw);
	u32 val, state;
	u8 source, shift;

	val = readl_relaxed(mux->reg);

	state = val & SUPER_STATE_MASK;

	BUG_ON((state != super_state(SUPER_STATE_RUN)) &&
	       (state != super_state(SUPER_STATE_IDLE)));
	shift = (state == super_state(SUPER_STATE_IDLE)) ?
		super_state_to_src_shift(mux, SUPER_STATE_IDLE) :
		super_state_to_src_shift(mux, SUPER_STATE_RUN);

	source = (val >> shift) & super_state_to_src_mask(mux);

	/*
	 * If LP_DIV2_BYPASS is not set and PLLX is current parent then
	 * PLLX/2 is the input source to CCLKLP.
	 */
	if ((mux->flags & TEGRA_DIVIDER_2) && !(val & SUPER_LP_DIV2_BYPASS) &&
	    (source == mux->pllx_index))
		source = mux->div2_index;

	return source;
}

static int clk_super_set_parent(struct clk_hw *hw, u8 index)
{
	struct tegra_clk_super_mux *mux = to_clk_super_mux(hw);
	u32 val, state;
	int err = 0;
	u8 parent_index, shift;
	unsigned long flags = 0;

	if (mux->lock)
		spin_lock_irqsave(mux->lock, flags);

	val = readl_relaxed(mux->reg);
	state = val & SUPER_STATE_MASK;
	BUG_ON((state != super_state(SUPER_STATE_RUN)) &&
	       (state != super_state(SUPER_STATE_IDLE)));
	shift = (state == super_state(SUPER_STATE_IDLE)) ?
		super_state_to_src_shift(mux, SUPER_STATE_IDLE) :
		super_state_to_src_shift(mux, SUPER_STATE_RUN);

	/*
	 * For LP mode super-clock switch between PLLX direct
	 * and divided-by-2 outputs is allowed only when other
	 * than PLLX clock source is current parent.
	 */
	if ((mux->flags & TEGRA_DIVIDER_2) && ((index == mux->div2_index) ||
					       (index == mux->pllx_index))) {
		parent_index = clk_super_get_parent(hw);
		if ((parent_index == mux->div2_index) ||
		    (parent_index == mux->pllx_index)) {
			err = -EINVAL;
			goto out;
		}

		val ^= SUPER_LP_DIV2_BYPASS;
		writel_relaxed(val, mux->reg);
		udelay(2);

		if (index == mux->div2_index)
			index = mux->pllx_index;
	}
	val &= ~((super_state_to_src_mask(mux)) << shift);
	val |= (index & (super_state_to_src_mask(mux))) << shift;

	writel_relaxed(val, mux->reg);
	udelay(2);

out:
	if (mux->lock)
		spin_unlock_irqrestore(mux->lock, flags);

	return err;
}

static const struct clk_ops tegra_clk_super_mux_ops = {
	.get_parent = clk_super_get_parent,
	.set_parent = clk_super_set_parent,
};

static long clk_super_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	struct tegra_clk_super_mux *super = to_clk_super_mux(hw);
	struct clk_hw *div_hw = &super->frac_div.hw;

	__clk_hw_set_clk(div_hw, hw);

	return super->div_ops->round_rate(div_hw, rate, parent_rate);
}

static unsigned long clk_super_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct tegra_clk_super_mux *super = to_clk_super_mux(hw);
	struct clk_hw *div_hw = &super->frac_div.hw;

	__clk_hw_set_clk(div_hw, hw);

	return super->div_ops->recalc_rate(div_hw, parent_rate);
}

static int clk_super_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct tegra_clk_super_mux *super = to_clk_super_mux(hw);
	struct clk_hw *div_hw = &super->frac_div.hw;

	__clk_hw_set_clk(div_hw, hw);

	return super->div_ops->set_rate(div_hw, rate, parent_rate);
}

const struct clk_ops tegra_clk_super_ops = {
	.get_parent = clk_super_get_parent,
	.set_parent = clk_super_set_parent,
	.set_rate = clk_super_set_rate,
	.round_rate = clk_super_round_rate,
	.recalc_rate = clk_super_recalc_rate,
};

struct clk *tegra_clk_register_super_mux(const char *name,
		const char **parent_names, u8 num_parents,
		unsigned long flags, void __iomem *reg, u8 clk_super_flags,
		u8 width, u8 pllx_index, u8 div2_index, spinlock_t *lock)
{
	struct tegra_clk_super_mux *super;
	struct clk *clk;
	struct clk_init_data init;

	super = kzalloc(sizeof(*super), GFP_KERNEL);
	if (!super)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &tegra_clk_super_mux_ops;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	super->reg = reg;
	super->pllx_index = pllx_index;
	super->div2_index = div2_index;
	super->lock = lock;
	super->width = width;
	super->flags = clk_super_flags;

	/* Data in .init is copied by clk_register(), so stack variable OK */
	super->hw.init = &init;

	clk = clk_register(NULL, &super->hw);
	if (IS_ERR(clk))
		kfree(super);

	return clk;
}

struct clk *tegra_clk_register_super_clk(const char *name,
		const char * const *parent_names, u8 num_parents,
		unsigned long flags, void __iomem *reg, u8 clk_super_flags,
		spinlock_t *lock)
{
	struct tegra_clk_super_mux *super;
	struct clk *clk;
	struct clk_init_data init;

	super = kzalloc(sizeof(*super), GFP_KERNEL);
	if (!super)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &tegra_clk_super_ops;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	super->reg = reg;
	super->lock = lock;
	super->width = 4;
	super->flags = clk_super_flags;
	super->frac_div.reg = reg + 4;
	super->frac_div.shift = 16;
	super->frac_div.width = 8;
	super->frac_div.frac_width = 1;
	super->frac_div.lock = lock;
	super->div_ops = &tegra_clk_frac_div_ops;

	/* Data in .init is copied by clk_register(), so stack variable OK */
	super->hw.init = &init;

	clk = clk_register(NULL, &super->hw);
	if (IS_ERR(clk))
		kfree(super);

	return clk;
}

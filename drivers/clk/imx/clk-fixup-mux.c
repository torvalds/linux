// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "clk.h"

/**
 * struct clk_fixup_mux - imx integer fixup multiplexer clock
 * @mux: the parent class
 * @ops: pointer to clk_ops of parent class
 * @fixup: a hook to fixup the write value
 *
 * The imx fixup multiplexer clock is a subclass of basic clk_mux
 * with an addtional fixup hook.
 */
struct clk_fixup_mux {
	struct clk_mux mux;
	const struct clk_ops *ops;
	void (*fixup)(u32 *val);
};

static inline struct clk_fixup_mux *to_clk_fixup_mux(struct clk_hw *hw)
{
	struct clk_mux *mux = to_clk_mux(hw);

	return container_of(mux, struct clk_fixup_mux, mux);
}

static u8 clk_fixup_mux_get_parent(struct clk_hw *hw)
{
	struct clk_fixup_mux *fixup_mux = to_clk_fixup_mux(hw);

	return fixup_mux->ops->get_parent(&fixup_mux->mux.hw);
}

static int clk_fixup_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_fixup_mux *fixup_mux = to_clk_fixup_mux(hw);
	struct clk_mux *mux = to_clk_mux(hw);
	unsigned long flags = 0;
	u32 val;

	spin_lock_irqsave(mux->lock, flags);

	val = readl(mux->reg);
	val &= ~(mux->mask << mux->shift);
	val |= index << mux->shift;
	fixup_mux->fixup(&val);
	writel(val, mux->reg);

	spin_unlock_irqrestore(mux->lock, flags);

	return 0;
}

static const struct clk_ops clk_fixup_mux_ops = {
	.get_parent = clk_fixup_mux_get_parent,
	.set_parent = clk_fixup_mux_set_parent,
};

struct clk *imx_clk_fixup_mux(const char *name, void __iomem *reg,
			      u8 shift, u8 width, const char * const *parents,
			      int num_parents, void (*fixup)(u32 *val))
{
	struct clk_fixup_mux *fixup_mux;
	struct clk *clk;
	struct clk_init_data init;

	if (!fixup)
		return ERR_PTR(-EINVAL);

	fixup_mux = kzalloc(sizeof(*fixup_mux), GFP_KERNEL);
	if (!fixup_mux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_fixup_mux_ops;
	init.parent_names = parents;
	init.num_parents = num_parents;
	init.flags = 0;

	fixup_mux->mux.reg = reg;
	fixup_mux->mux.shift = shift;
	fixup_mux->mux.mask = BIT(width) - 1;
	fixup_mux->mux.lock = &imx_ccm_lock;
	fixup_mux->mux.hw.init = &init;
	fixup_mux->ops = &clk_mux_ops;
	fixup_mux->fixup = fixup;

	clk = clk_register(NULL, &fixup_mux->mux.hw);
	if (IS_ERR(clk))
		kfree(fixup_mux);

	return clk;
}

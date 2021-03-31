// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright (C) 2011 Richard Zhao, Linaro <richard.zhao@linaro.org>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 *
 * Simple multiplexer clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>

/*
 * DOC: basic adjustable multiplexer clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is only affected by parent switching.  No clk_set_rate support
 * parent - parent is adjustable through clk_set_parent
 */

static inline u32 clk_mux_readl(struct clk_mux *mux)
{
	if (mux->flags & CLK_MUX_BIG_ENDIAN)
		return ioread32be(mux->reg);

	return readl(mux->reg);
}

static inline void clk_mux_writel(struct clk_mux *mux, u32 val)
{
	if (mux->flags & CLK_MUX_BIG_ENDIAN)
		iowrite32be(val, mux->reg);
	else
		writel(val, mux->reg);
}

int clk_mux_val_to_index(struct clk_hw *hw, u32 *table, unsigned int flags,
			 unsigned int val)
{
	int num_parents = clk_hw_get_num_parents(hw);

	if (table) {
		int i;

		for (i = 0; i < num_parents; i++)
			if (table[i] == val)
				return i;
		return -EINVAL;
	}

	if (val && (flags & CLK_MUX_INDEX_BIT))
		val = ffs(val) - 1;

	if (val && (flags & CLK_MUX_INDEX_ONE))
		val--;

	if (val >= num_parents)
		return -EINVAL;

	return val;
}
EXPORT_SYMBOL_GPL(clk_mux_val_to_index);

unsigned int clk_mux_index_to_val(u32 *table, unsigned int flags, u8 index)
{
	unsigned int val = index;

	if (table) {
		val = table[index];
	} else {
		if (flags & CLK_MUX_INDEX_BIT)
			val = 1 << index;

		if (flags & CLK_MUX_INDEX_ONE)
			val++;
	}

	return val;
}
EXPORT_SYMBOL_GPL(clk_mux_index_to_val);

static u8 clk_mux_get_parent(struct clk_hw *hw)
{
	struct clk_mux *mux = to_clk_mux(hw);
	u32 val;

	val = clk_mux_readl(mux) >> mux->shift;
	val &= mux->mask;

	return clk_mux_val_to_index(hw, mux->table, mux->flags, val);
}

static int clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_mux *mux = to_clk_mux(hw);
	u32 val = clk_mux_index_to_val(mux->table, mux->flags, index);
	unsigned long flags = 0;
	u32 reg;

	if (mux->lock)
		spin_lock_irqsave(mux->lock, flags);
	else
		__acquire(mux->lock);

	if (mux->flags & CLK_MUX_HIWORD_MASK) {
		reg = mux->mask << (mux->shift + 16);
	} else {
		reg = clk_mux_readl(mux);
		reg &= ~(mux->mask << mux->shift);
	}
	val = val << mux->shift;
	reg |= val;
	clk_mux_writel(mux, reg);

	if (mux->lock)
		spin_unlock_irqrestore(mux->lock, flags);
	else
		__release(mux->lock);

	return 0;
}

static int clk_mux_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct clk_mux *mux = to_clk_mux(hw);

	return clk_mux_determine_rate_flags(hw, req, mux->flags);
}

const struct clk_ops clk_mux_ops = {
	.get_parent = clk_mux_get_parent,
	.set_parent = clk_mux_set_parent,
	.determine_rate = clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_mux_ops);

const struct clk_ops clk_mux_ro_ops = {
	.get_parent = clk_mux_get_parent,
};
EXPORT_SYMBOL_GPL(clk_mux_ro_ops);

struct clk_hw *__clk_hw_register_mux(struct device *dev, struct device_node *np,
		const char *name, u8 num_parents,
		const char * const *parent_names,
		const struct clk_hw **parent_hws,
		const struct clk_parent_data *parent_data,
		unsigned long flags, void __iomem *reg, u8 shift, u32 mask,
		u8 clk_mux_flags, u32 *table, spinlock_t *lock)
{
	struct clk_mux *mux;
	struct clk_hw *hw;
	struct clk_init_data init = {};
	u8 width = 0;
	int ret = -EINVAL;

	if (clk_mux_flags & CLK_MUX_HIWORD_MASK) {
		width = fls(mask) - ffs(mask) + 1;
		if (width + shift > 16) {
			pr_err("mux value exceeds LOWORD field\n");
			return ERR_PTR(-EINVAL);
		}
	}

	/* allocate the mux */
	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	if (clk_mux_flags & CLK_MUX_READ_ONLY)
		init.ops = &clk_mux_ro_ops;
	else
		init.ops = &clk_mux_ops;
	init.flags = flags;
	init.parent_names = parent_names;
	init.parent_data = parent_data;
	init.parent_hws = parent_hws;
	init.num_parents = num_parents;

	/* struct clk_mux assignments */
	mux->reg = reg;
	mux->shift = shift;
	mux->mask = mask;
	mux->flags = clk_mux_flags;
	mux->lock = lock;
	mux->table = table;
	mux->hw.init = &init;

	hw = &mux->hw;
	if (dev || !np)
		ret = clk_hw_register(dev, hw);
	else if (np)
		ret = of_clk_hw_register(np, hw);
	if (ret) {
		kfree(mux);
		hw = ERR_PTR(ret);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(__clk_hw_register_mux);

static void devm_clk_hw_release_mux(struct device *dev, void *res)
{
	clk_hw_unregister_mux(*(struct clk_hw **)res);
}

struct clk_hw *__devm_clk_hw_register_mux(struct device *dev, struct device_node *np,
		const char *name, u8 num_parents,
		const char * const *parent_names,
		const struct clk_hw **parent_hws,
		const struct clk_parent_data *parent_data,
		unsigned long flags, void __iomem *reg, u8 shift, u32 mask,
		u8 clk_mux_flags, u32 *table, spinlock_t *lock)
{
	struct clk_hw **ptr, *hw;

	ptr = devres_alloc(devm_clk_hw_release_mux, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	hw = __clk_hw_register_mux(dev, np, name, num_parents, parent_names, parent_hws,
				       parent_data, flags, reg, shift, mask,
				       clk_mux_flags, table, lock);

	if (!IS_ERR(hw)) {
		*ptr = hw;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(__devm_clk_hw_register_mux);

struct clk *clk_register_mux_table(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents,
		unsigned long flags, void __iomem *reg, u8 shift, u32 mask,
		u8 clk_mux_flags, u32 *table, spinlock_t *lock)
{
	struct clk_hw *hw;

	hw = clk_hw_register_mux_table(dev, name, parent_names,
				       num_parents, flags, reg, shift, mask,
				       clk_mux_flags, table, lock);
	if (IS_ERR(hw))
		return ERR_CAST(hw);
	return hw->clk;
}
EXPORT_SYMBOL_GPL(clk_register_mux_table);

void clk_unregister_mux(struct clk *clk)
{
	struct clk_mux *mux;
	struct clk_hw *hw;

	hw = __clk_get_hw(clk);
	if (!hw)
		return;

	mux = to_clk_mux(hw);

	clk_unregister(clk);
	kfree(mux);
}
EXPORT_SYMBOL_GPL(clk_unregister_mux);

void clk_hw_unregister_mux(struct clk_hw *hw)
{
	struct clk_mux *mux;

	mux = to_clk_mux(hw);

	clk_hw_unregister(hw);
	kfree(mux);
}
EXPORT_SYMBOL_GPL(clk_hw_unregister_mux);

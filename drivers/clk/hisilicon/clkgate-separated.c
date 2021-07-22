// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hisilicon clock separated gate driver
 *
 * Copyright (c) 2012-2013 Hisilicon Limited.
 * Copyright (c) 2012-2013 Linaro Limited.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 *	   Xin Li <li.xin@linaro.org>
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "clk.h"

/* clock separated gate register offset */
#define CLKGATE_SEPERATED_ENABLE		0x0
#define CLKGATE_SEPERATED_DISABLE		0x4
#define CLKGATE_SEPERATED_STATUS		0x8

struct clkgate_separated {
	struct clk_hw	hw;
	void __iomem	*enable;	/* enable register */
	u8		bit_idx;	/* bits in enable/disable register */
	u8		flags;
	spinlock_t	*lock;
};

static int clkgate_separated_enable(struct clk_hw *hw)
{
	struct clkgate_separated *sclk;
	unsigned long flags = 0;
	u32 reg;

	sclk = container_of(hw, struct clkgate_separated, hw);
	if (sclk->lock)
		spin_lock_irqsave(sclk->lock, flags);
	reg = BIT(sclk->bit_idx);
	writel_relaxed(reg, sclk->enable);
	readl_relaxed(sclk->enable + CLKGATE_SEPERATED_STATUS);
	if (sclk->lock)
		spin_unlock_irqrestore(sclk->lock, flags);
	return 0;
}

static void clkgate_separated_disable(struct clk_hw *hw)
{
	struct clkgate_separated *sclk;
	unsigned long flags = 0;
	u32 reg;

	sclk = container_of(hw, struct clkgate_separated, hw);
	if (sclk->lock)
		spin_lock_irqsave(sclk->lock, flags);
	reg = BIT(sclk->bit_idx);
	writel_relaxed(reg, sclk->enable + CLKGATE_SEPERATED_DISABLE);
	readl_relaxed(sclk->enable + CLKGATE_SEPERATED_STATUS);
	if (sclk->lock)
		spin_unlock_irqrestore(sclk->lock, flags);
}

static int clkgate_separated_is_enabled(struct clk_hw *hw)
{
	struct clkgate_separated *sclk;
	u32 reg;

	sclk = container_of(hw, struct clkgate_separated, hw);
	reg = readl_relaxed(sclk->enable + CLKGATE_SEPERATED_STATUS);
	reg &= BIT(sclk->bit_idx);

	return reg ? 1 : 0;
}

static const struct clk_ops clkgate_separated_ops = {
	.enable		= clkgate_separated_enable,
	.disable	= clkgate_separated_disable,
	.is_enabled	= clkgate_separated_is_enabled,
};

struct clk *hisi_register_clkgate_sep(struct device *dev, const char *name,
				      const char *parent_name,
				      unsigned long flags,
				      void __iomem *reg, u8 bit_idx,
				      u8 clk_gate_flags, spinlock_t *lock)
{
	struct clkgate_separated *sclk;
	struct clk *clk;
	struct clk_init_data init;

	sclk = kzalloc(sizeof(*sclk), GFP_KERNEL);
	if (!sclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clkgate_separated_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	sclk->enable = reg + CLKGATE_SEPERATED_ENABLE;
	sclk->bit_idx = bit_idx;
	sclk->flags = clk_gate_flags;
	sclk->hw.init = &init;
	sclk->lock = lock;

	clk = clk_register(dev, &sclk->hw);
	if (IS_ERR(clk))
		kfree(sclk);
	return clk;
}

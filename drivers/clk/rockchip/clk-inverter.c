// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include "clk.h"

struct rockchip_inv_clock {
	struct clk_hw	hw;
	void __iomem	*reg;
	int		shift;
	int		flags;
	spinlock_t	*lock;
};

#define to_inv_clock(_hw) container_of(_hw, struct rockchip_inv_clock, hw)

#define INVERTER_MASK 0x1

static int rockchip_inv_get_phase(struct clk_hw *hw)
{
	struct rockchip_inv_clock *inv_clock = to_inv_clock(hw);
	u32 val;

	val = readl(inv_clock->reg) >> inv_clock->shift;
	val &= INVERTER_MASK;
	return val ? 180 : 0;
}

static int rockchip_inv_set_phase(struct clk_hw *hw, int degrees)
{
	struct rockchip_inv_clock *inv_clock = to_inv_clock(hw);
	u32 val;

	if (degrees % 180 == 0) {
		val = !!degrees;
	} else {
		pr_err("%s: unsupported phase %d for %s\n",
		       __func__, degrees, clk_hw_get_name(hw));
		return -EINVAL;
	}

	if (inv_clock->flags & ROCKCHIP_INVERTER_HIWORD_MASK) {
		writel(HIWORD_UPDATE(val, INVERTER_MASK, inv_clock->shift),
		       inv_clock->reg);
	} else {
		unsigned long flags;
		u32 reg;

		spin_lock_irqsave(inv_clock->lock, flags);

		reg = readl(inv_clock->reg);
		reg &= ~BIT(inv_clock->shift);
		reg |= val;
		writel(reg, inv_clock->reg);

		spin_unlock_irqrestore(inv_clock->lock, flags);
	}

	return 0;
}

static const struct clk_ops rockchip_inv_clk_ops = {
	.get_phase	= rockchip_inv_get_phase,
	.set_phase	= rockchip_inv_set_phase,
};

struct clk *rockchip_clk_register_inverter(const char *name,
				const char *const *parent_names, u8 num_parents,
				void __iomem *reg, int shift, int flags,
				spinlock_t *lock)
{
	struct clk_init_data init;
	struct rockchip_inv_clock *inv_clock;
	struct clk *clk;

	inv_clock = kmalloc(sizeof(*inv_clock), GFP_KERNEL);
	if (!inv_clock)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent_names;
	init.ops = &rockchip_inv_clk_ops;

	inv_clock->hw.init = &init;
	inv_clock->reg = reg;
	inv_clock->shift = shift;
	inv_clock->flags = flags;
	inv_clock->lock = lock;

	clk = clk_register(NULL, &inv_clock->hw);
	if (IS_ERR(clk))
		kfree(inv_clock);

	return clk;
}

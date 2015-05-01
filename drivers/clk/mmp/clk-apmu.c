/*
 * mmp AXI peripharal clock operation source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "clk.h"

#define to_clk_apmu(clk) (container_of(clk, struct clk_apmu, clk))
struct clk_apmu {
	struct clk_hw   hw;
	void __iomem    *base;
	u32		rst_mask;
	u32		enable_mask;
	spinlock_t	*lock;
};

static int clk_apmu_enable(struct clk_hw *hw)
{
	struct clk_apmu *apmu = to_clk_apmu(hw);
	unsigned long data;
	unsigned long flags = 0;

	if (apmu->lock)
		spin_lock_irqsave(apmu->lock, flags);

	data = readl_relaxed(apmu->base) | apmu->enable_mask;
	writel_relaxed(data, apmu->base);

	if (apmu->lock)
		spin_unlock_irqrestore(apmu->lock, flags);

	return 0;
}

static void clk_apmu_disable(struct clk_hw *hw)
{
	struct clk_apmu *apmu = to_clk_apmu(hw);
	unsigned long data;
	unsigned long flags = 0;

	if (apmu->lock)
		spin_lock_irqsave(apmu->lock, flags);

	data = readl_relaxed(apmu->base) & ~apmu->enable_mask;
	writel_relaxed(data, apmu->base);

	if (apmu->lock)
		spin_unlock_irqrestore(apmu->lock, flags);
}

static struct clk_ops clk_apmu_ops = {
	.enable = clk_apmu_enable,
	.disable = clk_apmu_disable,
};

struct clk *mmp_clk_register_apmu(const char *name, const char *parent_name,
		void __iomem *base, u32 enable_mask, spinlock_t *lock)
{
	struct clk_apmu *apmu;
	struct clk *clk;
	struct clk_init_data init;

	apmu = kzalloc(sizeof(*apmu), GFP_KERNEL);
	if (!apmu)
		return NULL;

	init.name = name;
	init.ops = &clk_apmu_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	apmu->base = base;
	apmu->enable_mask = enable_mask;
	apmu->lock = lock;
	apmu->hw.init = &init;

	clk = clk_register(NULL, &apmu->hw);

	if (IS_ERR(clk))
		kfree(apmu);

	return clk;
}

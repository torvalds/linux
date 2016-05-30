/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Gated clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/imx_sema4.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include "clk.h"
#include "common.h"
#include "hardware.h"

/**
 * DOC: basic gatable clock which can gate and ungate it's ouput
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_gate2 {
	struct clk_hw hw;
	void __iomem	*reg;
	u8		bit_idx;
	u8		flags;
	spinlock_t	*lock;
	unsigned int	*share_count;
};

#define to_clk_gate2(_hw) container_of(_hw, struct clk_gate2, hw)
#define CCM_CCGR_FULL_ENABLE	0x3

static void clk_gate2_do_hardware(struct clk_gate2 *gate, bool enable)
{
	u32 reg;

	reg = readl(gate->reg);
	if (enable)
		reg |= CCM_CCGR_FULL_ENABLE << gate->bit_idx;
	else
		reg &= ~(CCM_CCGR_FULL_ENABLE << gate->bit_idx);
	writel(reg, gate->reg);
}

static void clk_gate2_do_shared_clks(struct clk_hw *hw, bool enable)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);

	if (imx_src_is_m4_enabled() && cpu_is_imx6sx()) {
#ifdef CONFIG_SOC_IMX6SX
		if (!amp_power_mutex || !shared_mem) {
			if (enable)
				clk_gate2_do_hardware(gate, enable);
			return;
		}

		imx_sema4_mutex_lock(amp_power_mutex);
		if (shared_mem->ca9_valid != SHARED_MEM_MAGIC_NUMBER ||
			shared_mem->cm4_valid != SHARED_MEM_MAGIC_NUMBER) {
			imx_sema4_mutex_unlock(amp_power_mutex);
			return;
		}

		if (!imx_update_shared_mem(hw, enable)) {
			imx_sema4_mutex_unlock(amp_power_mutex);
			return;
		}

		clk_gate2_do_hardware(gate, enable);

		imx_sema4_mutex_unlock(amp_power_mutex);
#endif
	} else {
		clk_gate2_do_hardware(gate, enable);
	}
}

static int clk_gate2_enable(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count && (*gate->share_count)++ > 0)
		goto out;

	clk_gate2_do_shared_clks(hw, true);
out:
	spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static void clk_gate2_disable(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count) {
		if (WARN_ON(*gate->share_count == 0))
			goto out;
		else if (--(*gate->share_count) > 0)
			goto out;
	}

	clk_gate2_do_shared_clks(hw, false);
out:
	spin_unlock_irqrestore(gate->lock, flags);
}

static int clk_gate2_reg_is_enabled(void __iomem *reg, u8 bit_idx)
{
	u32 val = readl(reg);

	if (((val >> bit_idx) & 1) == 1)
		return 1;

	return 0;
}

static int clk_gate2_is_enabled(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);

	return clk_gate2_reg_is_enabled(gate->reg, gate->bit_idx);
}

static void clk_gate2_disable_unused(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(gate->lock, flags);

	if (!gate->share_count || *gate->share_count == 0)
		clk_gate2_do_shared_clks(hw, false);

	spin_unlock_irqrestore(gate->lock, flags);
}

static struct clk_ops clk_gate2_ops = {
	.enable = clk_gate2_enable,
	.disable = clk_gate2_disable,
	.disable_unused = clk_gate2_disable_unused,
	.is_enabled = clk_gate2_is_enabled,
};

struct clk *clk_register_gate2(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		u8 clk_gate2_flags, spinlock_t *lock,
		unsigned int *share_count)
{
	struct clk_gate2 *gate;
	struct clk *clk;
	struct clk_init_data init;

	gate = kzalloc(sizeof(struct clk_gate2), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	/* struct clk_gate2 assignments */
	gate->reg = reg;
	gate->bit_idx = bit_idx;
	gate->flags = clk_gate2_flags;
	gate->lock = lock;
	gate->share_count = share_count;

	init.name = name;
	init.ops = &clk_gate2_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->hw.init = &init;

	clk = clk_register(dev, &gate->hw);
	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 NXP
 *
 * Peng Fan <peng.fan@nxp.com>
 */

#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "clk.h"

#define DIRECT_OFFSET		0x0

/*
 * 0b000 - LPCG will be OFF in any CPU mode.
 * 0b100 - LPCG will be ON in any CPU mode.
 */
#define LPM_SETTING_OFF		0x0
#define LPM_SETTING_ON		0x4

#define LPM_CUR_OFFSET		0x1c

#define AUTHEN_OFFSET		0x30
#define CPULPM_EN		BIT(2)
#define TZ_NS_SHIFT		9
#define TZ_NS_MASK		BIT(9)

#define WHITE_LIST_SHIFT	16

struct imx93_clk_gate {
	struct clk_hw hw;
	void __iomem	*reg;
	u32		bit_idx;
	u32		val;
	u32		mask;
	spinlock_t	*lock;
	unsigned int	*share_count;
};

#define to_imx93_clk_gate(_hw) container_of(_hw, struct imx93_clk_gate, hw)

static void imx93_clk_gate_do_hardware(struct clk_hw *hw, bool enable)
{
	struct imx93_clk_gate *gate = to_imx93_clk_gate(hw);
	u32 val;

	val = readl(gate->reg + AUTHEN_OFFSET);
	if (val & CPULPM_EN) {
		val = enable ? LPM_SETTING_ON : LPM_SETTING_OFF;
		writel(val, gate->reg + LPM_CUR_OFFSET);
	} else {
		val = readl(gate->reg + DIRECT_OFFSET);
		val &= ~(gate->mask << gate->bit_idx);
		if (enable)
			val |= (gate->val & gate->mask) << gate->bit_idx;
		writel(val, gate->reg + DIRECT_OFFSET);
	}
}

static int imx93_clk_gate_enable(struct clk_hw *hw)
{
	struct imx93_clk_gate *gate = to_imx93_clk_gate(hw);
	unsigned long flags;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count && (*gate->share_count)++ > 0)
		goto out;

	imx93_clk_gate_do_hardware(hw, true);
out:
	spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static void imx93_clk_gate_disable(struct clk_hw *hw)
{
	struct imx93_clk_gate *gate = to_imx93_clk_gate(hw);
	unsigned long flags;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count) {
		if (WARN_ON(*gate->share_count == 0))
			goto out;
		else if (--(*gate->share_count) > 0)
			goto out;
	}

	imx93_clk_gate_do_hardware(hw, false);
out:
	spin_unlock_irqrestore(gate->lock, flags);
}

static int imx93_clk_gate_reg_is_enabled(struct imx93_clk_gate *gate)
{
	u32 val = readl(gate->reg + AUTHEN_OFFSET);

	if (val & CPULPM_EN) {
		val = readl(gate->reg + LPM_CUR_OFFSET);
		if (val == LPM_SETTING_ON)
			return 1;
	} else {
		val = readl(gate->reg);
		if (((val >> gate->bit_idx) & gate->mask) == gate->val)
			return 1;
	}

	return 0;
}

static int imx93_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct imx93_clk_gate *gate = to_imx93_clk_gate(hw);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(gate->lock, flags);

	ret = imx93_clk_gate_reg_is_enabled(gate);

	spin_unlock_irqrestore(gate->lock, flags);

	return ret;
}

static void imx93_clk_gate_disable_unused(struct clk_hw *hw)
{
	struct imx93_clk_gate *gate = to_imx93_clk_gate(hw);
	unsigned long flags;

	spin_lock_irqsave(gate->lock, flags);

	if (!gate->share_count || *gate->share_count == 0)
		imx93_clk_gate_do_hardware(hw, false);

	spin_unlock_irqrestore(gate->lock, flags);
}

static const struct clk_ops imx93_clk_gate_ops = {
	.enable = imx93_clk_gate_enable,
	.disable = imx93_clk_gate_disable,
	.disable_unused = imx93_clk_gate_disable_unused,
	.is_enabled = imx93_clk_gate_is_enabled,
};

static const struct clk_ops imx93_clk_gate_ro_ops = {
	.is_enabled = imx93_clk_gate_is_enabled,
};

struct clk_hw *imx93_clk_gate(struct device *dev, const char *name, const char *parent_name,
			      unsigned long flags, void __iomem *reg, u32 bit_idx, u32 val,
			      u32 mask, u32 domain_id, unsigned int *share_count)
{
	struct imx93_clk_gate *gate;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;
	u32 authen;

	gate = kzalloc(sizeof(struct imx93_clk_gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->reg = reg;
	gate->lock = &imx_ccm_lock;
	gate->bit_idx = bit_idx;
	gate->val = val;
	gate->mask = mask;
	gate->share_count = share_count;

	init.name = name;
	init.ops = &imx93_clk_gate_ops;
	init.flags = flags | CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->hw.init = &init;
	hw = &gate->hw;

	authen = readl(reg + AUTHEN_OFFSET);
	if (!(authen & TZ_NS_MASK) || !(authen & BIT(WHITE_LIST_SHIFT + domain_id)))
		init.ops = &imx93_clk_gate_ro_ops;

	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(gate);
		return ERR_PTR(ret);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(imx93_clk_gate);

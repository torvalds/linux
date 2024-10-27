// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 *	Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/units.h>

#include "clk-scu.h"

static DEFINE_SPINLOCK(imx_lpcg_scu_lock);

#define CLK_GATE_SCU_LPCG_MASK		0x3
#define CLK_GATE_SCU_LPCG_HW_SEL	BIT(0)
#define CLK_GATE_SCU_LPCG_SW_SEL	BIT(1)

/*
 * struct clk_lpcg_scu - Description of LPCG clock
 *
 * @hw: clk_hw of this LPCG
 * @reg: register of this LPCG clock
 * @bit_idx: bit index of this LPCG clock
 * @hw_gate: HW auto gate enable
 *
 * This structure describes one LPCG clock
 */
struct clk_lpcg_scu {
	struct clk_hw hw;
	void __iomem *reg;
	u8 bit_idx;
	bool hw_gate;

	/* for state save&restore */
	u32 state;
};

#define to_clk_lpcg_scu(_hw) container_of(_hw, struct clk_lpcg_scu, hw)

/* e10858 -LPCG clock gating register synchronization errata */
static void lpcg_e10858_writel(unsigned long rate, void __iomem *reg, u32 val)
{
	writel(val, reg);

	if (rate >= 24 * HZ_PER_MHZ || rate == 0) {
		/*
		 * The time taken to access the LPCG registers from the AP core
		 * through the interconnect is longer than the minimum delay
		 * of 4 clock cycles required by the errata.
		 * Adding a readl will provide sufficient delay to prevent
		 * back-to-back writes.
		 */
		readl(reg);
	} else {
		/*
		 * For clocks running below 24MHz, wait a minimum of
		 * 4 clock cycles.
		 */
		ndelay(4 * (DIV_ROUND_UP(1000 * HZ_PER_MHZ, rate)));
	}
}

static int clk_lpcg_scu_enable(struct clk_hw *hw)
{
	struct clk_lpcg_scu *clk = to_clk_lpcg_scu(hw);
	unsigned long flags;
	u32 reg, val;

	spin_lock_irqsave(&imx_lpcg_scu_lock, flags);

	reg = readl_relaxed(clk->reg);
	reg &= ~(CLK_GATE_SCU_LPCG_MASK << clk->bit_idx);

	val = CLK_GATE_SCU_LPCG_SW_SEL;
	if (clk->hw_gate)
		val |= CLK_GATE_SCU_LPCG_HW_SEL;

	reg |= val << clk->bit_idx;

	lpcg_e10858_writel(clk_hw_get_rate(hw), clk->reg, reg);

	spin_unlock_irqrestore(&imx_lpcg_scu_lock, flags);

	return 0;
}

static void clk_lpcg_scu_disable(struct clk_hw *hw)
{
	struct clk_lpcg_scu *clk = to_clk_lpcg_scu(hw);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&imx_lpcg_scu_lock, flags);

	reg = readl_relaxed(clk->reg);
	reg &= ~(CLK_GATE_SCU_LPCG_MASK << clk->bit_idx);
	lpcg_e10858_writel(clk_hw_get_rate(hw), clk->reg, reg);

	spin_unlock_irqrestore(&imx_lpcg_scu_lock, flags);
}

static const struct clk_ops clk_lpcg_scu_ops = {
	.enable = clk_lpcg_scu_enable,
	.disable = clk_lpcg_scu_disable,
};

struct clk_hw *__imx_clk_lpcg_scu(struct device *dev, const char *name,
				  const char *parent_name, unsigned long flags,
				  void __iomem *reg, u8 bit_idx, bool hw_gate)
{
	struct clk_lpcg_scu *clk;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return ERR_PTR(-ENOMEM);

	clk->reg = reg;
	clk->bit_idx = bit_idx;
	clk->hw_gate = hw_gate;

	init.name = name;
	init.ops = &clk_lpcg_scu_ops;
	init.flags = CLK_SET_RATE_PARENT | flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	clk->hw.init = &init;

	hw = &clk->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(clk);
		hw = ERR_PTR(ret);
		return hw;
	}

	if (dev)
		dev_set_drvdata(dev, clk);

	return hw;
}

void imx_clk_lpcg_scu_unregister(struct clk_hw *hw)
{
	struct clk_lpcg_scu *clk = to_clk_lpcg_scu(hw);

	clk_hw_unregister(&clk->hw);
	kfree(clk);
}

static int __maybe_unused imx_clk_lpcg_scu_suspend(struct device *dev)
{
	struct clk_lpcg_scu *clk = dev_get_drvdata(dev);

	clk->state = readl_relaxed(clk->reg);
	dev_dbg(dev, "save lpcg state 0x%x\n", clk->state);

	return 0;
}

static int __maybe_unused imx_clk_lpcg_scu_resume(struct device *dev)
{
	struct clk_lpcg_scu *clk = dev_get_drvdata(dev);

	writel(clk->state, clk->reg);
	lpcg_e10858_writel(0, clk->reg, clk->state);
	dev_dbg(dev, "restore lpcg state 0x%x\n", clk->state);

	return 0;
}

const struct dev_pm_ops imx_clk_lpcg_scu_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(imx_clk_lpcg_scu_suspend,
				      imx_clk_lpcg_scu_resume)
};

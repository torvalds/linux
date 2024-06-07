// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017~2018 NXP
 *
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "../clk-fractional-divider.h"
#include "clk.h"

#define PCG_PR_MASK		BIT(31)
#define PCG_PCS_SHIFT	24
#define PCG_PCS_MASK	0x7
#define PCG_CGC_SHIFT	30
#define PCG_FRAC_SHIFT	3
#define PCG_FRAC_WIDTH	1
#define PCG_PCD_SHIFT	0
#define PCG_PCD_WIDTH	3

#define SW_RST		BIT(28)

static int pcc_gate_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	unsigned long flags;
	u32 val;
	int ret;

	ret = clk_gate_ops.enable(hw);
	if (ret)
		return ret;

	spin_lock_irqsave(gate->lock, flags);
	/*
	 * release the sw reset for peripherals associated with
	 * with this pcc clock.
	 */
	val = readl(gate->reg);
	val |= SW_RST;
	writel(val, gate->reg);

	spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static void pcc_gate_disable(struct clk_hw *hw)
{
	clk_gate_ops.disable(hw);
}

static int pcc_gate_is_enabled(struct clk_hw *hw)
{
	return clk_gate_ops.is_enabled(hw);
}

static const struct clk_ops pcc_gate_ops = {
	.enable = pcc_gate_enable,
	.disable = pcc_gate_disable,
	.is_enabled = pcc_gate_is_enabled,
};

static struct clk_hw *imx_ulp_clk_hw_composite(const char *name,
				     const char * const *parent_names,
				     int num_parents, bool mux_present,
				     bool rate_present, bool gate_present,
				     void __iomem *reg, bool has_swrst)
{
	struct clk_hw *mux_hw = NULL, *fd_hw = NULL, *gate_hw = NULL;
	struct clk_fractional_divider *fd = NULL;
	struct clk_gate *gate = NULL;
	struct clk_mux *mux = NULL;
	struct clk_hw *hw;
	u32 val;

	val = readl(reg);
	if (!(val & PCG_PR_MASK)) {
		pr_info("PCC PR is 0 for clk:%s, bypass\n", name);
		return 0;
	}

	if (mux_present) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);
		mux_hw = &mux->hw;
		mux->reg = reg;
		mux->shift = PCG_PCS_SHIFT;
		mux->mask = PCG_PCS_MASK;
		if (has_swrst)
			mux->lock = &imx_ccm_lock;
	}

	if (rate_present) {
		fd = kzalloc(sizeof(*fd), GFP_KERNEL);
		if (!fd) {
			kfree(mux);
			return ERR_PTR(-ENOMEM);
		}
		fd_hw = &fd->hw;
		fd->reg = reg;
		fd->mshift = PCG_FRAC_SHIFT;
		fd->mwidth = PCG_FRAC_WIDTH;
		fd->nshift = PCG_PCD_SHIFT;
		fd->nwidth = PCG_PCD_WIDTH;
		fd->flags = CLK_FRAC_DIVIDER_ZERO_BASED;
		if (has_swrst)
			fd->lock = &imx_ccm_lock;
	}

	if (gate_present) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate) {
			kfree(mux);
			kfree(fd);
			return ERR_PTR(-ENOMEM);
		}
		gate_hw = &gate->hw;
		gate->reg = reg;
		gate->bit_idx = PCG_CGC_SHIFT;
		if (has_swrst)
			gate->lock = &imx_ccm_lock;
		/*
		 * make sure clock is gated during clock tree initialization,
		 * the HW ONLY allow clock parent/rate changed with clock gated,
		 * during clock tree initialization, clocks could be enabled
		 * by bootloader, so the HW status will mismatch with clock tree
		 * prepare count, then clock core driver will allow parent/rate
		 * change since the prepare count is zero, but HW actually
		 * prevent the parent/rate change due to the clock is enabled.
		 */
		val = readl_relaxed(reg);
		val &= ~(1 << PCG_CGC_SHIFT);
		writel_relaxed(val, reg);
	}

	hw = clk_hw_register_composite(NULL, name, parent_names, num_parents,
				       mux_hw, &clk_mux_ops, fd_hw,
				       &clk_fractional_divider_ops, gate_hw,
				       has_swrst ? &pcc_gate_ops : &clk_gate_ops, CLK_SET_RATE_GATE |
				       CLK_SET_PARENT_GATE | CLK_SET_RATE_NO_REPARENT);
	if (IS_ERR(hw)) {
		kfree(mux);
		kfree(fd);
		kfree(gate);
	}

	return hw;
}

struct clk_hw *imx7ulp_clk_hw_composite(const char *name, const char * const *parent_names,
				int num_parents, bool mux_present, bool rate_present,
				bool gate_present, void __iomem *reg)
{
	return imx_ulp_clk_hw_composite(name, parent_names, num_parents, mux_present, rate_present,
					gate_present, reg, false);
}

struct clk_hw *imx8ulp_clk_hw_composite(const char *name, const char * const *parent_names,
				int num_parents, bool mux_present, bool rate_present,
				bool gate_present, void __iomem *reg, bool has_swrst)
{
	return imx_ulp_clk_hw_composite(name, parent_names, num_parents, mux_present, rate_present,
					gate_present, reg, has_swrst);
}
EXPORT_SYMBOL_GPL(imx8ulp_clk_hw_composite);

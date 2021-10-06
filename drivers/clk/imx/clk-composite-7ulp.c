// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017~2018 NXP
 *
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "../clk-fractional-divider.h"
#include "clk.h"

#define PCG_PCS_SHIFT	24
#define PCG_PCS_MASK	0x7
#define PCG_CGC_SHIFT	30
#define PCG_FRAC_SHIFT	3
#define PCG_FRAC_WIDTH	1
#define PCG_FRAC_MASK	BIT(3)
#define PCG_PCD_SHIFT	0
#define PCG_PCD_WIDTH	3
#define PCG_PCD_MASK	0x7

struct clk_hw *imx7ulp_clk_hw_composite(const char *name,
				     const char * const *parent_names,
				     int num_parents, bool mux_present,
				     bool rate_present, bool gate_present,
				     void __iomem *reg)
{
	struct clk_hw *mux_hw = NULL, *fd_hw = NULL, *gate_hw = NULL;
	struct clk_fractional_divider *fd = NULL;
	struct clk_gate *gate = NULL;
	struct clk_mux *mux = NULL;
	struct clk_hw *hw;

	if (mux_present) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);
		mux_hw = &mux->hw;
		mux->reg = reg;
		mux->shift = PCG_PCS_SHIFT;
		mux->mask = PCG_PCS_MASK;
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
		fd->mmask  = PCG_FRAC_MASK;
		fd->nshift = PCG_PCD_SHIFT;
		fd->nwidth = PCG_PCD_WIDTH;
		fd->nmask = PCG_PCD_MASK;
		fd->flags = CLK_FRAC_DIVIDER_ZERO_BASED;
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
	}

	hw = clk_hw_register_composite(NULL, name, parent_names, num_parents,
				       mux_hw, &clk_mux_ops, fd_hw,
				       &clk_fractional_divider_ops, gate_hw,
				       &clk_gate_ops, CLK_SET_RATE_GATE |
				       CLK_SET_PARENT_GATE);
	if (IS_ERR(hw)) {
		kfree(mux);
		kfree(fd);
		kfree(gate);
	}

	return hw;
}

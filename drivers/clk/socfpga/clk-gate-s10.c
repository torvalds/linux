// SPDX-License-Identifier:	GPL-2.0
/*
 * Copyright (C) 2017, Intel Corporation
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "stratix10-clk.h"
#include "clk.h"

#define SOCFPGA_CS_PDBG_CLK	"cs_pdbg_clk"
#define to_socfpga_gate_clk(p) container_of(p, struct socfpga_gate_clk, hw.hw)

static unsigned long socfpga_gate_clk_recalc_rate(struct clk_hw *hwclk,
						  unsigned long parent_rate)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 div = 1, val;

	if (socfpgaclk->fixed_div) {
		div = socfpgaclk->fixed_div;
	} else if (socfpgaclk->div_reg) {
		val = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
		val &= GENMASK(socfpgaclk->width - 1, 0);
		div = (1 << val);
	}
	return parent_rate / div;
}

static unsigned long socfpga_dbg_clk_recalc_rate(struct clk_hw *hwclk,
						  unsigned long parent_rate)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 div, val;

	val = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
	val &= GENMASK(socfpgaclk->width - 1, 0);
	div = (1 << val);
	div = div ? 4 : 1;

	return parent_rate / div;
}

static u8 socfpga_gate_get_parent(struct clk_hw *hwclk)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 mask;
	u8 parent = 0;

	if (socfpgaclk->bypass_reg) {
		mask = (0x1 << socfpgaclk->bypass_shift);
		parent = ((readl(socfpgaclk->bypass_reg) & mask) >>
			  socfpgaclk->bypass_shift);
	}
	return parent;
}

static struct clk_ops gateclk_ops = {
	.recalc_rate = socfpga_gate_clk_recalc_rate,
	.get_parent = socfpga_gate_get_parent,
};

static const struct clk_ops dbgclk_ops = {
	.recalc_rate = socfpga_dbg_clk_recalc_rate,
	.get_parent = socfpga_gate_get_parent,
};

struct clk_hw *s10_register_gate(const struct stratix10_gate_clock *clks, void __iomem *regbase)
{
	struct clk_hw *hw_clk;
	struct socfpga_gate_clk *socfpga_clk;
	struct clk_init_data init;
	const char *parent_name = clks->parent_name;
	int ret;

	socfpga_clk = kzalloc(sizeof(*socfpga_clk), GFP_KERNEL);
	if (!socfpga_clk)
		return NULL;

	socfpga_clk->hw.reg = regbase + clks->gate_reg;
	socfpga_clk->hw.bit_idx = clks->gate_idx;

	gateclk_ops.enable = clk_gate_ops.enable;
	gateclk_ops.disable = clk_gate_ops.disable;

	socfpga_clk->fixed_div = clks->fixed_div;

	if (clks->div_reg)
		socfpga_clk->div_reg = regbase + clks->div_reg;
	else
		socfpga_clk->div_reg = NULL;

	socfpga_clk->width = clks->div_width;
	socfpga_clk->shift = clks->div_offset;

	if (clks->bypass_reg)
		socfpga_clk->bypass_reg = regbase + clks->bypass_reg;
	else
		socfpga_clk->bypass_reg = NULL;
	socfpga_clk->bypass_shift = clks->bypass_shift;

	if (streq(clks->name, "cs_pdbg_clk"))
		init.ops = &dbgclk_ops;
	else
		init.ops = &gateclk_ops;

	init.name = clks->name;
	init.flags = clks->flags;

	init.num_parents = clks->num_parents;
	init.parent_names = parent_name ? &parent_name : NULL;
	if (init.parent_names == NULL)
		init.parent_data = clks->parent_data;
	socfpga_clk->hw.hw.init = &init;

	hw_clk = &socfpga_clk->hw.hw;

	ret = clk_hw_register(NULL, &socfpga_clk->hw.hw);
	if (ret) {
		kfree(socfpga_clk);
		return ERR_PTR(ret);
	}
	return hw_clk;
}

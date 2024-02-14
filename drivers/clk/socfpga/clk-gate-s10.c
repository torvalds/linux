// SPDX-License-Identifier: GPL-2.0
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

#define SOCFPGA_EMAC0_CLK		"emac0_clk"
#define SOCFPGA_EMAC1_CLK		"emac1_clk"
#define SOCFPGA_EMAC2_CLK		"emac2_clk"
#define AGILEX_BYPASS_OFFSET		0xC
#define STRATIX10_BYPASS_OFFSET		0x2C
#define BOOTCLK_BYPASS			2

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
	u32 mask, second_bypass;
	u8 parent = 0;
	const char *name = clk_hw_get_name(hwclk);

	if (socfpgaclk->bypass_reg) {
		mask = (0x1 << socfpgaclk->bypass_shift);
		parent = ((readl(socfpgaclk->bypass_reg) & mask) >>
			  socfpgaclk->bypass_shift);
	}

	if (streq(name, SOCFPGA_EMAC0_CLK) ||
	    streq(name, SOCFPGA_EMAC1_CLK) ||
	    streq(name, SOCFPGA_EMAC2_CLK)) {
		second_bypass = readl(socfpgaclk->bypass_reg -
				      STRATIX10_BYPASS_OFFSET);
		/* EMACA bypass to bootclk @0xB0 offset */
		if (second_bypass & 0x1)
			if (parent == 0) /* only applicable if parent is maca */
				parent = BOOTCLK_BYPASS;

		if (second_bypass & 0x2)
			if (parent == 1) /* only applicable if parent is macb */
				parent = BOOTCLK_BYPASS;
	}
	return parent;
}

static u8 socfpga_agilex_gate_get_parent(struct clk_hw *hwclk)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 mask, second_bypass;
	u8 parent = 0;
	const char *name = clk_hw_get_name(hwclk);

	if (socfpgaclk->bypass_reg) {
		mask = (0x1 << socfpgaclk->bypass_shift);
		parent = ((readl(socfpgaclk->bypass_reg) & mask) >>
			  socfpgaclk->bypass_shift);
	}

	if (streq(name, SOCFPGA_EMAC0_CLK) ||
	    streq(name, SOCFPGA_EMAC1_CLK) ||
	    streq(name, SOCFPGA_EMAC2_CLK)) {
		second_bypass = readl(socfpgaclk->bypass_reg -
				      AGILEX_BYPASS_OFFSET);
		/* EMACA bypass to bootclk @0x88 offset */
		if (second_bypass & 0x1)
			if (parent == 0) /* only applicable if parent is maca */
				parent = BOOTCLK_BYPASS;

		if (second_bypass & 0x2)
			if (parent == 1) /* only applicable if parent is macb */
				parent = BOOTCLK_BYPASS;
	}

	return parent;
}

static struct clk_ops gateclk_ops = {
	.recalc_rate = socfpga_gate_clk_recalc_rate,
	.get_parent = socfpga_gate_get_parent,
};

static const struct clk_ops agilex_gateclk_ops = {
	.recalc_rate = socfpga_gate_clk_recalc_rate,
	.get_parent = socfpga_agilex_gate_get_parent,
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

struct clk_hw *agilex_register_gate(const struct stratix10_gate_clock *clks, void __iomem *regbase)
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
		init.ops = &agilex_gateclk_ops;

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

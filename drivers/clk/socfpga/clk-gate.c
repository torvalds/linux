/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based from clk-highbank.c
 *
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>

#include "clk.h"

#define SOCFPGA_L4_MP_CLK		"l4_mp_clk"
#define SOCFPGA_L4_SP_CLK		"l4_sp_clk"
#define SOCFPGA_NAND_CLK		"nand_clk"
#define SOCFPGA_NAND_X_CLK		"nand_x_clk"
#define SOCFPGA_MMC_CLK			"sdmmc_clk"
#define SOCFPGA_GPIO_DB_CLK_OFFSET	0xA8

#define div_mask(width)	((1 << (width)) - 1)
#define streq(a, b) (strcmp((a), (b)) == 0)

#define to_socfpga_gate_clk(p) container_of(p, struct socfpga_gate_clk, hw.hw)

static u8 socfpga_clk_get_parent(struct clk_hw *hwclk)
{
	u32 l4_src;
	u32 perpll_src;

	if (streq(hwclk->init->name, SOCFPGA_L4_MP_CLK)) {
		l4_src = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		return l4_src &= 0x1;
	}
	if (streq(hwclk->init->name, SOCFPGA_L4_SP_CLK)) {
		l4_src = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		return !!(l4_src & 2);
	}

	perpll_src = readl(clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
	if (streq(hwclk->init->name, SOCFPGA_MMC_CLK))
		return perpll_src &= 0x3;
	if (streq(hwclk->init->name, SOCFPGA_NAND_CLK) ||
			streq(hwclk->init->name, SOCFPGA_NAND_X_CLK))
			return (perpll_src >> 2) & 3;

	/* QSPI clock */
	return (perpll_src >> 4) & 3;

}

static int socfpga_clk_set_parent(struct clk_hw *hwclk, u8 parent)
{
	u32 src_reg;

	if (streq(hwclk->init->name, SOCFPGA_L4_MP_CLK)) {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		src_reg &= ~0x1;
		src_reg |= parent;
		writel(src_reg, clk_mgr_base_addr + CLKMGR_L4SRC);
	} else if (streq(hwclk->init->name, SOCFPGA_L4_SP_CLK)) {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		src_reg &= ~0x2;
		src_reg |= (parent << 1);
		writel(src_reg, clk_mgr_base_addr + CLKMGR_L4SRC);
	} else {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
		if (streq(hwclk->init->name, SOCFPGA_MMC_CLK)) {
			src_reg &= ~0x3;
			src_reg |= parent;
		} else if (streq(hwclk->init->name, SOCFPGA_NAND_CLK) ||
			streq(hwclk->init->name, SOCFPGA_NAND_X_CLK)) {
			src_reg &= ~0xC;
			src_reg |= (parent << 2);
		} else {/* QSPI clock */
			src_reg &= ~0x30;
			src_reg |= (parent << 4);
		}
		writel(src_reg, clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
	}

	return 0;
}

static unsigned long socfpga_clk_recalc_rate(struct clk_hw *hwclk,
	unsigned long parent_rate)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 div = 1, val;

	if (socfpgaclk->fixed_div)
		div = socfpgaclk->fixed_div;
	else if (socfpgaclk->div_reg) {
		val = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
		val &= div_mask(socfpgaclk->width);
		/* Check for GPIO_DB_CLK by its offset */
		if ((int) socfpgaclk->div_reg & SOCFPGA_GPIO_DB_CLK_OFFSET)
			div = val + 1;
		else
			div = (1 << val);
	}

	return parent_rate / div;
}

static struct clk_ops gateclk_ops = {
	.recalc_rate = socfpga_clk_recalc_rate,
	.get_parent = socfpga_clk_get_parent,
	.set_parent = socfpga_clk_set_parent,
};

static void __init __socfpga_gate_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 clk_gate[2];
	u32 div_reg[3];
	u32 fixed_div;
	struct clk *clk;
	struct socfpga_gate_clk *socfpga_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFPGA_MAX_PARENTS];
	struct clk_init_data init;
	int rc;
	int i = 0;

	socfpga_clk = kzalloc(sizeof(*socfpga_clk), GFP_KERNEL);
	if (WARN_ON(!socfpga_clk))
		return;

	rc = of_property_read_u32_array(node, "clk-gate", clk_gate, 2);
	if (rc)
		clk_gate[0] = 0;

	if (clk_gate[0]) {
		socfpga_clk->hw.reg = clk_mgr_base_addr + clk_gate[0];
		socfpga_clk->hw.bit_idx = clk_gate[1];

		gateclk_ops.enable = clk_gate_ops.enable;
		gateclk_ops.disable = clk_gate_ops.disable;
	}

	rc = of_property_read_u32(node, "fixed-divider", &fixed_div);
	if (rc)
		socfpga_clk->fixed_div = 0;
	else
		socfpga_clk->fixed_div = fixed_div;

	rc = of_property_read_u32_array(node, "div-reg", div_reg, 3);
	if (!rc) {
		socfpga_clk->div_reg = clk_mgr_base_addr + div_reg[0];
		socfpga_clk->shift = div_reg[1];
		socfpga_clk->width = div_reg[2];
	} else {
		socfpga_clk->div_reg = 0;
	}

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;
	while (i < SOCFPGA_MAX_PARENTS && (parent_name[i] =
			of_clk_get_parent_name(node, i)) != NULL)
		i++;

	init.parent_names = parent_name;
	init.num_parents = i;
	socfpga_clk->hw.hw.init = &init;

	clk = clk_register(NULL, &socfpga_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(socfpga_clk);
		return;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (WARN_ON(rc))
		return;
}

void __init socfpga_gate_init(struct device_node *node)
{
	__socfpga_gate_init(node, &gateclk_ops);
}

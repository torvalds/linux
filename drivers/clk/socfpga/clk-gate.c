// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * Based from clk-highbank.c
 */
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "clk.h"

#define SOCFPGA_L4_MP_CLK		"l4_mp_clk"
#define SOCFPGA_L4_SP_CLK		"l4_sp_clk"
#define SOCFPGA_NAND_CLK		"nand_clk"
#define SOCFPGA_NAND_X_CLK		"nand_x_clk"
#define SOCFPGA_MMC_CLK			"sdmmc_clk"
#define SOCFPGA_GPIO_DB_CLK_OFFSET	0xA8

#define to_socfpga_gate_clk(p) container_of(p, struct socfpga_gate_clk, hw.hw)

/* SDMMC Group for System Manager defines */
#define SYSMGR_SDMMCGRP_CTRL_OFFSET    0x108

static u8 socfpga_clk_get_parent(struct clk_hw *hwclk)
{
	u32 l4_src;
	u32 perpll_src;
	const char *name = clk_hw_get_name(hwclk);

	if (streq(name, SOCFPGA_L4_MP_CLK)) {
		l4_src = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		return l4_src & 0x1;
	}
	if (streq(name, SOCFPGA_L4_SP_CLK)) {
		l4_src = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		return !!(l4_src & 2);
	}

	perpll_src = readl(clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
	if (streq(name, SOCFPGA_MMC_CLK))
		return perpll_src & 0x3;
	if (streq(name, SOCFPGA_NAND_CLK) ||
	    streq(name, SOCFPGA_NAND_X_CLK))
		return (perpll_src >> 2) & 3;

	/* QSPI clock */
	return (perpll_src >> 4) & 3;

}

static int socfpga_clk_set_parent(struct clk_hw *hwclk, u8 parent)
{
	u32 src_reg;
	const char *name = clk_hw_get_name(hwclk);

	if (streq(name, SOCFPGA_L4_MP_CLK)) {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		src_reg &= ~0x1;
		src_reg |= parent;
		writel(src_reg, clk_mgr_base_addr + CLKMGR_L4SRC);
	} else if (streq(name, SOCFPGA_L4_SP_CLK)) {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		src_reg &= ~0x2;
		src_reg |= (parent << 1);
		writel(src_reg, clk_mgr_base_addr + CLKMGR_L4SRC);
	} else {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
		if (streq(name, SOCFPGA_MMC_CLK)) {
			src_reg &= ~0x3;
			src_reg |= parent;
		} else if (streq(name, SOCFPGA_NAND_CLK) ||
			streq(name, SOCFPGA_NAND_X_CLK)) {
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

static u32 socfpga_clk_get_div(struct socfpga_gate_clk *socfpgaclk)
{
	u32 div = 1, val;

	if (socfpgaclk->fixed_div)
		div = socfpgaclk->fixed_div;
	else if (socfpgaclk->div_reg) {
		val = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
		val &= GENMASK(socfpgaclk->width - 1, 0);
		/* Check for GPIO_DB_CLK by its offset */
		if ((uintptr_t) socfpgaclk->div_reg & SOCFPGA_GPIO_DB_CLK_OFFSET)
			div = val + 1;
		else
			div = (1 << val);
	}

	return div;
}

static unsigned long socfpga_clk_recalc_rate(struct clk_hw *hwclk,
					     unsigned long parent_rate)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 div = socfpga_clk_get_div(socfpgaclk);

	return parent_rate / div;
}


static int socfpga_clk_determine_rate(struct clk_hw *hwclk,
				      struct clk_rate_request *req)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 div = socfpga_clk_get_div(socfpgaclk);

	req->rate = req->best_parent_rate / div;

	return 0;
}

static struct clk_ops gateclk_ops = {
	.recalc_rate = socfpga_clk_recalc_rate,
	.determine_rate = socfpga_clk_determine_rate,
	.get_parent = socfpga_clk_get_parent,
	.set_parent = socfpga_clk_set_parent,
};

void __init socfpga_gate_init(struct device_node *node)
{
	u32 clk_gate[2];
	u32 div_reg[3];
	u32 fixed_div;
	struct clk_hw *hw_clk;
	struct socfpga_gate_clk *socfpga_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFPGA_MAX_PARENTS];
	struct clk_init_data init;
	struct clk_ops *ops;
	int rc;

	socfpga_clk = kzalloc(sizeof(*socfpga_clk), GFP_KERNEL);
	if (WARN_ON(!socfpga_clk))
		return;

	ops = kmemdup(&gateclk_ops, sizeof(gateclk_ops), GFP_KERNEL);
	if (WARN_ON(!ops))
		goto err_kmemdup;

	rc = of_property_read_u32_array(node, "clk-gate", clk_gate, 2);
	if (rc)
		clk_gate[0] = 0;

	if (clk_gate[0]) {
		socfpga_clk->hw.reg = clk_mgr_base_addr + clk_gate[0];
		socfpga_clk->hw.bit_idx = clk_gate[1];

		ops->enable = clk_gate_ops.enable;
		ops->disable = clk_gate_ops.disable;
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
		socfpga_clk->div_reg = NULL;
	}

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;

	init.num_parents = of_clk_parent_fill(node, parent_name, SOCFPGA_MAX_PARENTS);
	if (init.num_parents < 2) {
		ops->get_parent = NULL;
		ops->set_parent = NULL;
	}

	init.parent_names = parent_name;
	socfpga_clk->hw.hw.init = &init;

	hw_clk = &socfpga_clk->hw.hw;

	rc = clk_hw_register(NULL, hw_clk);
	if (rc) {
		pr_err("Could not register clock:%s\n", clk_name);
		goto err_clk_hw_register;
	}

	rc = of_clk_add_hw_provider(node, of_clk_hw_simple_get, hw_clk);
	if (rc) {
		pr_err("Could not register clock provider for node:%s\n",
		       clk_name);
		goto err_of_clk_add_hw_provider;
	}

	return;

err_of_clk_add_hw_provider:
	clk_hw_unregister(hw_clk);
err_clk_hw_register:
	kfree(ops);
err_kmemdup:
	kfree(socfpga_clk);
}

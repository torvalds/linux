/*
 * Copyright (C) 2015 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>

#include "clk.h"

#define CLK_MGR_FREE_SHIFT		16
#define CLK_MGR_FREE_MASK		0x7

#define SOCFPGA_MPU_FREE_CLK		"mpu_free_clk"
#define SOCFPGA_NOC_FREE_CLK		"noc_free_clk"
#define SOCFPGA_SDMMC_FREE_CLK		"sdmmc_free_clk"
#define to_socfpga_periph_clk(p) container_of(p, struct socfpga_periph_clk, hw.hw)

static unsigned long clk_periclk_recalc_rate(struct clk_hw *hwclk,
					     unsigned long parent_rate)
{
	struct socfpga_periph_clk *socfpgaclk = to_socfpga_periph_clk(hwclk);
	u32 div;

	if (socfpgaclk->fixed_div) {
		div = socfpgaclk->fixed_div;
	} else if (socfpgaclk->div_reg) {
		div = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
		div &= GENMASK(socfpgaclk->width - 1, 0);
		div += 1;
	} else {
		div = ((readl(socfpgaclk->hw.reg) & 0x7ff) + 1);
	}

	return parent_rate / div;
}

static u8 clk_periclk_get_parent(struct clk_hw *hwclk)
{
	struct socfpga_periph_clk *socfpgaclk = to_socfpga_periph_clk(hwclk);
	u32 clk_src;

	clk_src = readl(socfpgaclk->hw.reg);
	if (streq(hwclk->init->name, SOCFPGA_MPU_FREE_CLK) ||
	    streq(hwclk->init->name, SOCFPGA_NOC_FREE_CLK) ||
	    streq(hwclk->init->name, SOCFPGA_SDMMC_FREE_CLK))
		return (clk_src >> CLK_MGR_FREE_SHIFT) &
			CLK_MGR_FREE_MASK;
	else
		return 0;
}

static const struct clk_ops periclk_ops = {
	.recalc_rate = clk_periclk_recalc_rate,
	.get_parent = clk_periclk_get_parent,
};

static __init void __socfpga_periph_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct socfpga_periph_clk *periph_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFPGA_MAX_PARENTS];
	struct clk_init_data init = {};
	int rc;
	u32 fixed_div;
	u32 div_reg[3];

	of_property_read_u32(node, "reg", &reg);

	periph_clk = kzalloc(sizeof(*periph_clk), GFP_KERNEL);
	if (WARN_ON(!periph_clk))
		return;

	periph_clk->hw.reg = clk_mgr_a10_base_addr + reg;

	rc = of_property_read_u32_array(node, "div-reg", div_reg, 3);
	if (!rc) {
		periph_clk->div_reg = clk_mgr_a10_base_addr + div_reg[0];
		periph_clk->shift = div_reg[1];
		periph_clk->width = div_reg[2];
	} else {
		periph_clk->div_reg = NULL;
	}

	rc = of_property_read_u32(node, "fixed-divider", &fixed_div);
	if (rc)
		periph_clk->fixed_div = 0;
	else
		periph_clk->fixed_div = fixed_div;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;

	init.num_parents = of_clk_parent_fill(node, parent_name, SOCFPGA_MAX_PARENTS);
	init.parent_names = parent_name;

	periph_clk->hw.hw.init = &init;

	clk = clk_register(NULL, &periph_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(periph_clk);
		return;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (rc < 0) {
		pr_err("Could not register clock provider for node:%s\n",
		       clk_name);
		goto err_clk;
	}

	return;

err_clk:
	clk_unregister(clk);
}

void __init socfpga_a10_periph_init(struct device_node *node)
{
	__socfpga_periph_init(node, &periclk_ops);
}

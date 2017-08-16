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
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "clk.h"

#define streq(a, b) (strcmp((a), (b)) == 0)

#define to_socfpga_gate_clk(p) container_of(p, struct socfpga_gate_clk, hw.hw)

/* SDMMC Group for System Manager defines */
#define SYSMGR_SDMMCGRP_CTRL_OFFSET	0x28

static unsigned long socfpga_gate_clk_recalc_rate(struct clk_hw *hwclk,
	unsigned long parent_rate)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	u32 div = 1, val;

	if (socfpgaclk->fixed_div)
		div = socfpgaclk->fixed_div;
	else if (socfpgaclk->div_reg) {
		val = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
		val &= GENMASK(socfpgaclk->width - 1, 0);
		div = (1 << val);
	}

	return parent_rate / div;
}

static int socfpga_clk_prepare(struct clk_hw *hwclk)
{
	struct socfpga_gate_clk *socfpgaclk = to_socfpga_gate_clk(hwclk);
	int i;
	u32 hs_timing;
	u32 clk_phase[2];

	if (socfpgaclk->clk_phase[0] || socfpgaclk->clk_phase[1]) {
		for (i = 0; i < ARRAY_SIZE(clk_phase); i++) {
			switch (socfpgaclk->clk_phase[i]) {
			case 0:
				clk_phase[i] = 0;
				break;
			case 45:
				clk_phase[i] = 1;
				break;
			case 90:
				clk_phase[i] = 2;
				break;
			case 135:
				clk_phase[i] = 3;
				break;
			case 180:
				clk_phase[i] = 4;
				break;
			case 225:
				clk_phase[i] = 5;
				break;
			case 270:
				clk_phase[i] = 6;
				break;
			case 315:
				clk_phase[i] = 7;
				break;
			default:
				clk_phase[i] = 0;
				break;
			}
		}

		hs_timing = SYSMGR_SDMMC_CTRL_SET_AS10(clk_phase[0], clk_phase[1]);
		if (!IS_ERR(socfpgaclk->sys_mgr_base_addr))
			regmap_write(socfpgaclk->sys_mgr_base_addr,
				     SYSMGR_SDMMCGRP_CTRL_OFFSET, hs_timing);
		else
			pr_err("%s: cannot set clk_phase because sys_mgr_base_addr is not available!\n",
					__func__);
	}
	return 0;
}

static struct clk_ops gateclk_ops = {
	.prepare = socfpga_clk_prepare,
	.recalc_rate = socfpga_gate_clk_recalc_rate,
};

static void __init __socfpga_gate_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 clk_gate[2];
	u32 div_reg[3];
	u32 clk_phase[2];
	u32 fixed_div;
	struct clk *clk;
	struct socfpga_gate_clk *socfpga_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFPGA_MAX_PARENTS];
	struct clk_init_data init;
	int rc;

	socfpga_clk = kzalloc(sizeof(*socfpga_clk), GFP_KERNEL);
	if (WARN_ON(!socfpga_clk))
		return;

	rc = of_property_read_u32_array(node, "clk-gate", clk_gate, 2);
	if (rc)
		clk_gate[0] = 0;

	if (clk_gate[0]) {
		socfpga_clk->hw.reg = clk_mgr_a10_base_addr + clk_gate[0];
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
		socfpga_clk->div_reg = clk_mgr_a10_base_addr + div_reg[0];
		socfpga_clk->shift = div_reg[1];
		socfpga_clk->width = div_reg[2];
	} else {
		socfpga_clk->div_reg = NULL;
	}

	rc = of_property_read_u32_array(node, "clk-phase", clk_phase, 2);
	if (!rc) {
		socfpga_clk->clk_phase[0] = clk_phase[0];
		socfpga_clk->clk_phase[1] = clk_phase[1];

		socfpga_clk->sys_mgr_base_addr =
			syscon_regmap_lookup_by_compatible("altr,sys-mgr");
		if (IS_ERR(socfpga_clk->sys_mgr_base_addr)) {
			pr_err("%s: failed to find altr,sys-mgr regmap!\n",
					__func__);
			return;
		}
	}

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;

	init.num_parents = of_clk_parent_fill(node, parent_name, SOCFPGA_MAX_PARENTS);
	init.parent_names = parent_name;
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

void __init socfpga_a10_gate_init(struct device_node *node)
{
	__socfpga_gate_init(node, &gateclk_ops);
}

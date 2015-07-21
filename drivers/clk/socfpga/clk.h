/*
 * Copyright (c) 2013, Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * based on drivers/clk/tegra/clk.h
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __SOCFPGA_CLK_H
#define __SOCFPGA_CLK_H

#include <linux/clk-provider.h>

/* Clock Manager offsets */
#define CLKMGR_CTRL		0x0
#define CLKMGR_BYPASS		0x4
#define CLKMGR_L4SRC		0x70
#define CLKMGR_PERPLL_SRC	0xAC

#define SOCFPGA_MAX_PARENTS		5
#define div_mask(width) ((1 << (width)) - 1)

#define streq(a, b) (strcmp((a), (b)) == 0)
#define SYSMGR_SDMMC_CTRL_SET(smplsel, drvsel) \
	((((smplsel) & 0x7) << 3) | (((drvsel) & 0x7) << 0))

extern void __iomem *clk_mgr_base_addr;
extern void __iomem *clk_mgr_a10_base_addr;

void __init socfpga_pll_init(struct device_node *node);
void __init socfpga_periph_init(struct device_node *node);
void __init socfpga_gate_init(struct device_node *node);
void socfpga_a10_pll_init(struct device_node *node);
void socfpga_a10_periph_init(struct device_node *node);
void socfpga_a10_gate_init(struct device_node *node);

struct socfpga_pll {
	struct clk_gate	hw;
};

struct socfpga_gate_clk {
	struct clk_gate hw;
	char *parent_name;
	u32 fixed_div;
	void __iomem *div_reg;
	struct regmap *sys_mgr_base_addr;
	u32 width;	/* only valid if div_reg != 0 */
	u32 shift;	/* only valid if div_reg != 0 */
	u32 clk_phase[2];
};

struct socfpga_periph_clk {
	struct clk_gate hw;
	char *parent_name;
	u32 fixed_div;
	void __iomem *div_reg;
	u32 width;      /* only valid if div_reg != 0 */
	u32 shift;      /* only valid if div_reg != 0 */
};

#endif /* SOCFPGA_CLK_H */

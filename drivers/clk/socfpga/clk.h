/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013, Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * based on drivers/clk/tegra/clk.h
 */

#ifndef __SOCFPGA_CLK_H
#define __SOCFPGA_CLK_H

#include <linux/clk-provider.h>

/* Clock Manager offsets */
#define CLKMGR_CTRL		0x0
#define CLKMGR_BYPASS		0x4
#define CLKMGR_DBCTRL		0x10
#define CLKMGR_L4SRC		0x70
#define CLKMGR_PERPLL_SRC	0xAC

#define SOCFPGA_MAX_PARENTS		5

#define streq(a, b) (strcmp((a), (b)) == 0)
#define SYSMGR_SDMMC_CTRL_SET(smplsel, drvsel) \
	((((smplsel) & 0x7) << 3) | (((drvsel) & 0x7) << 0))

#define SYSMGR_SDMMC_CTRL_SET_AS10(smplsel, drvsel) \
	((((smplsel) & 0x7) << 4) | (((drvsel) & 0x7) << 0))

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
	void __iomem *bypass_reg;
	struct regmap *sys_mgr_base_addr;
	u32 width;	/* only valid if div_reg != 0 */
	u32 shift;	/* only valid if div_reg != 0 */
	u32 bypass_shift;      /* only valid if bypass_reg != 0 */
};

struct socfpga_periph_clk {
	struct clk_gate hw;
	char *parent_name;
	u32 fixed_div;
	void __iomem *div_reg;
	void __iomem *bypass_reg;
	u32 width;      /* only valid if div_reg != 0 */
	u32 shift;      /* only valid if div_reg != 0 */
	u32 bypass_shift;      /* only valid if bypass_reg != 0 */
};

#endif /* SOCFPGA_CLK_H */

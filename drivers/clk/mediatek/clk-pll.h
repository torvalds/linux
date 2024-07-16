/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#ifndef __DRV_CLK_MTK_PLL_H
#define __DRV_CLK_MTK_PLL_H

#include <linux/types.h>

struct clk_ops;
struct clk_hw_onecell_data;
struct device_node;

struct mtk_pll_div_table {
	u32 div;
	unsigned long freq;
};

#define HAVE_RST_BAR	BIT(0)
#define PLL_AO		BIT(1)

struct mtk_pll_data {
	int id;
	const char *name;
	u32 reg;
	u32 pwr_reg;
	u32 en_mask;
	u32 pd_reg;
	u32 tuner_reg;
	u32 tuner_en_reg;
	u8 tuner_en_bit;
	int pd_shift;
	unsigned int flags;
	const struct clk_ops *ops;
	u32 rst_bar_mask;
	unsigned long fmin;
	unsigned long fmax;
	int pcwbits;
	int pcwibits;
	u32 pcw_reg;
	int pcw_shift;
	u32 pcw_chg_reg;
	const struct mtk_pll_div_table *div_table;
	const char *parent_name;
	u32 en_reg;
	u8 pll_en_bit; /* Assume 0, indicates BIT(0) by default */
};

int mtk_clk_register_plls(struct device_node *node,
			  const struct mtk_pll_data *plls, int num_plls,
			  struct clk_hw_onecell_data *clk_data);
void mtk_clk_unregister_plls(const struct mtk_pll_data *plls, int num_plls,
			     struct clk_hw_onecell_data *clk_data);

#endif /* __DRV_CLK_MTK_PLL_H */

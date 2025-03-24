/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef __MESON_CLK_PLL_H
#define __MESON_CLK_PLL_H

#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include "parm.h"

struct pll_params_table {
	unsigned int	m;
	unsigned int	n;
};

struct pll_mult_range {
	unsigned int	min;
	unsigned int	max;
};

#define PLL_PARAMS(_m, _n)						\
	{								\
		.m		= (_m),					\
		.n		= (_n),					\
	}

#define CLK_MESON_PLL_ROUND_CLOSEST	BIT(0)
#define CLK_MESON_PLL_NOINIT_ENABLED	BIT(1)

struct meson_clk_pll_data {
	struct parm en;
	struct parm m;
	struct parm n;
	struct parm frac;
	struct parm l;
	struct parm rst;
	struct parm current_en;
	struct parm l_detect;
	const struct reg_sequence *init_regs;
	unsigned int init_count;
	const struct pll_params_table *table;
	const struct pll_mult_range *range;
	unsigned int frac_max;
	u8 flags;
};

extern const struct clk_ops meson_clk_pll_ro_ops;
extern const struct clk_ops meson_clk_pll_ops;
extern const struct clk_ops meson_clk_pcie_pll_ops;

#endif /* __MESON_CLK_PLL_H */

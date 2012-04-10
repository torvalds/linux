/*
 * Clock framework definitions for SPEAr platform
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __SPEAR_CLK_H
#define __SPEAR_CLK_H

#include <linux/clk-provider.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

/* VCO-PLL clk */
struct pll_rate_tbl {
	u8 mode;
	u16 m;
	u8 n;
	u8 p;
};

struct clk_vco {
	struct			clk_hw hw;
	void __iomem		*mode_reg;
	void __iomem		*cfg_reg;
	struct pll_rate_tbl	*rtbl;
	u8			rtbl_cnt;
	spinlock_t		*lock;
};

struct clk_pll {
	struct			clk_hw hw;
	struct clk_vco		*vco;
	const char		*parent[1];
	spinlock_t		*lock;
};

typedef unsigned long (*clk_calc_rate)(struct clk_hw *hw, unsigned long prate,
		int index);

/* clk register routines */
struct clk *clk_register_vco_pll(const char *vco_name, const char *pll_name,
		const char *vco_gate_name, const char *parent_name,
		unsigned long flags, void __iomem *mode_reg, void __iomem
		*cfg_reg, struct pll_rate_tbl *rtbl, u8 rtbl_cnt,
		spinlock_t *lock, struct clk **pll_clk,
		struct clk **vco_gate_clk);

long clk_round_rate_index(struct clk_hw *hw, unsigned long drate,
		unsigned long parent_rate, clk_calc_rate calc_rate, u8 rtbl_cnt,
		int *index);

#endif /* __SPEAR_CLK_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Clock framework definitions for SPEAr platform
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 */

#ifndef __SPEAR_CLK_H
#define __SPEAR_CLK_H

#include <linux/clk-provider.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

/* Auxiliary Synth clk */
/* Default masks */
#define AUX_EQ_SEL_SHIFT	30
#define AUX_EQ_SEL_MASK		1
#define AUX_EQ1_SEL		0
#define AUX_EQ2_SEL		1
#define AUX_XSCALE_SHIFT	16
#define AUX_XSCALE_MASK		0xFFF
#define AUX_YSCALE_SHIFT	0
#define AUX_YSCALE_MASK		0xFFF
#define AUX_SYNT_ENB		31

struct aux_clk_masks {
	u32 eq_sel_mask;
	u32 eq_sel_shift;
	u32 eq1_mask;
	u32 eq2_mask;
	u32 xscale_sel_mask;
	u32 xscale_sel_shift;
	u32 yscale_sel_mask;
	u32 yscale_sel_shift;
	u32 enable_bit;
};

struct aux_rate_tbl {
	u16 xscale;
	u16 yscale;
	u8 eq;
};

struct clk_aux {
	struct			clk_hw hw;
	void __iomem		*reg;
	const struct aux_clk_masks *masks;
	struct aux_rate_tbl	*rtbl;
	u8			rtbl_cnt;
	spinlock_t		*lock;
};

/* Fractional Synth clk */
struct frac_rate_tbl {
	u32 div;
};

struct clk_frac {
	struct			clk_hw hw;
	void __iomem		*reg;
	struct frac_rate_tbl	*rtbl;
	u8			rtbl_cnt;
	spinlock_t		*lock;
};

/* GPT clk */
struct gpt_rate_tbl {
	u16 mscale;
	u16 nscale;
};

struct clk_gpt {
	struct			clk_hw hw;
	void __iomem		*reg;
	struct gpt_rate_tbl	*rtbl;
	u8			rtbl_cnt;
	spinlock_t		*lock;
};

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
struct clk *clk_register_aux(const char *aux_name, const char *gate_name,
		const char *parent_name, unsigned long flags, void __iomem *reg,
		const struct aux_clk_masks *masks, struct aux_rate_tbl *rtbl,
		u8 rtbl_cnt, spinlock_t *lock, struct clk **gate_clk);
struct clk *clk_register_frac(const char *name, const char *parent_name,
		unsigned long flags, void __iomem *reg,
		struct frac_rate_tbl *rtbl, u8 rtbl_cnt, spinlock_t *lock);
struct clk *clk_register_gpt(const char *name, const char *parent_name, unsigned
		long flags, void __iomem *reg, struct gpt_rate_tbl *rtbl, u8
		rtbl_cnt, spinlock_t *lock);
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

/*
 * arch/arm/plat-spear/include/plat/clock.h
 *
 * Clock framework definitions for SPEAr platform
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_CLOCK_H
#define __PLAT_CLOCK_H

#include <linux/list.h>
#include <linux/clkdev.h>
#include <linux/types.h>

/* clk structure flags */
#define	ALWAYS_ENABLED		(1 << 0) /* clock always enabled */
#define	RESET_TO_ENABLE		(1 << 1) /* reset register bit to enable clk */
#define	ENABLED_ON_INIT		(1 << 2) /* clocks enabled at init */

/**
 * struct clkops - clock operations
 * @enable: pointer to clock enable function
 * @disable: pointer to clock disable function
 */
struct clkops {
	int (*enable) (struct clk *);
	void (*disable) (struct clk *);
};

/**
 * struct pclk_info - parents info
 * @pclk: pointer to parent clk
 * @pclk_val: value to be written for selecting this parent
 */
struct pclk_info {
	struct clk *pclk;
	u8 pclk_val;
};

/**
 * struct pclk_sel - parents selection configuration
 * @pclk_info: pointer to array of parent clock info
 * @pclk_count: number of parents
 * @pclk_sel_reg: register for selecting a parent
 * @pclk_sel_mask: mask for selecting parent (can be used to clear bits also)
 */
struct pclk_sel {
	struct pclk_info *pclk_info;
	u8 pclk_count;
	void __iomem *pclk_sel_reg;
	unsigned int pclk_sel_mask;
};

/**
 * struct rate_config - clk rate configurations
 * @tbls: array of device specific clk rate tables, in ascending order of rates
 * @count: size of tbls array
 * @default_index: default setting when originally disabled
 */
struct rate_config {
	void *tbls;
	u8 count;
	u8 default_index;
};

/**
 * struct clk - clock structure
 * @usage_count: num of users who enabled this clock
 * @flags: flags for clock properties
 * @rate: programmed clock rate in Hz
 * @en_reg: clk enable/disable reg
 * @en_reg_bit: clk enable/disable bit
 * @ops: clk enable/disable ops - generic_clkops selected if NULL
 * @recalc: pointer to clock rate recalculate function
 * @set_rate: pointer to clock set rate function
 * @calc_rate: pointer to clock get rate function for index
 * @rate_config: rate configuration information, used by set_rate
 * @div_factor: division factor to parent clock.
 * @pclk: current parent clk
 * @pclk_sel: pointer to parent selection structure
 * @pclk_sel_shift: register shift for selecting parent of this clock
 * @children: list for childrens or this clock
 * @sibling: node for list of clocks having same parents
 * @private_data: clock specific private data
 * @node: list to maintain clocks linearly
 * @cl: clocklook up associated with this clock
 * @dent: object for debugfs
 */
struct clk {
	unsigned int usage_count;
	unsigned int flags;
	unsigned long rate;
	void __iomem *en_reg;
	u8 en_reg_bit;
	const struct clkops *ops;
	int (*recalc) (struct clk *);
	int (*set_rate) (struct clk *, unsigned long rate);
	unsigned long (*calc_rate)(struct clk *, int index);
	struct rate_config rate_config;
	unsigned int div_factor;

	struct clk *pclk;
	struct pclk_sel *pclk_sel;
	unsigned int pclk_sel_shift;

	struct list_head children;
	struct list_head sibling;
	void *private_data;
#ifdef CONFIG_DEBUG_FS
	struct list_head node;
	struct clk_lookup *cl;
	struct dentry *dent;
#endif
};

/* pll configuration structure */
struct pll_clk_masks {
	u32 mode_mask;
	u32 mode_shift;

	u32 norm_fdbk_m_mask;
	u32 norm_fdbk_m_shift;
	u32 dith_fdbk_m_mask;
	u32 dith_fdbk_m_shift;
	u32 div_p_mask;
	u32 div_p_shift;
	u32 div_n_mask;
	u32 div_n_shift;
};

struct pll_clk_config {
	void __iomem *mode_reg;
	void __iomem *cfg_reg;
	struct pll_clk_masks *masks;
};

/* pll clk rate config structure */
struct pll_rate_tbl {
	u8 mode;
	u16 m;
	u8 n;
	u8 p;
};

/* ahb and apb bus configuration structure */
struct bus_clk_masks {
	u32 mask;
	u32 shift;
};

struct bus_clk_config {
	void __iomem *reg;
	struct bus_clk_masks *masks;
};

/* ahb and apb clk bus rate config structure */
struct bus_rate_tbl {
	u8 div;
};

/* Aux clk configuration structure: applicable to UART and FIRDA */
struct aux_clk_masks {
	u32 eq_sel_mask;
	u32 eq_sel_shift;
	u32 eq1_mask;
	u32 eq2_mask;
	u32 xscale_sel_mask;
	u32 xscale_sel_shift;
	u32 yscale_sel_mask;
	u32 yscale_sel_shift;
};

struct aux_clk_config {
	void __iomem *synth_reg;
	struct aux_clk_masks *masks;
};

/* aux clk rate config structure */
struct aux_rate_tbl {
	u16 xscale;
	u16 yscale;
	u8 eq;
};

/* GPT clk configuration structure */
struct gpt_clk_masks {
	u32 mscale_sel_mask;
	u32 mscale_sel_shift;
	u32 nscale_sel_mask;
	u32 nscale_sel_shift;
};

struct gpt_clk_config {
	void __iomem *synth_reg;
	struct gpt_clk_masks *masks;
};

/* gpt clk rate config structure */
struct gpt_rate_tbl {
	u16 mscale;
	u16 nscale;
};

/* clcd clk configuration structure */
struct clcd_synth_masks {
	u32 div_factor_mask;
	u32 div_factor_shift;
};

struct clcd_clk_config {
	void __iomem *synth_reg;
	struct clcd_synth_masks *masks;
};

/* clcd clk rate config structure */
struct clcd_rate_tbl {
	u16 div;
};

/* platform specific clock functions */
void clk_register(struct clk_lookup *cl);
void recalc_root_clocks(void);

/* clock recalc & set rate functions */
int follow_parent(struct clk *clk);
unsigned long pll_calc_rate(struct clk *clk, int index);
int pll_clk_recalc(struct clk *clk);
int pll_clk_set_rate(struct clk *clk, unsigned long desired_rate);
unsigned long bus_calc_rate(struct clk *clk, int index);
int bus_clk_recalc(struct clk *clk);
int bus_clk_set_rate(struct clk *clk, unsigned long desired_rate);
unsigned long gpt_calc_rate(struct clk *clk, int index);
int gpt_clk_recalc(struct clk *clk);
int gpt_clk_set_rate(struct clk *clk, unsigned long desired_rate);
unsigned long aux_calc_rate(struct clk *clk, int index);
int aux_clk_recalc(struct clk *clk);
int aux_clk_set_rate(struct clk *clk, unsigned long desired_rate);
unsigned long clcd_calc_rate(struct clk *clk, int index);
int clcd_clk_recalc(struct clk *clk);
int clcd_clk_set_rate(struct clk *clk, unsigned long desired_rate);

#endif /* __PLAT_CLOCK_H */

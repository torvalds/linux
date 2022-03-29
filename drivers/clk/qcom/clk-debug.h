/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016, 2019-2021, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_CLK_DEBUG_H__
#define __QCOM_CLK_DEBUG_H__

#include <linux/platform_device.h>
#include "../clk.h"

/**
 * struct mux_regmap_names - Structure of mux regmap mapping
 * @mux:		pointer to a clock debug mux
 * @regmap_name:	corresponding regmap name used to match a debug mux to
			its regmap
 */
struct mux_regmap_names {
	struct clk_debug_mux *mux;
	const char *regmap_name;
};

/* Debugfs Measure Clocks */

/**
 * struct measure_clk_data - Structure of clk measure
 *
 * @cxo:		XO clock.
 * @xo_div4_cbcr:	offset of debug XO/4 div register.
 * @ctl_reg:		offset of debug control register.
 * @status_reg:		offset of debug status register.
 * @cbcr_offset:	branch register to turn on debug mux.
 */
struct measure_clk_data {
	struct clk *cxo;
	u32 ctl_reg;
	u32 status_reg;
	u32 xo_div4_cbcr;
};

/**
 * struct clk_debug_mux - Structure of clock debug mux
 *
 * @mux_sels:		indicates the debug mux index at recursive debug mux.
 * @pre_div_val:	optional divider values for clocks that were pre-divided
			before feeding into the debug muxes
 * @num_parents:	number of parents
 * @regmap:		regmaps of debug mux
 * @priv:		private measure_clk_data to be used by debug mux
 * @en_mask:		indicates the enable bit mask at global clock
 *			controller debug mux.
 * @debug_offset:	debug mux offset.
 * @post_div_offset:	register with post-divider settings for the debug mux.
 * @cbcr_offset:	branch register to turn on debug mux.
 * @src_sel_mask:	indicates the mask to be used for src selection in
			primary mux.
 * @src_sel_shift:	indicates the shift required for source selection in
			primary mux.
 * @post_div_mask:	indicates the post div mask to be used for the primary
			mux.
 * @post_div_shift:	indicates the shift required for post divider
			selection in primary mux.
 * @period_offset:	offset of the period register used to read to determine
			the mc clock period
 * @hw:			handle between common and hardware-specific interfaces.
 */
struct clk_debug_mux {
	int *mux_sels;
	int num_mux_sels;
	int *pre_div_vals;
	int num_parents;
	struct regmap *regmap;
	void *priv;
	u32 en_mask;
	u32 debug_offset;
	u32 cbcr_offset;
	u32 src_sel_mask;
	u32 src_sel_shift;
	u32 post_div_offset;
	u32 post_div_mask;
	u32 post_div_shift;
	u32 post_div_val;
	u32 period_offset;
	struct clk_hw hw;
	struct list_head list;
};

#define to_clk_measure(_hw) container_of((_hw), struct clk_debug_mux, hw)

extern const struct clk_ops clk_debug_mux_ops;

int clk_debug_measure_register(struct clk_hw *hw);
int devm_clk_register_debug_mux(struct device *pdev, struct clk_debug_mux *mux);
void clk_debug_measure_add(struct clk_hw *hw, struct dentry *dentry);
int map_debug_bases(struct platform_device *pdev, const char *base,
		    struct clk_debug_mux *mux);

void clk_common_debug_init(struct clk_hw *hw, struct dentry *dentry);

/* hw debug registration */
int clk_hw_debug_register(struct device *dev, struct clk_hw *clk_hw);
int clk_debug_init(void);
void clk_debug_exit(void);
extern void clk_debug_print_hw(struct clk_hw *hw, struct seq_file *f);

#define WARN_CLK(hw, cond, fmt, ...)						\
	do {									\
		clk_debug_print_hw(hw, NULL);					\
		WARN(cond, "%s: " fmt, qcom_clk_hw_get_name(hw), ##__VA_ARGS__);	\
	} while (0)

#define clock_debug_output(m, fmt, ...)			\
	do {							\
		if (m)						\
			seq_printf(m, fmt, ##__VA_ARGS__);	\
		else						\
			pr_info(fmt, ##__VA_ARGS__);		\
	} while (0)

#define clock_debug_output_cont(s, fmt, ...)			\
	do {							\
		if (s)						\
			seq_printf(s, fmt, ##__VA_ARGS__);	\
		else						\
			pr_cont(fmt, ##__VA_ARGS__);		\
	} while (0)

#endif

/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */

#ifndef __MESON_CLKC_UTILS_H__
#define __MESON_CLKC_UTILS_H__

#include <linux/of_device.h>
#include <linux/clk-provider.h>

struct platform_device;

struct meson_clk_hw_data {
	struct clk_hw	**hws;
	unsigned int	num;
};

struct clk_hw *meson_clk_hw_get(struct of_phandle_args *clkspec, void *clk_hw_data);

struct meson_clkc_data {
	const struct reg_sequence	*init_regs;
	unsigned int			init_count;
	struct meson_clk_hw_data	hw_clks;
};

int meson_clkc_syscon_probe(struct platform_device *pdev);
int meson_clkc_mmio_probe(struct platform_device *pdev);

#define __MESON_PCLK(_name, _reg, _bit, _ops, _pdata, _flags)		\
struct clk_regmap _name = {						\
	.data = &(struct clk_regmap_gate_data) {			\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = _ops,						\
		.parent_data = (_pdata),				\
		.num_parents = 1,					\
		.flags = (_flags),					\
	},								\
}

#define MESON_PCLK(_name, _reg, _bit, _pdata, _flags)			\
	__MESON_PCLK(_name, _reg, _bit, &clk_regmap_gate_ops, _pdata, _flags)

#define MESON_PCLK_RO(_name, _reg, _bit, _pdata, _flags)		\
	__MESON_PCLK(_name, _reg, _bit, &clk_regmap_gate_ro_ops, _pdata, _flags)

/* Helpers for the usual sel/div/gate composite clocks */
#define MESON_COMP_SEL(_prefix, _name, _reg, _shift, _mask, _pdata,	\
		       _table, _dflags, _iflags)			\
struct clk_regmap _prefix##_name##_sel = {				\
	.data = &(struct clk_regmap_mux_data) {				\
		.offset = (_reg),					\
		.mask = (_mask),					\
		.shift = (_shift),					\
		.flags = (_dflags),					\
		.table = (_table),					\
	},								\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name "_sel",					\
		.ops = &clk_regmap_mux_ops,				\
		.parent_data = _pdata,					\
		.num_parents = ARRAY_SIZE(_pdata),			\
		.flags = (_iflags),					\
	},								\
}

#define MESON_COMP_DIV(_prefix, _name, _reg, _shift, _width,		\
		       _dflags, _iflags)				\
struct clk_regmap _prefix##_name##_div = {				\
	.data = &(struct clk_regmap_div_data) {				\
		.offset = (_reg),					\
		.shift = (_shift),					\
		.width = (_width),					\
		.flags = (_dflags),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name "_div",					\
		.ops = &clk_regmap_divider_ops,				\
		.parent_hws = (const struct clk_hw *[]) {		\
			&_prefix##_name##_sel.hw			\
		},							\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define MESON_COMP_GATE(_prefix, _name, _reg, _bit, _iflags)		\
struct clk_regmap _prefix##_name = {					\
	.data = &(struct clk_regmap_gate_data) {			\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = &clk_regmap_gate_ops,				\
		.parent_hws = (const struct clk_hw *[]) {		\
			&_prefix##_name##_div.hw			\
		},							\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#endif

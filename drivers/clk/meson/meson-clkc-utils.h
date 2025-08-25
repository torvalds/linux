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

#define __MESON_PCLK(_name, _reg, _bit, _ops, _pname)			\
struct clk_regmap _name = {						\
	.data = &(struct clk_regmap_gate_data) {			\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = _ops,						\
		.parent_hws = (const struct clk_hw *[]) { _pname },	\
		.num_parents = 1,					\
		.flags = CLK_IGNORE_UNUSED,				\
	},								\
}

#define MESON_PCLK(_name, _reg, _bit, _pname)	\
	__MESON_PCLK(_name, _reg, _bit, &clk_regmap_gate_ops, _pname)

#define MESON_PCLK_RO(_name, _reg, _bit, _pname)	\
	__MESON_PCLK(_name, _reg, _bit, &clk_regmap_gate_ro_ops, _pname)

#endif

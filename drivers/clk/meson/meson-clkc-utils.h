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

#endif

/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */

#ifndef __MESON_CLKC_UTILS_H__
#define __MESON_CLKC_UTILS_H__

#include <linux/of_device.h>
#include <linux/clk-provider.h>

struct meson_clk_hw_data {
	struct clk_hw	**hws;
	unsigned int	num;
};

struct clk_hw *meson_clk_hw_get(struct of_phandle_args *clkspec, void *clk_hw_data);

#endif

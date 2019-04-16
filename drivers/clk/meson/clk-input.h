/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef __MESON_CLK_INPUT_H
#define __MESON_CLK_INPUT_H

#include <linux/clk-provider.h>

struct device;

struct clk_hw *meson_clk_hw_register_input(struct device *dev,
					   const char *of_name,
					   const char *clk_name,
					   unsigned long flags);

#endif /* __MESON_CLK_INPUT_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef __MESON_CLKC_H
#define __MESON_CLKC_H

#include <linux/clk-provider.h>
#include "clk-regmap.h"

#define IN_PREFIX "ee-in-"

struct platform_device;

struct meson_eeclkc_data {
	struct clk_regmap *const	*regmap_clks;
	unsigned int			regmap_clk_num;
	const struct reg_sequence	*init_regs;
	unsigned int			init_count;
	struct clk_hw_onecell_data	*hw_onecell_data;
};

int meson_eeclkc_probe(struct platform_device *pdev);

#endif /* __MESON_CLKC_H */

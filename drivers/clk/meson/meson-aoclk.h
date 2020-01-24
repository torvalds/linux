/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Copyright (c) 2018 Amlogic, inc.
 * Author: Qiufang Dai <qiufang.dai@amlogic.com>
 * Author: Yixun Lan <yixun.lan@amlogic.com>
 */

#ifndef __MESON_AOCLK_H__
#define __MESON_AOCLK_H__

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include "clk-regmap.h"

struct meson_aoclk_data {
	const unsigned int			reset_reg;
	const int				num_reset;
	const unsigned int			*reset;
	const int				num_clks;
	struct clk_regmap			**clks;
	const struct clk_hw_onecell_data	*hw_data;
};

struct meson_aoclk_reset_controller {
	struct reset_controller_dev		reset;
	const struct meson_aoclk_data		*data;
	struct regmap				*regmap;
};

int meson_aoclkc_probe(struct platform_device *pdev);
#endif

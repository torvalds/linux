// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Stefan Dösinger
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/regmap.h>

#include "clk-zx.h"

struct clk_hw *zx_clk_register_pll(struct device *dev, struct regmap *regmap,
				   const struct zx_pll_desc *desc,
				   struct clk_hw * const *clocks)
{
	return ERR_PTR(-ENODEV);
}

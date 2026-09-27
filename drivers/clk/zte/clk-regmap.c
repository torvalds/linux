// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Stefan Dösinger
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/regmap.h>

#include "clk-zx.h"

struct clk_hw *zx_clk_register_gate(struct device *dev, struct regmap *regmap,
				    const struct zx_gate_desc *desc,
				    struct clk_hw * const *clocks)
{
	return ERR_PTR(-ENODEV);
}

struct clk_hw *zx_clk_register_divider(struct device *dev, struct regmap *regmap,
				       const struct zx_div_desc *desc,
				       struct clk_hw * const *clocks)
{
	return ERR_PTR(-ENODEV);
}

struct clk_hw *zx_clk_register_mux(struct device *dev, struct regmap *regmap,
				   const struct zx_mux_desc *desc,
				   struct clk_hw * const *clocks)
{
	return ERR_PTR(-ENODEV);
}

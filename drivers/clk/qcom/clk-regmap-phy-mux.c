// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Linaro Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/export.h>

#include "clk-regmap.h"
#include "clk-regmap-phy-mux.h"

#define PHY_MUX_MASK		GENMASK(1, 0)
#define PHY_MUX_PHY_SRC		0
#define PHY_MUX_REF_SRC		2

static inline struct clk_regmap_phy_mux *to_clk_regmap_phy_mux(struct clk_regmap *clkr)
{
	return container_of(clkr, struct clk_regmap_phy_mux, clkr);
}

static int phy_mux_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_regmap_phy_mux *phy_mux = to_clk_regmap_phy_mux(clkr);
	unsigned int val;

	regmap_read(clkr->regmap, phy_mux->reg, &val);
	val = FIELD_GET(PHY_MUX_MASK, val);

	WARN_ON(val != PHY_MUX_PHY_SRC && val != PHY_MUX_REF_SRC);

	return val == PHY_MUX_PHY_SRC;
}

static int phy_mux_enable(struct clk_hw *hw)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_regmap_phy_mux *phy_mux = to_clk_regmap_phy_mux(clkr);

	return regmap_update_bits(clkr->regmap, phy_mux->reg,
				  PHY_MUX_MASK,
				  FIELD_PREP(PHY_MUX_MASK, PHY_MUX_PHY_SRC));
}

static void phy_mux_disable(struct clk_hw *hw)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_regmap_phy_mux *phy_mux = to_clk_regmap_phy_mux(clkr);

	regmap_update_bits(clkr->regmap, phy_mux->reg,
			   PHY_MUX_MASK,
			   FIELD_PREP(PHY_MUX_MASK, PHY_MUX_REF_SRC));
}

const struct clk_ops clk_regmap_phy_mux_ops = {
	.enable = phy_mux_enable,
	.disable = phy_mux_disable,
	.is_enabled = phy_mux_is_enabled,
};
EXPORT_SYMBOL_GPL(clk_regmap_phy_mux_ops);

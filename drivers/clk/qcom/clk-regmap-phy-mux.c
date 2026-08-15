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

#define XO_RATE			19200000UL

static inline struct clk_regmap_phy_mux *to_clk_regmap_phy_mux(struct clk_regmap *clkr)
{
	return container_of(clkr, struct clk_regmap_phy_mux, clkr);
}

static unsigned long phy_mux_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_regmap_phy_mux *phy_mux = to_clk_regmap_phy_mux(clkr);
	u32 val;

	regmap_read(clkr->regmap, phy_mux->reg, &val);

	switch (FIELD_GET(PHY_MUX_MASK, val)) {
	case PHY_MUX_PHY_SRC:
		return ULONG_MAX;
	case PHY_MUX_REF_SRC:
		return XO_RATE;
	default:
		return 0;
	}
}

static int phy_mux_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	if (req->rate == XO_RATE || req->rate == ULONG_MAX)
		return 0;

	return -EINVAL;
}

static int phy_mux_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_regmap_phy_mux *phy_mux = to_clk_regmap_phy_mux(clkr);
	u32 val;

	switch (rate) {
	case XO_RATE:
		val = PHY_MUX_REF_SRC;
		break;
	case ULONG_MAX:
		val = PHY_MUX_PHY_SRC;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(clkr->regmap, phy_mux->reg,
			   PHY_MUX_MASK,
			   FIELD_PREP(PHY_MUX_MASK, val));

	return 0;
}

const struct clk_ops clk_regmap_phy_mux_ops = {
	.recalc_rate = phy_mux_recalc_rate,
	.determine_rate = phy_mux_determine_rate,
	.set_rate = phy_mux_set_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_phy_mux_ops);

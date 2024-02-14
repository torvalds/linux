// SPDX-License-Identifier: GPL-2.0+
//
// OWL mux clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include "owl-mux.h"

u8 owl_mux_helper_get_parent(const struct owl_clk_common *common,
			     const struct owl_mux_hw *mux_hw)
{
	u32 reg;
	u8 parent;

	regmap_read(common->regmap, mux_hw->reg, &reg);
	parent = reg >> mux_hw->shift;
	parent &= BIT(mux_hw->width) - 1;

	return parent;
}

static u8 owl_mux_get_parent(struct clk_hw *hw)
{
	struct owl_mux *mux = hw_to_owl_mux(hw);

	return owl_mux_helper_get_parent(&mux->common, &mux->mux_hw);
}

int owl_mux_helper_set_parent(const struct owl_clk_common *common,
			      struct owl_mux_hw *mux_hw, u8 index)
{
	u32 reg;

	regmap_read(common->regmap, mux_hw->reg, &reg);
	reg &= ~GENMASK(mux_hw->width + mux_hw->shift - 1, mux_hw->shift);
	regmap_write(common->regmap, mux_hw->reg,
			reg | (index << mux_hw->shift));

	return 0;
}

static int owl_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct owl_mux *mux = hw_to_owl_mux(hw);

	return owl_mux_helper_set_parent(&mux->common, &mux->mux_hw, index);
}

const struct clk_ops owl_mux_ops = {
	.get_parent = owl_mux_get_parent,
	.set_parent = owl_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

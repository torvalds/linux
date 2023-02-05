// SPDX-License-Identifier: GPL-2.0
/*
 */

#define pr_fmt(fmt) "imx:clk-gpr-mux: " fmt

#include <linux/module.h>

#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "clk.h"

struct imx_clk_gpr {
	struct clk_hw hw;
	struct regmap *regmap;
	u32 mask;
	u32 reg;
	const u32 *mux_table;
};

static struct imx_clk_gpr *to_imx_clk_gpr(struct clk_hw *hw)
{
	return container_of(hw, struct imx_clk_gpr, hw);
}

static u8 imx_clk_gpr_mux_get_parent(struct clk_hw *hw)
{
	struct imx_clk_gpr *priv = to_imx_clk_gpr(hw);
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, priv->reg, &val);
	if (ret)
		goto get_parent_err;

	val &= priv->mask;

	ret = clk_mux_val_to_index(hw, priv->mux_table, 0, val);
	if (ret < 0)
		goto get_parent_err;

	return ret;

get_parent_err:
	pr_err("failed to get parent (%pe)\n", ERR_PTR(ret));

	/* return some realistic non negative value. Potentially we could
	 * give index to some dummy error parent.
	 */
	return 0;
}

static int imx_clk_gpr_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct imx_clk_gpr *priv = to_imx_clk_gpr(hw);
	unsigned int val = clk_mux_index_to_val(priv->mux_table, 0, index);

	return regmap_update_bits(priv->regmap, priv->reg, priv->mask, val);
}

static int imx_clk_gpr_mux_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	return clk_mux_determine_rate_flags(hw, req, 0);
}

static const struct clk_ops imx_clk_gpr_mux_ops = {
	.get_parent = imx_clk_gpr_mux_get_parent,
	.set_parent = imx_clk_gpr_mux_set_parent,
	.determine_rate = imx_clk_gpr_mux_determine_rate,
};

struct clk_hw *imx_clk_gpr_mux(const char *name, const char *compatible,
			       u32 reg, const char **parent_names,
			       u8 num_parents, const u32 *mux_table, u32 mask)
{
	struct clk_init_data init  = { };
	struct imx_clk_gpr *priv;
	struct regmap *regmap;
	struct clk_hw *hw;
	int ret;

	regmap = syscon_regmap_lookup_by_compatible(compatible);
	if (IS_ERR(regmap)) {
		pr_err("failed to find %s regmap\n", compatible);
		return ERR_CAST(regmap);
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &imx_clk_gpr_mux_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;

	priv->hw.init = &init;
	priv->regmap = regmap;
	priv->mux_table = mux_table;
	priv->reg = reg;
	priv->mask = mask;

	hw = &priv->hw;
	ret = clk_hw_register(NULL, &priv->hw);
	if (ret) {
		kfree(priv);
		hw = ERR_PTR(ret);
	}

	return hw;
}

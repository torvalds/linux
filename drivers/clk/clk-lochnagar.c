// SPDX-License-Identifier: GPL-2.0
/*
 * Lochnagar clock control
 *
 * Copyright (c) 2017-2018 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Charles Keepax <ckeepax@opensource.cirrus.com>
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/mfd/lochnagar.h>
#include <linux/mfd/lochnagar1_regs.h>
#include <linux/mfd/lochnagar2_regs.h>

#include <dt-bindings/clk/lochnagar.h>

#define LOCHNAGAR_NUM_CLOCKS	(LOCHNAGAR_SPDIF_CLKOUT + 1)

struct lochnagar_clk {
	const char * const name;
	struct clk_hw hw;

	struct lochnagar_clk_priv *priv;

	u16 cfg_reg;
	u16 ena_mask;

	u16 src_reg;
	u16 src_mask;
};

struct lochnagar_clk_priv {
	struct device *dev;
	struct regmap *regmap;
	enum lochnagar_type type;

	const char **parents;
	unsigned int nparents;

	struct lochnagar_clk lclks[LOCHNAGAR_NUM_CLOCKS];
};

static const char * const lochnagar1_clk_parents[] = {
	"ln-none",
	"ln-spdif-mclk",
	"ln-psia1-mclk",
	"ln-psia2-mclk",
	"ln-cdc-clkout",
	"ln-dsp-clkout",
	"ln-pmic-32k",
	"ln-gf-mclk1",
	"ln-gf-mclk3",
	"ln-gf-mclk2",
	"ln-gf-mclk4",
};

static const char * const lochnagar2_clk_parents[] = {
	"ln-none",
	"ln-cdc-clkout",
	"ln-dsp-clkout",
	"ln-pmic-32k",
	"ln-spdif-mclk",
	"ln-clk-12m",
	"ln-clk-11m",
	"ln-clk-24m",
	"ln-clk-22m",
	"ln-clk-8m",
	"ln-usb-clk-24m",
	"ln-gf-mclk1",
	"ln-gf-mclk3",
	"ln-gf-mclk2",
	"ln-psia1-mclk",
	"ln-psia2-mclk",
	"ln-spdif-clkout",
	"ln-adat-mclk",
	"ln-usb-clk-12m",
};

#define LN1_CLK(ID, NAME, REG) \
	[LOCHNAGAR_##ID] = { \
		.name = NAME, \
		.cfg_reg = LOCHNAGAR1_##REG, \
		.ena_mask = LOCHNAGAR1_##ID##_ENA_MASK, \
		.src_reg = LOCHNAGAR1_##ID##_SEL, \
		.src_mask = LOCHNAGAR1_SRC_MASK, \
	}

#define LN2_CLK(ID, NAME) \
	[LOCHNAGAR_##ID] = { \
		.name = NAME, \
		.cfg_reg = LOCHNAGAR2_##ID##_CTRL, \
		.src_reg = LOCHNAGAR2_##ID##_CTRL, \
		.ena_mask = LOCHNAGAR2_CLK_ENA_MASK, \
		.src_mask = LOCHNAGAR2_CLK_SRC_MASK, \
	}

static const struct lochnagar_clk lochnagar1_clks[LOCHNAGAR_NUM_CLOCKS] = {
	LN1_CLK(CDC_MCLK1,      "ln-cdc-mclk1",  CDC_AIF_CTRL2),
	LN1_CLK(CDC_MCLK2,      "ln-cdc-mclk2",  CDC_AIF_CTRL2),
	LN1_CLK(DSP_CLKIN,      "ln-dsp-clkin",  DSP_AIF),
	LN1_CLK(GF_CLKOUT1,     "ln-gf-clkout1", GF_AIF1),
};

static const struct lochnagar_clk lochnagar2_clks[LOCHNAGAR_NUM_CLOCKS] = {
	LN2_CLK(CDC_MCLK1,      "ln-cdc-mclk1"),
	LN2_CLK(CDC_MCLK2,      "ln-cdc-mclk2"),
	LN2_CLK(DSP_CLKIN,      "ln-dsp-clkin"),
	LN2_CLK(GF_CLKOUT1,     "ln-gf-clkout1"),
	LN2_CLK(GF_CLKOUT2,     "ln-gf-clkout2"),
	LN2_CLK(PSIA1_MCLK,     "ln-psia1-mclk"),
	LN2_CLK(PSIA2_MCLK,     "ln-psia2-mclk"),
	LN2_CLK(SPDIF_MCLK,     "ln-spdif-mclk"),
	LN2_CLK(ADAT_MCLK,      "ln-adat-mclk"),
	LN2_CLK(SOUNDCARD_MCLK, "ln-soundcard-mclk"),
};

static inline struct lochnagar_clk *lochnagar_hw_to_lclk(struct clk_hw *hw)
{
	return container_of(hw, struct lochnagar_clk, hw);
}

static int lochnagar_clk_prepare(struct clk_hw *hw)
{
	struct lochnagar_clk *lclk = lochnagar_hw_to_lclk(hw);
	struct lochnagar_clk_priv *priv = lclk->priv;
	struct regmap *regmap = priv->regmap;
	int ret;

	ret = regmap_update_bits(regmap, lclk->cfg_reg,
				 lclk->ena_mask, lclk->ena_mask);
	if (ret < 0)
		dev_dbg(priv->dev, "Failed to prepare %s: %d\n",
			lclk->name, ret);

	return ret;
}

static void lochnagar_clk_unprepare(struct clk_hw *hw)
{
	struct lochnagar_clk *lclk = lochnagar_hw_to_lclk(hw);
	struct lochnagar_clk_priv *priv = lclk->priv;
	struct regmap *regmap = priv->regmap;
	int ret;

	ret = regmap_update_bits(regmap, lclk->cfg_reg, lclk->ena_mask, 0);
	if (ret < 0)
		dev_dbg(priv->dev, "Failed to unprepare %s: %d\n",
			lclk->name, ret);
}

static int lochnagar_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct lochnagar_clk *lclk = lochnagar_hw_to_lclk(hw);
	struct lochnagar_clk_priv *priv = lclk->priv;
	struct regmap *regmap = priv->regmap;
	int ret;

	ret = regmap_update_bits(regmap, lclk->src_reg, lclk->src_mask, index);
	if (ret < 0)
		dev_dbg(priv->dev, "Failed to reparent %s: %d\n",
			lclk->name, ret);

	return ret;
}

static u8 lochnagar_clk_get_parent(struct clk_hw *hw)
{
	struct lochnagar_clk *lclk = lochnagar_hw_to_lclk(hw);
	struct lochnagar_clk_priv *priv = lclk->priv;
	struct regmap *regmap = priv->regmap;
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, lclk->src_reg, &val);
	if (ret < 0) {
		dev_dbg(priv->dev, "Failed to read parent of %s: %d\n",
			lclk->name, ret);
		return priv->nparents;
	}

	val &= lclk->src_mask;

	return val;
}

static const struct clk_ops lochnagar_clk_ops = {
	.prepare = lochnagar_clk_prepare,
	.unprepare = lochnagar_clk_unprepare,
	.set_parent = lochnagar_clk_set_parent,
	.get_parent = lochnagar_clk_get_parent,
};

static int lochnagar_init_parents(struct lochnagar_clk_priv *priv)
{
	struct device_node *np = priv->dev->of_node;
	int i, j;

	switch (priv->type) {
	case LOCHNAGAR1:
		memcpy(priv->lclks, lochnagar1_clks, sizeof(lochnagar1_clks));

		priv->nparents = ARRAY_SIZE(lochnagar1_clk_parents);
		priv->parents = devm_kmemdup(priv->dev, lochnagar1_clk_parents,
					     sizeof(lochnagar1_clk_parents),
					     GFP_KERNEL);
		break;
	case LOCHNAGAR2:
		memcpy(priv->lclks, lochnagar2_clks, sizeof(lochnagar2_clks));

		priv->nparents = ARRAY_SIZE(lochnagar2_clk_parents);
		priv->parents = devm_kmemdup(priv->dev, lochnagar2_clk_parents,
					     sizeof(lochnagar2_clk_parents),
					     GFP_KERNEL);
		break;
	default:
		dev_err(priv->dev, "Unknown Lochnagar type: %d\n", priv->type);
		return -EINVAL;
	}

	if (!priv->parents)
		return -ENOMEM;

	for (i = 0; i < priv->nparents; i++) {
		j = of_property_match_string(np, "clock-names",
					     priv->parents[i]);
		if (j >= 0)
			priv->parents[i] = of_clk_get_parent_name(np, j);
	}

	return 0;
}

static struct clk_hw *
lochnagar_of_clk_hw_get(struct of_phandle_args *clkspec, void *data)
{
	struct lochnagar_clk_priv *priv = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= ARRAY_SIZE(priv->lclks)) {
		dev_err(priv->dev, "Invalid index %u\n", idx);
		return ERR_PTR(-EINVAL);
	}

	return &priv->lclks[idx].hw;
}

static int lochnagar_init_clks(struct lochnagar_clk_priv *priv)
{
	struct clk_init_data clk_init = {
		.ops = &lochnagar_clk_ops,
		.parent_names = priv->parents,
		.num_parents = priv->nparents,
	};
	struct lochnagar_clk *lclk;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(priv->lclks); i++) {
		lclk = &priv->lclks[i];

		if (!lclk->name)
			continue;

		clk_init.name = lclk->name;

		lclk->priv = priv;
		lclk->hw.init = &clk_init;

		ret = devm_clk_hw_register(priv->dev, &lclk->hw);
		if (ret) {
			dev_err(priv->dev, "Failed to register %s: %d\n",
				lclk->name, ret);
			return ret;
		}
	}

	ret = devm_of_clk_add_hw_provider(priv->dev, lochnagar_of_clk_hw_get,
					  priv);
	if (ret < 0)
		dev_err(priv->dev, "Failed to register provider: %d\n", ret);

	return ret;
}

static const struct of_device_id lochnagar_of_match[] = {
	{ .compatible = "cirrus,lochnagar1-clk", .data = (void *)LOCHNAGAR1 },
	{ .compatible = "cirrus,lochnagar2-clk", .data = (void *)LOCHNAGAR2 },
	{}
};
MODULE_DEVICE_TABLE(of, lochnagar_of_match);

static int lochnagar_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lochnagar_clk_priv *priv;
	const struct of_device_id *of_id;
	int ret;

	of_id = of_match_device(lochnagar_of_match, dev);
	if (!of_id)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regmap = dev_get_regmap(dev->parent, NULL);
	priv->type = (enum lochnagar_type)of_id->data;

	ret = lochnagar_init_parents(priv);
	if (ret)
		return ret;

	return lochnagar_init_clks(priv);
}

static struct platform_driver lochnagar_clk_driver = {
	.driver = {
		.name = "lochnagar-clk",
		.of_match_table = lochnagar_of_match,
	},
	.probe = lochnagar_clk_probe,
};
module_platform_driver(lochnagar_clk_driver);

MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_DESCRIPTION("Clock driver for Cirrus Logic Lochnagar Board");
MODULE_LICENSE("GPL v2");

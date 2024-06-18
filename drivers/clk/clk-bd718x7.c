// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 ROHM Semiconductors

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/regmap.h>

/* clk control registers */
/* BD71815 */
#define BD71815_REG_OUT32K	0x1d
/* BD71828 */
#define BD71828_REG_OUT32K	0x4B
/* BD71837 and BD71847 */
#define BD718XX_REG_OUT32K	0x2E

/*
 * BD71837, BD71847, and BD71828 all use bit [0] to clk output control
 */
#define CLK_OUT_EN_MASK		BIT(0)


struct bd718xx_clk {
	struct clk_hw hw;
	u8 reg;
	u8 mask;
	struct platform_device *pdev;
	struct regmap *regmap;
};

static int bd71837_clk_set(struct bd718xx_clk *c, unsigned int status)
{
	return regmap_update_bits(c->regmap, c->reg, c->mask, status);
}

static void bd71837_clk_disable(struct clk_hw *hw)
{
	int rv;
	struct bd718xx_clk *c = container_of(hw, struct bd718xx_clk, hw);

	rv = bd71837_clk_set(c, 0);
	if (rv)
		dev_dbg(&c->pdev->dev, "Failed to disable 32K clk (%d)\n", rv);
}

static int bd71837_clk_enable(struct clk_hw *hw)
{
	struct bd718xx_clk *c = container_of(hw, struct bd718xx_clk, hw);

	return bd71837_clk_set(c, 0xffffffff);
}

static int bd71837_clk_is_enabled(struct clk_hw *hw)
{
	int enabled;
	int rval;
	struct bd718xx_clk *c = container_of(hw, struct bd718xx_clk, hw);

	rval = regmap_read(c->regmap, c->reg, &enabled);

	if (rval)
		return rval;

	return enabled & c->mask;
}

static const struct clk_ops bd71837_clk_ops = {
	.prepare = &bd71837_clk_enable,
	.unprepare = &bd71837_clk_disable,
	.is_prepared = &bd71837_clk_is_enabled,
};

static int bd71837_clk_probe(struct platform_device *pdev)
{
	struct bd718xx_clk *c;
	int rval = -ENOMEM;
	const char *parent_clk;
	struct device *parent = pdev->dev.parent;
	struct clk_init_data init = {
		.name = "bd718xx-32k-out",
		.ops = &bd71837_clk_ops,
	};
	enum rohm_chip_type chip = platform_get_device_id(pdev)->driver_data;

	c = devm_kzalloc(&pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!c->regmap)
		return -ENODEV;

	init.num_parents = 1;
	parent_clk = of_clk_get_parent_name(parent->of_node, 0);

	init.parent_names = &parent_clk;
	if (!parent_clk) {
		dev_err(&pdev->dev, "No parent clk found\n");
		return -EINVAL;
	}
	switch (chip) {
	case ROHM_CHIP_TYPE_BD71837:
	case ROHM_CHIP_TYPE_BD71847:
		c->reg = BD718XX_REG_OUT32K;
		c->mask = CLK_OUT_EN_MASK;
		break;
	case ROHM_CHIP_TYPE_BD71828:
		c->reg = BD71828_REG_OUT32K;
		c->mask = CLK_OUT_EN_MASK;
		break;
	case ROHM_CHIP_TYPE_BD71815:
		c->reg = BD71815_REG_OUT32K;
		c->mask = CLK_OUT_EN_MASK;
		break;
	default:
		dev_err(&pdev->dev, "Unknown clk chip\n");
		return -EINVAL;
	}
	c->pdev = pdev;
	c->hw.init = &init;

	of_property_read_string_index(parent->of_node,
				      "clock-output-names", 0, &init.name);

	rval = devm_clk_hw_register(&pdev->dev, &c->hw);
	if (rval) {
		dev_err(&pdev->dev, "failed to register 32K clk");
		return rval;
	}
	rval = devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_simple_get,
					   &c->hw);
	if (rval)
		dev_err(&pdev->dev, "adding clk provider failed\n");

	return rval;
}

static const struct platform_device_id bd718x7_clk_id[] = {
	{ "bd71837-clk", ROHM_CHIP_TYPE_BD71837 },
	{ "bd71847-clk", ROHM_CHIP_TYPE_BD71847 },
	{ "bd71828-clk", ROHM_CHIP_TYPE_BD71828 },
	{ "bd71815-clk", ROHM_CHIP_TYPE_BD71815 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd718x7_clk_id);

static struct platform_driver bd71837_clk = {
	.driver = {
		.name = "bd718xx-clk",
	},
	.probe = bd71837_clk_probe,
	.id_table = bd718x7_clk_id,
};

module_platform_driver(bd71837_clk);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD718(15/18/28/37/47/50) and chip clk driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd718xx-clk");

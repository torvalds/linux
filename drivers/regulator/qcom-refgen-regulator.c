// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2017, 2019-2020, The Linux Foundation. All rights reserved.
// Copyright (c) 2023, Linaro Limited

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define REFGEN_REG_BIAS_EN		0x08
#define REFGEN_BIAS_EN_MASK		GENMASK(2, 0)
 #define REFGEN_BIAS_EN_ENABLE		0x7
 #define REFGEN_BIAS_EN_DISABLE		0x6

#define REFGEN_REG_BG_CTRL		0x14
#define REFGEN_BG_CTRL_MASK		GENMASK(2, 1)
 #define REFGEN_BG_CTRL_ENABLE		0x3
 #define REFGEN_BG_CTRL_DISABLE		0x2

#define REFGEN_REG_PWRDWN_CTRL5		0x80
#define REFGEN_PWRDWN_CTRL5_MASK	BIT(0)
 #define REFGEN_PWRDWN_CTRL5_ENABLE	0x1

static int qcom_sdm845_refgen_enable(struct regulator_dev *rdev)
{
	regmap_update_bits(rdev->regmap, REFGEN_REG_BG_CTRL, REFGEN_BG_CTRL_MASK,
			   FIELD_PREP(REFGEN_BG_CTRL_MASK, REFGEN_BG_CTRL_ENABLE));

	regmap_write(rdev->regmap, REFGEN_REG_BIAS_EN,
		     FIELD_PREP(REFGEN_BIAS_EN_MASK, REFGEN_BIAS_EN_ENABLE));

	return 0;
}

static int qcom_sdm845_refgen_disable(struct regulator_dev *rdev)
{
	regmap_write(rdev->regmap, REFGEN_REG_BIAS_EN,
		     FIELD_PREP(REFGEN_BIAS_EN_MASK, REFGEN_BIAS_EN_DISABLE));

	regmap_update_bits(rdev->regmap, REFGEN_REG_BG_CTRL, REFGEN_BG_CTRL_MASK,
			   FIELD_PREP(REFGEN_BG_CTRL_MASK, REFGEN_BG_CTRL_DISABLE));

	return 0;
}

static int qcom_sdm845_refgen_is_enabled(struct regulator_dev *rdev)
{
	u32 val;

	regmap_read(rdev->regmap, REFGEN_REG_BG_CTRL, &val);
	if (FIELD_GET(REFGEN_BG_CTRL_MASK, val) != REFGEN_BG_CTRL_ENABLE)
		return 0;

	regmap_read(rdev->regmap, REFGEN_REG_BIAS_EN, &val);
	if (FIELD_GET(REFGEN_BIAS_EN_MASK, val) != REFGEN_BIAS_EN_ENABLE)
		return 0;

	return 1;
}

static struct regulator_desc sdm845_refgen_desc = {
	.enable_time = 5,
	.name = "refgen",
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.ops = &(const struct regulator_ops) {
		.enable		= qcom_sdm845_refgen_enable,
		.disable	= qcom_sdm845_refgen_disable,
		.is_enabled	= qcom_sdm845_refgen_is_enabled,
	},
};

static struct regulator_desc sm8250_refgen_desc = {
	.enable_reg = REFGEN_REG_PWRDWN_CTRL5,
	.enable_mask = REFGEN_PWRDWN_CTRL5_MASK,
	.enable_val = REFGEN_PWRDWN_CTRL5_ENABLE,
	.disable_val = 0,
	.enable_time = 5,
	.name = "refgen",
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.ops = &(const struct regulator_ops) {
		.enable		= regulator_enable_regmap,
		.disable	= regulator_disable_regmap,
		.is_enabled	= regulator_is_enabled_regmap,
	},
};

static const struct regmap_config qcom_refgen_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

static int qcom_refgen_probe(struct platform_device *pdev)
{
	struct regulator_init_data *init_data;
	struct regulator_config config = {};
	const struct regulator_desc *rdesc;
	struct device *dev = &pdev->dev;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	void __iomem *base;

	rdesc = of_device_get_match_data(dev);
	if (!rdesc)
		return -ENODATA;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &qcom_refgen_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	init_data = of_get_regulator_init_data(dev, dev->of_node, rdesc);
	if (!init_data)
		return -ENOMEM;

	config.dev = dev;
	config.init_data = init_data;
	config.of_node = dev->of_node;
	config.regmap = regmap;

	rdev = devm_regulator_register(dev, rdesc, &config);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	return 0;
}

static const struct of_device_id qcom_refgen_match_table[] = {
	{ .compatible = "qcom,sdm845-refgen-regulator", .data = &sdm845_refgen_desc },
	{ .compatible = "qcom,sm8250-refgen-regulator", .data = &sm8250_refgen_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_refgen_match_table);

static struct platform_driver qcom_refgen_driver = {
	.probe = qcom_refgen_probe,
	.driver = {
		.name = "qcom-refgen-regulator",
		.of_match_table = qcom_refgen_match_table,
	},
};
module_platform_driver(qcom_refgen_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm REFGEN regulator driver");

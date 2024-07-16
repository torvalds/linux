// SPDX-License-Identifier: GPL-2.0-only
/*
 * Oxford Semiconductor Reset Controller driver
 *
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2014 Ma Haijun <mahaijuns@gmail.com>
 * Copyright (C) 2009 Oxford Semiconductor Ltd
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

/* Regmap offsets */
#define RST_SET_REGOFFSET	0x34
#define RST_CLR_REGOFFSET	0x38

struct oxnas_reset {
	struct regmap *regmap;
	struct reset_controller_dev rcdev;
};

static int oxnas_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct oxnas_reset *data =
		container_of(rcdev, struct oxnas_reset, rcdev);

	regmap_write(data->regmap, RST_SET_REGOFFSET, BIT(id));
	msleep(50);
	regmap_write(data->regmap, RST_CLR_REGOFFSET, BIT(id));

	return 0;
}

static int oxnas_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct oxnas_reset *data =
		container_of(rcdev, struct oxnas_reset, rcdev);

	regmap_write(data->regmap, RST_SET_REGOFFSET, BIT(id));

	return 0;
}

static int oxnas_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct oxnas_reset *data =
		container_of(rcdev, struct oxnas_reset, rcdev);

	regmap_write(data->regmap, RST_CLR_REGOFFSET, BIT(id));

	return 0;
}

static const struct reset_control_ops oxnas_reset_ops = {
	.reset		= oxnas_reset_reset,
	.assert		= oxnas_reset_assert,
	.deassert	= oxnas_reset_deassert,
};

static const struct of_device_id oxnas_reset_dt_ids[] = {
	 { .compatible = "oxsemi,ox810se-reset", },
	 { .compatible = "oxsemi,ox820-reset", },
	 { /* sentinel */ },
};

static int oxnas_reset_probe(struct platform_device *pdev)
{
	struct oxnas_reset *data;
	struct device *parent;

	parent = pdev->dev.parent;
	if (!parent) {
		dev_err(&pdev->dev, "no parent\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = syscon_node_to_regmap(parent->of_node);
	if (IS_ERR(data->regmap)) {
		dev_err(&pdev->dev, "failed to get parent regmap\n");
		return PTR_ERR(data->regmap);
	}

	platform_set_drvdata(pdev, data);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = 32;
	data->rcdev.ops = &oxnas_reset_ops;
	data->rcdev.of_node = pdev->dev.of_node;

	return devm_reset_controller_register(&pdev->dev, &data->rcdev);
}

static struct platform_driver oxnas_reset_driver = {
	.probe	= oxnas_reset_probe,
	.driver = {
		.name		= "oxnas-reset",
		.of_match_table	= oxnas_reset_dt_ids,
	},
};
builtin_platform_driver(oxnas_reset_driver);

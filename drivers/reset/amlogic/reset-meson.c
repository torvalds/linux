// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Amlogic Meson Reset Controller driver
 *
 * Copyright (c) 2016-2024 BayLibre, SAS.
 * Authors: Neil Armstrong <narmstrong@baylibre.com>
 *          Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include "reset-meson.h"

static const struct meson_reset_param meson8b_param = {
	.reset_ops	= &meson_reset_ops,
	.reset_num	= 256,
	.reset_offset	= 0x0,
	.level_offset	= 0x7c,
	.level_low_reset = true,
};

static const struct meson_reset_param meson_a1_param = {
	.reset_ops	= &meson_reset_ops,
	.reset_num	= 96,
	.reset_offset	= 0x0,
	.level_offset	= 0x40,
	.level_low_reset = true,
};

static const struct meson_reset_param meson_s4_param = {
	.reset_ops	= &meson_reset_ops,
	.reset_num	= 192,
	.reset_offset	= 0x0,
	.level_offset	= 0x40,
	.level_low_reset = true,
};

static const struct meson_reset_param t7_param = {
	.reset_num      = 224,
	.reset_offset	= 0x0,
	.level_offset   = 0x40,
	.level_low_reset = true,
};

static const struct of_device_id meson_reset_dt_ids[] = {
	 { .compatible = "amlogic,meson8b-reset",    .data = &meson8b_param},
	 { .compatible = "amlogic,meson-gxbb-reset", .data = &meson8b_param},
	 { .compatible = "amlogic,meson-axg-reset",  .data = &meson8b_param},
	 { .compatible = "amlogic,meson-a1-reset",   .data = &meson_a1_param},
	 { .compatible = "amlogic,meson-s4-reset",   .data = &meson_s4_param},
	 { .compatible = "amlogic,c3-reset",   .data = &meson_s4_param},
	 { .compatible = "amlogic,t7-reset",   .data = &t7_param},
	 { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_reset_dt_ids);

static const struct regmap_config regmap_config = {
	.reg_bits   = 32,
	.val_bits   = 32,
	.reg_stride = 4,
};

static int meson_reset_probe(struct platform_device *pdev)
{
	const struct meson_reset_param *param;
	struct device *dev = &pdev->dev;
	struct regmap *map;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	param = device_get_match_data(dev);
	if (!param)
		return -ENODEV;

	map = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map),
				     "can't init regmap mmio region\n");

	return meson_reset_controller_register(dev, map, param);
}

static struct platform_driver meson_reset_driver = {
	.probe	= meson_reset_probe,
	.driver = {
		.name		= "meson_reset",
		.of_match_table	= meson_reset_dt_ids,
	},
};
module_platform_driver(meson_reset_driver);

MODULE_DESCRIPTION("Amlogic Meson Reset Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS("MESON_RESET");

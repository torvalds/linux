// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ROHM Semiconductors
 *
 * ROHM/KIONIX accelerometer driver
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "kionix-kx022a.h"

static int kx022a_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	const struct kx022a_chip_info *chip_info;
	struct regmap *regmap;

	if (!i2c->irq) {
		dev_err(dev, "No IRQ configured\n");
		return -EINVAL;
	}

	chip_info = i2c_get_match_data(i2c);
	if (!chip_info)
		return -EINVAL;

	regmap = devm_regmap_init_i2c(i2c, chip_info->regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize Regmap\n");

	return kx022a_probe_internal(dev, chip_info);
}

static const struct i2c_device_id kx022a_i2c_id[] = {
	{ .name = "kx022a", .driver_data = (kernel_ulong_t)&kx022a_chip_info },
	{ .name = "kx132-1211", .driver_data = (kernel_ulong_t)&kx132_chip_info },
	{ .name = "kx134-1211", .driver_data = (kernel_ulong_t)&kx134_chip_info },
	{ .name = "kx132acr-lbz", .driver_data = (kernel_ulong_t)&kx132acr_chip_info },
	{ .name = "kx134acr-lbz", .driver_data = (kernel_ulong_t)&kx134acr_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, kx022a_i2c_id);

static const struct of_device_id kx022a_of_match[] = {
	{ .compatible = "kionix,kx022a", .data = &kx022a_chip_info },
	{ .compatible = "kionix,kx132-1211", .data = &kx132_chip_info },
	{ .compatible = "kionix,kx134-1211", .data = &kx134_chip_info },
	{ .compatible = "rohm,kx132acr-lbz", .data = &kx132acr_chip_info },
	{ .compatible = "rohm,kx134acr-lbz", .data = &kx134acr_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, kx022a_of_match);

static struct i2c_driver kx022a_i2c_driver = {
	.driver = {
		.name  = "kx022a-i2c",
		.of_match_table = kx022a_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	  },
	.probe        = kx022a_i2c_probe,
	.id_table     = kx022a_i2c_id,
};
module_i2c_driver(kx022a_i2c_driver);

MODULE_DESCRIPTION("ROHM/Kionix KX022A accelerometer driver");
MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_KX022A");

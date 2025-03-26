// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ROHM Semiconductors
 *
 * ROHM/KIONIX accelerometer driver
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "kionix-kx022a.h"

static int kx022a_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct kx022a_chip_info *chip_info;
	struct regmap *regmap;

	if (!spi->irq) {
		dev_err(dev, "No IRQ configured\n");
		return -EINVAL;
	}

	chip_info = spi_get_device_match_data(spi);
	if (!chip_info)
		return -EINVAL;

	regmap = devm_regmap_init_spi(spi, chip_info->regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize Regmap\n");

	return kx022a_probe_internal(dev, chip_info);
}

static const struct spi_device_id kx022a_id[] = {
	{ .name = "kx022a", .driver_data = (kernel_ulong_t)&kx022a_chip_info },
	{ .name = "kx132-1211", .driver_data = (kernel_ulong_t)&kx132_chip_info },
	{ .name = "kx134-1211", .driver_data = (kernel_ulong_t)&kx134_chip_info },
	{ .name = "kx132acr-lbz", .driver_data = (kernel_ulong_t)&kx132acr_chip_info },
	{ .name = "kx134acr-lbz", .driver_data = (kernel_ulong_t)&kx134acr_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, kx022a_id);

static const struct of_device_id kx022a_of_match[] = {
	{ .compatible = "kionix,kx022a", .data = &kx022a_chip_info },
	{ .compatible = "kionix,kx132-1211", .data = &kx132_chip_info },
	{ .compatible = "kionix,kx134-1211", .data = &kx134_chip_info },
	{ .compatible = "rohm,kx132acr-lbz", .data = &kx132acr_chip_info },
	{ .compatible = "rohm,kx134acr-lbz", .data = &kx134acr_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, kx022a_of_match);

static struct spi_driver kx022a_spi_driver = {
	.driver = {
		.name   = "kx022a-spi",
		.of_match_table = kx022a_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = kx022a_spi_probe,
	.id_table = kx022a_id,
};
module_spi_driver(kx022a_spi_driver);

MODULE_DESCRIPTION("ROHM/Kionix kx022A accelerometer driver");
MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_KX022A");

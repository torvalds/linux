// SPDX-License-Identifier: GPL-2.0
/*
 * FXOS8700 - NXP IMU, SPI bits
 */
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "fxos8700.h"

static int fxos8700_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	const struct spi_device_id *id = spi_get_device_id(spi);

	regmap = devm_regmap_init_spi(spi, &fxos8700_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return fxos8700_core_probe(&spi->dev, regmap, id->name, true);
}

static const struct spi_device_id fxos8700_spi_id[] = {
	{"fxos8700", 0},
	{ }
};
MODULE_DEVICE_TABLE(spi, fxos8700_spi_id);

static const struct acpi_device_id fxos8700_acpi_match[] = {
	{"FXOS8700", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, fxos8700_acpi_match);

static const struct of_device_id fxos8700_of_match[] = {
	{ .compatible = "nxp,fxos8700" },
	{ }
};
MODULE_DEVICE_TABLE(of, fxos8700_of_match);

static struct spi_driver fxos8700_spi_driver = {
	.probe          = fxos8700_spi_probe,
	.id_table       = fxos8700_spi_id,
	.driver = {
		.acpi_match_table       = fxos8700_acpi_match,
		.of_match_table         = fxos8700_of_match,
		.name                   = "fxos8700_spi",
	},
};
module_spi_driver(fxos8700_spi_driver);

MODULE_AUTHOR("Robert Jones <rjones@gateworks.com>");
MODULE_DESCRIPTION("FXOS8700 SPI driver");
MODULE_LICENSE("GPL v2");

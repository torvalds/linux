// SPDX-License-Identifier: GPL-2.0+
/*
 * IIO driver for MCP48FEB02 Multi-Channel DAC with SPI interface
 *
 * Copyright (C) 2025-2026 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Ariana Lazar <ariana.lazar@microchip.com>
 *
 * Datasheet links for devices with SPI interface:
 * [MCP48FEBxx] https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20005429B.pdf
 * [MCP48FVBxx] https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20005466A.pdf
 * [MCP48FxBx4/8] https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/MCP48FXBX4-8-Family-Data-Sheet-DS20006362A.pdf
 */
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "mcp47feb02.h"

/* Parts with EEPROM memory */
MCP47FEB02_CHIP_INFO(mcp48feb01, 1, 8,  false, true);
MCP47FEB02_CHIP_INFO(mcp48feb02, 2, 8,  false, true);
MCP47FEB02_CHIP_INFO(mcp48feb04, 4, 8,  true,  true);
MCP47FEB02_CHIP_INFO(mcp48feb08, 8, 8,  true,  true);
MCP47FEB02_CHIP_INFO(mcp48feb11, 1, 10, false, true);
MCP47FEB02_CHIP_INFO(mcp48feb12, 2, 10, false, true);
MCP47FEB02_CHIP_INFO(mcp48feb14, 4, 10, true,  true);
MCP47FEB02_CHIP_INFO(mcp48feb18, 8, 10, true,  true);
MCP47FEB02_CHIP_INFO(mcp48feb21, 1, 12, false, true);
MCP47FEB02_CHIP_INFO(mcp48feb22, 2, 12, false, true);
MCP47FEB02_CHIP_INFO(mcp48feb24, 4, 12, true,  true);
MCP47FEB02_CHIP_INFO(mcp48feb28, 8, 12, true,  true);

/* Parts without EEPROM memory */
MCP47FEB02_CHIP_INFO(mcp48fvb01, 1, 8,  false, false);
MCP47FEB02_CHIP_INFO(mcp48fvb02, 2, 8,  false, false);
MCP47FEB02_CHIP_INFO(mcp48fvb04, 4, 8,  true,  false);
MCP47FEB02_CHIP_INFO(mcp48fvb08, 8, 8,  true,  false);
MCP47FEB02_CHIP_INFO(mcp48fvb11, 1, 10, false, false);
MCP47FEB02_CHIP_INFO(mcp48fvb12, 2, 10, false, false);
MCP47FEB02_CHIP_INFO(mcp48fvb14, 4, 10, true,  false);
MCP47FEB02_CHIP_INFO(mcp48fvb18, 8, 10, true,  false);
MCP47FEB02_CHIP_INFO(mcp48fvb21, 1, 12, false, false);
MCP47FEB02_CHIP_INFO(mcp48fvb22, 2, 12, false, false);
MCP47FEB02_CHIP_INFO(mcp48fvb24, 4, 12, true,  false);
MCP47FEB02_CHIP_INFO(mcp48fvb28, 8, 12, true,  false);

static int mcp47feb02_spi_probe(struct spi_device *spi)
{
	const struct mcp47feb02_features *chip_features;
	struct device *dev = &spi->dev;
	struct regmap *regmap;

	chip_features = spi_get_device_match_data(spi);
	if (!chip_features)
		return dev_err_probe(dev, -ENODATA, "No match data found\n");

	if (chip_features->have_eeprom)
		regmap = devm_regmap_init_spi(spi, &mcp47feb02_regmap_config);
	else
		regmap = devm_regmap_init_spi(spi, &mcp47fvb02_regmap_config);

	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Error initializing SPI regmap\n");

	return mcp47feb02_common_probe(chip_features, regmap);
}

static const struct spi_device_id mcp47feb02_spi_id[] = {
	{ .name = "mcp48feb01", .driver_data = (kernel_ulong_t)&mcp48feb01_chip_features },
	{ .name = "mcp48feb02", .driver_data = (kernel_ulong_t)&mcp48feb02_chip_features },
	{ .name = "mcp48feb04", .driver_data = (kernel_ulong_t)&mcp48feb04_chip_features },
	{ .name = "mcp48feb08", .driver_data = (kernel_ulong_t)&mcp48feb08_chip_features },
	{ .name = "mcp48feb11", .driver_data = (kernel_ulong_t)&mcp48feb11_chip_features },
	{ .name = "mcp48feb12", .driver_data = (kernel_ulong_t)&mcp48feb12_chip_features },
	{ .name = "mcp48feb14", .driver_data = (kernel_ulong_t)&mcp48feb14_chip_features },
	{ .name = "mcp48feb18", .driver_data =  (kernel_ulong_t)&mcp48feb18_chip_features },
	{ .name = "mcp48feb21", .driver_data = (kernel_ulong_t)&mcp48feb21_chip_features },
	{ .name = "mcp48feb22", .driver_data = (kernel_ulong_t)&mcp48feb22_chip_features },
	{ .name = "mcp48feb24", .driver_data = (kernel_ulong_t)&mcp48feb24_chip_features },
	{ .name = "mcp48feb28", .driver_data = (kernel_ulong_t)&mcp48feb28_chip_features },
	{ .name = "mcp48fvb01", .driver_data = (kernel_ulong_t)&mcp48fvb01_chip_features },
	{ .name = "mcp48fvb02", .driver_data = (kernel_ulong_t)&mcp48fvb02_chip_features },
	{ .name = "mcp48fvb04", .driver_data = (kernel_ulong_t)&mcp48fvb04_chip_features },
	{ .name = "mcp48fvb08", .driver_data = (kernel_ulong_t)&mcp48fvb08_chip_features },
	{ .name = "mcp48fvb11", .driver_data = (kernel_ulong_t)&mcp48fvb11_chip_features },
	{ .name = "mcp48fvb12", .driver_data = (kernel_ulong_t)&mcp48fvb12_chip_features },
	{ .name = "mcp48fvb14", .driver_data = (kernel_ulong_t)&mcp48fvb14_chip_features },
	{ .name = "mcp48fvb18", .driver_data = (kernel_ulong_t)&mcp48fvb18_chip_features },
	{ .name = "mcp48fvb21", .driver_data = (kernel_ulong_t)&mcp48fvb21_chip_features },
	{ .name = "mcp48fvb22", .driver_data = (kernel_ulong_t)&mcp48fvb22_chip_features },
	{ .name = "mcp48fvb24", .driver_data = (kernel_ulong_t)&mcp48fvb24_chip_features },
	{ .name = "mcp48fvb28", .driver_data = (kernel_ulong_t)&mcp48fvb28_chip_features },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp47feb02_spi_id);

static const struct of_device_id mcp47feb02_of_spi_match[] = {
	{ .compatible = "microchip,mcp48feb01", .data = &mcp48feb01_chip_features },
	{ .compatible = "microchip,mcp48feb02", .data = &mcp48feb02_chip_features },
	{ .compatible = "microchip,mcp48feb04", .data = &mcp48feb04_chip_features },
	{ .compatible = "microchip,mcp48feb08", .data = &mcp48feb08_chip_features },
	{ .compatible = "microchip,mcp48feb11", .data = &mcp48feb11_chip_features },
	{ .compatible = "microchip,mcp48feb12", .data = &mcp48feb12_chip_features },
	{ .compatible = "microchip,mcp48feb14", .data = &mcp48feb14_chip_features },
	{ .compatible = "microchip,mcp48feb18", .data = &mcp48feb18_chip_features },
	{ .compatible = "microchip,mcp48feb21", .data = &mcp48feb21_chip_features },
	{ .compatible = "microchip,mcp48feb22", .data = &mcp48feb22_chip_features },
	{ .compatible = "microchip,mcp48feb24", .data = &mcp48feb24_chip_features },
	{ .compatible = "microchip,mcp48feb28", .data = &mcp48feb28_chip_features },
	{ .compatible = "microchip,mcp48fvb01", .data = &mcp48fvb01_chip_features },
	{ .compatible = "microchip,mcp48fvb02", .data = &mcp48fvb02_chip_features },
	{ .compatible = "microchip,mcp48fvb04", .data = &mcp48fvb04_chip_features },
	{ .compatible = "microchip,mcp48fvb08", .data = &mcp48fvb08_chip_features },
	{ .compatible = "microchip,mcp48fvb11", .data = &mcp48fvb11_chip_features },
	{ .compatible = "microchip,mcp48fvb12", .data = &mcp48fvb12_chip_features },
	{ .compatible = "microchip,mcp48fvb14",	.data = &mcp48fvb14_chip_features },
	{ .compatible = "microchip,mcp48fvb18", .data = &mcp48fvb18_chip_features },
	{ .compatible = "microchip,mcp48fvb21", .data = &mcp48fvb21_chip_features },
	{ .compatible = "microchip,mcp48fvb22", .data = &mcp48fvb22_chip_features },
	{ .compatible = "microchip,mcp48fvb24", .data = &mcp48fvb24_chip_features },
	{ .compatible = "microchip,mcp48fvb28", .data = &mcp48fvb28_chip_features },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp47feb02_of_spi_match);

static struct spi_driver mcp47feb02_spi_driver = {
	.driver = {
		.name = "mcp47feb02",
		.of_match_table = mcp47feb02_of_spi_match,
		.pm = pm_sleep_ptr(&mcp47feb02_pm_ops),
	},
	.probe = mcp47feb02_spi_probe,
	.id_table = mcp47feb02_spi_id,
};
module_spi_driver(mcp47feb02_spi_driver);

MODULE_AUTHOR("Ariana Lazar <ariana.lazar@microchip.com>");
MODULE_DESCRIPTION("IIO driver for MCP48FEB02 Multi-Channel DAC with SPI interface");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_MCP47FEB02");

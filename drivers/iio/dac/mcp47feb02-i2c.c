// SPDX-License-Identifier: GPL-2.0+
/*
 * IIO driver for MCP47FEB02 Multi-Channel DAC with I2C interface
 *
 * Copyright (C) 2025-2026 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Ariana Lazar <ariana.lazar@microchip.com>
 *
 * Datasheet links for devices with I2C interface:
 * [MCP47FEBxx] https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20005375A.pdf
 * [MCP47FVBxx] https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20005405A.pdf
 * [MCP47FxBx4/8] https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/MCP47FXBX48-Data-Sheet-DS200006368A.pdf
 */
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "mcp47feb02.h"

/* Parts with EEPROM memory */
MCP47FEB02_CHIP_INFO(mcp47feb01, 1, 8,  false, true);
MCP47FEB02_CHIP_INFO(mcp47feb02, 2, 8,  false, true);
MCP47FEB02_CHIP_INFO(mcp47feb04, 4, 8,  true,  true);
MCP47FEB02_CHIP_INFO(mcp47feb08, 8, 8,  true,  true);
MCP47FEB02_CHIP_INFO(mcp47feb11, 1, 10, false, true);
MCP47FEB02_CHIP_INFO(mcp47feb12, 2, 10, false, true);
MCP47FEB02_CHIP_INFO(mcp47feb14, 4, 10, true,  true);
MCP47FEB02_CHIP_INFO(mcp47feb18, 8, 10, true,  true);
MCP47FEB02_CHIP_INFO(mcp47feb21, 1, 12, false, true);
MCP47FEB02_CHIP_INFO(mcp47feb22, 2, 12, false, true);
MCP47FEB02_CHIP_INFO(mcp47feb24, 4, 12, true,  true);
MCP47FEB02_CHIP_INFO(mcp47feb28, 8, 12, true,  true);

/* Parts without EEPROM memory */
MCP47FEB02_CHIP_INFO(mcp47fvb01, 1, 8,  false, false);
MCP47FEB02_CHIP_INFO(mcp47fvb02, 2, 8,  false, false);
MCP47FEB02_CHIP_INFO(mcp47fvb04, 4, 8,  true,  false);
MCP47FEB02_CHIP_INFO(mcp47fvb08, 8, 8,  true,  false);
MCP47FEB02_CHIP_INFO(mcp47fvb11, 1, 10, false, false);
MCP47FEB02_CHIP_INFO(mcp47fvb12, 2, 10, false, false);
MCP47FEB02_CHIP_INFO(mcp47fvb14, 4, 10, true,  false);
MCP47FEB02_CHIP_INFO(mcp47fvb18, 8, 10, true,  false);
MCP47FEB02_CHIP_INFO(mcp47fvb21, 1, 12, false, false);
MCP47FEB02_CHIP_INFO(mcp47fvb22, 2, 12, false, false);
MCP47FEB02_CHIP_INFO(mcp47fvb24, 4, 12, true,  false);
MCP47FEB02_CHIP_INFO(mcp47fvb28, 8, 12, true,  false);

static int mcp47feb02_i2c_probe(struct i2c_client *client)
{
	const struct mcp47feb02_features *chip_features;
	struct device *dev = &client->dev;
	struct regmap *regmap;

	chip_features = i2c_get_match_data(client);
	if (!chip_features)
		return dev_err_probe(dev, -ENODATA, "No match data found\n");

	if (chip_features->have_eeprom)
		regmap = devm_regmap_init_i2c(client, &mcp47feb02_regmap_config);
	else
		regmap = devm_regmap_init_i2c(client, &mcp47fvb02_regmap_config);

	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Error initializing I2C regmap\n");

	return mcp47feb02_common_probe(chip_features, regmap);
}

static const struct i2c_device_id mcp47feb02_i2c_id[] = {
	{ .name = "mcp47feb01", .driver_data = (kernel_ulong_t)&mcp47feb01_chip_features },
	{ .name = "mcp47feb02", .driver_data = (kernel_ulong_t)&mcp47feb02_chip_features },
	{ .name = "mcp47feb04", .driver_data = (kernel_ulong_t)&mcp47feb04_chip_features },
	{ .name = "mcp47feb08", .driver_data = (kernel_ulong_t)&mcp47feb08_chip_features },
	{ .name = "mcp47feb11", .driver_data = (kernel_ulong_t)&mcp47feb11_chip_features },
	{ .name = "mcp47feb12", .driver_data = (kernel_ulong_t)&mcp47feb12_chip_features },
	{ .name = "mcp47feb14", .driver_data = (kernel_ulong_t)&mcp47feb14_chip_features },
	{ .name = "mcp47feb18", .driver_data = (kernel_ulong_t)&mcp47feb18_chip_features },
	{ .name = "mcp47feb21", .driver_data = (kernel_ulong_t)&mcp47feb21_chip_features },
	{ .name = "mcp47feb22", .driver_data = (kernel_ulong_t)&mcp47feb22_chip_features },
	{ .name = "mcp47feb24", .driver_data = (kernel_ulong_t)&mcp47feb24_chip_features },
	{ .name = "mcp47feb28", .driver_data = (kernel_ulong_t)&mcp47feb28_chip_features },
	{ .name = "mcp47fvb01", .driver_data = (kernel_ulong_t)&mcp47fvb01_chip_features },
	{ .name = "mcp47fvb02", .driver_data = (kernel_ulong_t)&mcp47fvb02_chip_features },
	{ .name = "mcp47fvb04", .driver_data = (kernel_ulong_t)&mcp47fvb04_chip_features },
	{ .name = "mcp47fvb08", .driver_data = (kernel_ulong_t)&mcp47fvb08_chip_features },
	{ .name = "mcp47fvb11", .driver_data = (kernel_ulong_t)&mcp47fvb11_chip_features },
	{ .name = "mcp47fvb12", .driver_data = (kernel_ulong_t)&mcp47fvb12_chip_features },
	{ .name = "mcp47fvb14", .driver_data = (kernel_ulong_t)&mcp47fvb14_chip_features },
	{ .name = "mcp47fvb18", .driver_data = (kernel_ulong_t)&mcp47fvb18_chip_features },
	{ .name = "mcp47fvb21", .driver_data = (kernel_ulong_t)&mcp47fvb21_chip_features },
	{ .name = "mcp47fvb22", .driver_data = (kernel_ulong_t)&mcp47fvb22_chip_features },
	{ .name = "mcp47fvb24", .driver_data = (kernel_ulong_t)&mcp47fvb24_chip_features },
	{ .name = "mcp47fvb28", .driver_data = (kernel_ulong_t)&mcp47fvb28_chip_features },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp47feb02_i2c_id);

static const struct of_device_id mcp47feb02_of_i2c_match[] = {
	{ .compatible = "microchip,mcp47feb01", .data = &mcp47feb01_chip_features },
	{ .compatible = "microchip,mcp47feb02", .data = &mcp47feb02_chip_features },
	{ .compatible = "microchip,mcp47feb04", .data = &mcp47feb04_chip_features },
	{ .compatible = "microchip,mcp47feb08", .data = &mcp47feb08_chip_features },
	{ .compatible = "microchip,mcp47feb11", .data = &mcp47feb11_chip_features },
	{ .compatible = "microchip,mcp47feb12", .data = &mcp47feb12_chip_features },
	{ .compatible = "microchip,mcp47feb14", .data = &mcp47feb14_chip_features },
	{ .compatible = "microchip,mcp47feb18", .data = &mcp47feb18_chip_features },
	{ .compatible = "microchip,mcp47feb21", .data = &mcp47feb21_chip_features },
	{ .compatible = "microchip,mcp47feb22", .data = &mcp47feb22_chip_features },
	{ .compatible = "microchip,mcp47feb24", .data = &mcp47feb24_chip_features },
	{ .compatible = "microchip,mcp47feb28", .data = &mcp47feb28_chip_features },
	{ .compatible = "microchip,mcp47fvb01", .data = &mcp47fvb01_chip_features },
	{ .compatible = "microchip,mcp47fvb02", .data = &mcp47fvb02_chip_features },
	{ .compatible = "microchip,mcp47fvb04", .data = &mcp47fvb04_chip_features },
	{ .compatible = "microchip,mcp47fvb08", .data = &mcp47fvb08_chip_features },
	{ .compatible = "microchip,mcp47fvb11", .data = &mcp47fvb11_chip_features },
	{ .compatible = "microchip,mcp47fvb12", .data = &mcp47fvb12_chip_features },
	{ .compatible = "microchip,mcp47fvb14",	.data = &mcp47fvb14_chip_features },
	{ .compatible = "microchip,mcp47fvb18", .data = &mcp47fvb18_chip_features },
	{ .compatible = "microchip,mcp47fvb21", .data = &mcp47fvb21_chip_features },
	{ .compatible = "microchip,mcp47fvb22", .data = &mcp47fvb22_chip_features },
	{ .compatible = "microchip,mcp47fvb24", .data = &mcp47fvb24_chip_features },
	{ .compatible = "microchip,mcp47fvb28", .data = &mcp47fvb28_chip_features },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp47feb02_of_i2c_match);

static struct i2c_driver mcp47feb02_i2c_driver = {
	.driver = {
		.name = "mcp47feb02",
		.of_match_table = mcp47feb02_of_i2c_match,
		.pm = pm_sleep_ptr(&mcp47feb02_pm_ops),
	},
	.probe = mcp47feb02_i2c_probe,
	.id_table = mcp47feb02_i2c_id,
};
module_i2c_driver(mcp47feb02_i2c_driver);

MODULE_AUTHOR("Ariana Lazar <ariana.lazar@microchip.com>");
MODULE_DESCRIPTION("IIO driver for MCP47FEB02 Multi-Channel DAC with I2C interface");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_MCP47FEB02");

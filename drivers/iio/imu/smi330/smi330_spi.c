// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Copyright (c) 2025 Robert Bosch GmbH.
 */
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "smi330.h"

static int smi330_regmap_spi_read(void *context, const void *reg_buf,
				  size_t reg_size, void *val_buf,
				  size_t val_size)
{
	struct spi_device *spi = context;

	/* Insert pad byte for reading */
	u8 reg[] = { *(u8 *)reg_buf, 0 };

	if (reg_size + 1 != ARRAY_SIZE(reg)) {
		dev_err(&spi->dev, "Invalid register size %zu\n", reg_size);
		return -EINVAL;
	}

	return spi_write_then_read(spi, reg, ARRAY_SIZE(reg), val_buf,
				   val_size);
}

static int smi330_regmap_spi_write(void *context, const void *data,
				   size_t count)
{
	struct spi_device *spi = context;

	return spi_write(spi, data, count);
}

static const struct regmap_bus smi330_regmap_bus = {
	.read = smi330_regmap_spi_read,
	.write = smi330_regmap_spi_write,
	.read_flag_mask = 0x80,
};

static int smi330_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init(&spi->dev, &smi330_regmap_bus, &spi->dev,
				  &smi330_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(regmap),
				     "Failed to initialize SPI Regmap\n");

	return smi330_core_probe(&spi->dev, regmap);
}

static const struct spi_device_id smi330_spi_device_id[] = {
	{ .name = "smi330" },
	{ }
};
MODULE_DEVICE_TABLE(spi, smi330_spi_device_id);

static const struct of_device_id smi330_of_match[] = {
	{ .compatible = "bosch,smi330" },
	{ }
};
MODULE_DEVICE_TABLE(of, smi330_of_match);

static struct spi_driver smi330_spi_driver = {
	.probe = smi330_spi_probe,
	.id_table = smi330_spi_device_id,
	.driver = {
		.of_match_table = smi330_of_match,
		.name = "smi330_spi",
	},
};
module_spi_driver(smi330_spi_driver);

MODULE_AUTHOR("Stefan Gutmann <stefan.gutmann@de.bosch.com>");
MODULE_AUTHOR("Roman Huber <roman.huber@de.bosch.com>");
MODULE_AUTHOR("Filip Andrei <Andrei.Filip@ro.bosch.com>");
MODULE_AUTHOR("Drimbarean Avram Andrei <Avram-Andrei.Drimbarean@ro.bosch.com>");
MODULE_DESCRIPTION("Bosch SMI330 SPI driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS("IIO_SMI330");

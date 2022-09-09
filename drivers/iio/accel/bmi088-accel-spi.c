// SPDX-License-Identifier: GPL-2.0
/*
 * 3-axis accelerometer driver supporting following Bosch-Sensortec chips:
 *  - BMI088
 *
 * Copyright (c) 2018-2020, Topic Embedded Products
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "bmi088-accel.h"

static int bmi088_regmap_spi_write(void *context, const void *data, size_t count)
{
	struct spi_device *spi = context;

	/* Write register is same as generic SPI */
	return spi_write(spi, data, count);
}

static int bmi088_regmap_spi_read(void *context, const void *reg,
				size_t reg_size, void *val, size_t val_size)
{
	struct spi_device *spi = context;
	u8 addr[2];

	addr[0] = *(u8 *)reg;
	addr[0] |= BIT(7); /* Set RW = '1' */
	addr[1] = 0; /* Read requires a dummy byte transfer */

	return spi_write_then_read(spi, addr, sizeof(addr), val, val_size);
}

static struct regmap_bus bmi088_regmap_bus = {
	.write = bmi088_regmap_spi_write,
	.read = bmi088_regmap_spi_read,
};

static int bmi088_accel_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	const struct spi_device_id *id = spi_get_device_id(spi);

	regmap = devm_regmap_init(&spi->dev, &bmi088_regmap_bus,
			spi, &bmi088_regmap_conf);

	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to initialize spi regmap\n");
		return PTR_ERR(regmap);
	}

	return bmi088_accel_core_probe(&spi->dev, regmap, spi->irq,
					id->driver_data);
}

static void bmi088_accel_remove(struct spi_device *spi)
{
	bmi088_accel_core_remove(&spi->dev);
}

static const struct of_device_id bmi088_of_match[] = {
	{ .compatible = "bosch,bmi085-accel" },
	{ .compatible = "bosch,bmi088-accel" },
	{ .compatible = "bosch,bmi090l-accel" },
	{}
};
MODULE_DEVICE_TABLE(of, bmi088_of_match);

static const struct spi_device_id bmi088_accel_id[] = {
	{"bmi085-accel",  BOSCH_BMI085},
	{"bmi088-accel",  BOSCH_BMI088},
	{"bmi090l-accel", BOSCH_BMI090L},
	{}
};
MODULE_DEVICE_TABLE(spi, bmi088_accel_id);

static struct spi_driver bmi088_accel_driver = {
	.driver = {
		.name	= "bmi088_accel_spi",
		.pm	= &bmi088_accel_pm_ops,
		.of_match_table = bmi088_of_match,
	},
	.probe		= bmi088_accel_probe,
	.remove		= bmi088_accel_remove,
	.id_table	= bmi088_accel_id,
};
module_spi_driver(bmi088_accel_driver);

MODULE_AUTHOR("Niek van Agt <niek.van.agt@topicproducts.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMI088 accelerometer driver (SPI)");
MODULE_IMPORT_NS(IIO_BMI088);

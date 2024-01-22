// SPDX-License-Identifier: GPL-2.0
/*
 * SPI driver for Bosch BMI323 6-Axis IMU.
 *
 * Copyright (C) 2023, Jagath Jog J <jagathjog1996@gmail.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "bmi323.h"

/*
 * From BMI323 datasheet section 4: Notes on the Serial Interface Support.
 * Each SPI register read operation requires to read one dummy byte before
 * the actual payload.
 */
static int bmi323_regmap_spi_read(void *context, const void *reg_buf,
				  size_t reg_size, void *val_buf,
				  size_t val_size)
{
	struct spi_device *spi = context;

	return spi_write_then_read(spi, reg_buf, reg_size, val_buf, val_size);
}

static int bmi323_regmap_spi_write(void *context, const void *data,
				   size_t count)
{
	struct spi_device *spi = context;
	u8 *data_buff = (u8 *)data;

	data_buff[1] = data_buff[0];
	return spi_write(spi, data_buff + 1, count - 1);
}

static struct regmap_bus bmi323_regmap_bus = {
	.read = bmi323_regmap_spi_read,
	.write = bmi323_regmap_spi_write,
};

static const struct regmap_config bmi323_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.pad_bits = 8,
	.read_flag_mask = BIT(7),
	.max_register = BMI323_CFG_RES_REG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int bmi323_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct regmap *regmap;

	regmap = devm_regmap_init(dev, &bmi323_regmap_bus, dev,
				  &bmi323_spi_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize SPI Regmap\n");

	return bmi323_core_probe(dev);
}

static const struct spi_device_id bmi323_spi_ids[] = {
	{ "bmi323" },
	{ }
};
MODULE_DEVICE_TABLE(spi, bmi323_spi_ids);

static const struct of_device_id bmi323_of_spi_match[] = {
	{ .compatible = "bosch,bmi323" },
	{ }
};
MODULE_DEVICE_TABLE(of, bmi323_of_spi_match);

static struct spi_driver bmi323_spi_driver = {
	.driver = {
		.name = "bmi323",
		.of_match_table = bmi323_of_spi_match,
	},
	.probe = bmi323_spi_probe,
	.id_table = bmi323_spi_ids,
};
module_spi_driver(bmi323_spi_driver);

MODULE_DESCRIPTION("Bosch BMI323 IMU driver");
MODULE_AUTHOR("Jagath Jog J <jagathjog1996@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_BMI323);

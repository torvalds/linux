// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include "bmp280.h"

static int bmp280_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;
	const struct regmap_config *regmap_config;
	const struct i2c_device_id *id = i2c_client_get_device_id(client);

	switch (id->driver_data) {
	case BMP180_CHIP_ID:
		regmap_config = &bmp180_regmap_config;
		break;
	case BMP280_CHIP_ID:
	case BME280_CHIP_ID:
		regmap_config = &bmp280_regmap_config;
		break;
	case BMP380_CHIP_ID:
		regmap_config = &bmp380_regmap_config;
		break;
	default:
		return -EINVAL;
	}

	regmap = devm_regmap_init_i2c(client, regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	return bmp280_common_probe(&client->dev,
				   regmap,
				   id->driver_data,
				   id->name,
				   client->irq);
}

static const struct of_device_id bmp280_of_i2c_match[] = {
	{ .compatible = "bosch,bmp085", .data = (void *)BMP180_CHIP_ID },
	{ .compatible = "bosch,bmp180", .data = (void *)BMP180_CHIP_ID },
	{ .compatible = "bosch,bmp280", .data = (void *)BMP280_CHIP_ID },
	{ .compatible = "bosch,bme280", .data = (void *)BME280_CHIP_ID },
	{ .compatible = "bosch,bmp380", .data = (void *)BMP380_CHIP_ID },
	{ },
};
MODULE_DEVICE_TABLE(of, bmp280_of_i2c_match);

static const struct i2c_device_id bmp280_i2c_id[] = {
	{"bmp085", BMP180_CHIP_ID },
	{"bmp180", BMP180_CHIP_ID },
	{"bmp280", BMP280_CHIP_ID },
	{"bme280", BME280_CHIP_ID },
	{"bmp380", BMP380_CHIP_ID },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bmp280_i2c_id);

static struct i2c_driver bmp280_i2c_driver = {
	.driver = {
		.name	= "bmp280",
		.of_match_table = bmp280_of_i2c_match,
		.pm = pm_ptr(&bmp280_dev_pm_ops),
	},
	.probe_new	= bmp280_i2c_probe,
	.id_table	= bmp280_i2c_id,
};
module_i2c_driver(bmp280_i2c_driver);

MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_DESCRIPTION("Driver for Bosch Sensortec BMP180/BMP280 pressure and temperature sensor");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_BMP280);

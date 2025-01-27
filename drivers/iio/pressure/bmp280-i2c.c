// SPDX-License-Identifier: GPL-2.0-only
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bmp280.h"

static int bmp280_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	const struct bmp280_chip_info *chip_info;
	struct regmap *regmap;

	chip_info = i2c_get_match_data(client);

	regmap = devm_regmap_init_i2c(client, chip_info->regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	return bmp280_common_probe(&client->dev,
				   regmap,
				   chip_info,
				   id->name,
				   client->irq);
}

static const struct of_device_id bmp280_of_i2c_match[] = {
	{ .compatible = "bosch,bmp085", .data = &bmp085_chip_info },
	{ .compatible = "bosch,bmp180", .data = &bmp180_chip_info },
	{ .compatible = "bosch,bmp280", .data = &bmp280_chip_info },
	{ .compatible = "bosch,bme280", .data = &bme280_chip_info },
	{ .compatible = "bosch,bmp380", .data = &bmp380_chip_info },
	{ .compatible = "bosch,bmp580", .data = &bmp580_chip_info },
	{ },
};
MODULE_DEVICE_TABLE(of, bmp280_of_i2c_match);

static const struct i2c_device_id bmp280_i2c_id[] = {
	{"bmp085", (kernel_ulong_t)&bmp085_chip_info },
	{"bmp180", (kernel_ulong_t)&bmp180_chip_info },
	{"bmp280", (kernel_ulong_t)&bmp280_chip_info },
	{"bme280", (kernel_ulong_t)&bme280_chip_info },
	{"bmp380", (kernel_ulong_t)&bmp380_chip_info },
	{"bmp580", (kernel_ulong_t)&bmp580_chip_info },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bmp280_i2c_id);

static struct i2c_driver bmp280_i2c_driver = {
	.driver = {
		.name	= "bmp280",
		.of_match_table = bmp280_of_i2c_match,
		.pm = pm_ptr(&bmp280_dev_pm_ops),
	},
	.probe		= bmp280_i2c_probe,
	.id_table	= bmp280_i2c_id,
};
module_i2c_driver(bmp280_i2c_driver);

MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_DESCRIPTION("Driver for Bosch Sensortec BMP180/BMP280 pressure and temperature sensor");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_BMP280");

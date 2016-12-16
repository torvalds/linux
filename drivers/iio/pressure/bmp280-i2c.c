#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "bmp280.h"

static int bmp280_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const struct regmap_config *regmap_config;

	switch (id->driver_data) {
	case BMP180_CHIP_ID:
		regmap_config = &bmp180_regmap_config;
		break;
	case BMP280_CHIP_ID:
	case BME280_CHIP_ID:
		regmap_config = &bmp280_regmap_config;
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

static int bmp280_i2c_remove(struct i2c_client *client)
{
	return bmp280_common_remove(&client->dev);
}

static const struct acpi_device_id bmp280_acpi_i2c_match[] = {
	{"BMP0280", BMP280_CHIP_ID },
	{"BMP0180", BMP180_CHIP_ID },
	{"BMP0085", BMP180_CHIP_ID },
	{"BME0280", BME280_CHIP_ID },
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmp280_acpi_i2c_match);

#ifdef CONFIG_OF
static const struct of_device_id bmp280_of_i2c_match[] = {
	{ .compatible = "bosch,bme280", .data = (void *)BME280_CHIP_ID },
	{ .compatible = "bosch,bmp280", .data = (void *)BMP280_CHIP_ID },
	{ .compatible = "bosch,bmp180", .data = (void *)BMP180_CHIP_ID },
	{ .compatible = "bosch,bmp085", .data = (void *)BMP180_CHIP_ID },
	{ },
};
MODULE_DEVICE_TABLE(of, bmp280_of_i2c_match);
#else
#define bmp280_of_i2c_match NULL
#endif

static const struct i2c_device_id bmp280_i2c_id[] = {
	{"bmp280", BMP280_CHIP_ID },
	{"bmp180", BMP180_CHIP_ID },
	{"bmp085", BMP180_CHIP_ID },
	{"bme280", BME280_CHIP_ID },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bmp280_i2c_id);

static struct i2c_driver bmp280_i2c_driver = {
	.driver = {
		.name	= "bmp280",
		.acpi_match_table = ACPI_PTR(bmp280_acpi_i2c_match),
		.of_match_table = of_match_ptr(bmp280_of_i2c_match),
		.pm = &bmp280_dev_pm_ops,
	},
	.probe		= bmp280_i2c_probe,
	.remove		= bmp280_i2c_remove,
	.id_table	= bmp280_i2c_id,
};
module_i2c_driver(bmp280_i2c_driver);

MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_DESCRIPTION("Driver for Bosch Sensortec BMP180/BMP280 pressure and temperature sensor");
MODULE_LICENSE("GPL v2");

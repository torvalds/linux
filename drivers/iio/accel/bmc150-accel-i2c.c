// SPDX-License-Identifier: GPL-2.0-only
/*
 * 3-axis accelerometer driver supporting following I2C Bosch-Sensortec chips:
 *  - BMC150
 *  - BMI055
 *  - BMA255
 *  - BMA250E
 *  - BMA222
 *  - BMA222E
 *  - BMA280
 *
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/regmap.h>

#include "bmc150-accel.h"

#ifdef CONFIG_ACPI
static const struct acpi_device_id bmc150_acpi_dual_accel_ids[] = {
	{"BOSC0200"},
	{"DUAL250E"},
	{ }
};

/*
 * Some acpi_devices describe 2 accelerometers in a single ACPI device,
 * try instantiating a second i2c_client for an I2cSerialBusV2 ACPI resource
 * with index 1.
 */
static void bmc150_acpi_dual_accel_probe(struct i2c_client *client)
{
	struct bmc150_accel_data *data = iio_priv(i2c_get_clientdata(client));
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);
	char dev_name[16];
	struct i2c_board_info board_info = {
		.type = "bmc150_accel",
		.dev_name = dev_name,
		.fwnode = client->dev.fwnode,
	};

	if (acpi_match_device_ids(adev, bmc150_acpi_dual_accel_ids))
		return;

	/*
	 * The 2nd accel sits in the base of 2-in-1s. The suffix is static, as
	 * there should never be more then 1 ACPI node with 2 accelerometers.
	 */
	snprintf(dev_name, sizeof(dev_name), "%s:base", acpi_device_hid(adev));

	board_info.irq = acpi_dev_gpio_irq_get(adev, 1);

	data->second_device = i2c_acpi_new_device(&client->dev, 1, &board_info);
}

static void bmc150_acpi_dual_accel_remove(struct i2c_client *client)
{
	struct bmc150_accel_data *data = iio_priv(i2c_get_clientdata(client));

	i2c_unregister_device(data->second_device);
}
#else
static void bmc150_acpi_dual_accel_probe(struct i2c_client *client) {}
static void bmc150_acpi_dual_accel_remove(struct i2c_client *client) {}
#endif

static int bmc150_accel_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const char *name = NULL;
	bool block_supported =
		i2c_check_functionality(client->adapter, I2C_FUNC_I2C) ||
		i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_READ_I2C_BLOCK);
	int ret;

	regmap = devm_regmap_init_i2c(client, &bmc150_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to initialize i2c regmap\n");
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	ret = bmc150_accel_core_probe(&client->dev, regmap, client->irq, name, block_supported);
	if (ret)
		return ret;

	/*
	 * The !id check avoids recursion when probe() gets called
	 * for the second client.
	 */
	if (!id && has_acpi_companion(&client->dev))
		bmc150_acpi_dual_accel_probe(client);

	return 0;
}

static int bmc150_accel_remove(struct i2c_client *client)
{
	bmc150_acpi_dual_accel_remove(client);

	return bmc150_accel_core_remove(&client->dev);
}

static const struct acpi_device_id bmc150_accel_acpi_match[] = {
	{"BSBA0150",	bmc150},
	{"BMC150A",	bmc150},
	{"BMI055A",	bmi055},
	{"BMA0255",	bma255},
	{"BMA250E",	bma250e},
	{"BMA222",	bma222},
	{"BMA222E",	bma222e},
	{"BMA0280",	bma280},
	{"BOSC0200"},
	{"DUAL250E"},
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmc150_accel_acpi_match);

static const struct i2c_device_id bmc150_accel_id[] = {
	{"bmc150_accel",	bmc150},
	{"bmi055_accel",	bmi055},
	{"bma255",		bma255},
	{"bma250e",		bma250e},
	{"bma222",		bma222},
	{"bma222e",		bma222e},
	{"bma280",		bma280},
	{}
};

MODULE_DEVICE_TABLE(i2c, bmc150_accel_id);

static const struct of_device_id bmc150_accel_of_match[] = {
	{ .compatible = "bosch,bmc150_accel" },
	{ .compatible = "bosch,bmi055_accel" },
	{ .compatible = "bosch,bma255" },
	{ .compatible = "bosch,bma250e" },
	{ .compatible = "bosch,bma222" },
	{ .compatible = "bosch,bma222e" },
	{ .compatible = "bosch,bma280" },
	{ },
};
MODULE_DEVICE_TABLE(of, bmc150_accel_of_match);

static struct i2c_driver bmc150_accel_driver = {
	.driver = {
		.name	= "bmc150_accel_i2c",
		.of_match_table = bmc150_accel_of_match,
		.acpi_match_table = ACPI_PTR(bmc150_accel_acpi_match),
		.pm	= &bmc150_accel_pm_ops,
	},
	.probe		= bmc150_accel_probe,
	.remove		= bmc150_accel_remove,
	.id_table	= bmc150_accel_id,
};
module_i2c_driver(bmc150_accel_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 I2C accelerometer driver");

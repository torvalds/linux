// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/common/st_sensors_i2c.h>
#include "st_pressure.h"

static const struct of_device_id st_press_of_match[] = {
	{
		.compatible = "st,lps001wp-press",
		.data = LPS001WP_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps25h-press",
		.data = LPS25H_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps331ap-press",
		.data = LPS331AP_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps22hb-press",
		.data = LPS22HB_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps33hw",
		.data = LPS33HW_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps35hw",
		.data = LPS35HW_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps22hh",
		.data = LPS22HH_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps22df",
		.data = LPS22DF_PRESS_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_press_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id st_press_acpi_match[] = {
	{"SNO9210", LPS22HB},
	{ },
};
MODULE_DEVICE_TABLE(acpi, st_press_acpi_match);
#endif

static const struct i2c_device_id st_press_id_table[] = {
	{ LPS001WP_PRESS_DEV_NAME, LPS001WP },
	{ LPS25H_PRESS_DEV_NAME,  LPS25H },
	{ LPS331AP_PRESS_DEV_NAME, LPS331AP },
	{ LPS22HB_PRESS_DEV_NAME, LPS22HB },
	{ LPS33HW_PRESS_DEV_NAME, LPS33HW },
	{ LPS35HW_PRESS_DEV_NAME, LPS35HW },
	{ LPS22HH_PRESS_DEV_NAME, LPS22HH },
	{ LPS22DF_PRESS_DEV_NAME, LPS22DF },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_press_id_table);

static int st_press_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	const struct st_sensor_settings *settings;
	struct st_sensor_data *press_data;
	struct iio_dev *indio_dev;
	int ret;

	st_sensors_dev_name_probe(&client->dev, client->name, sizeof(client->name));

	settings = st_press_get_settings(client->name);
	if (!settings) {
		dev_err(&client->dev, "device name %s not recognized.\n",
			client->name);
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*press_data));
	if (!indio_dev)
		return -ENOMEM;

	press_data = iio_priv(indio_dev);
	press_data->sensor_settings = (struct st_sensor_settings *)settings;

	ret = st_sensors_i2c_configure(indio_dev, client);
	if (ret < 0)
		return ret;

	ret = st_sensors_power_enable(indio_dev);
	if (ret)
		return ret;

	return st_press_common_probe(indio_dev);
}

static struct i2c_driver st_press_driver = {
	.driver = {
		.name = "st-press-i2c",
		.of_match_table = st_press_of_match,
		.acpi_match_table = ACPI_PTR(st_press_acpi_match),
	},
	.probe = st_press_i2c_probe,
	.id_table = st_press_id_table,
};
module_i2c_driver(st_press_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics pressures i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ST_SENSORS);

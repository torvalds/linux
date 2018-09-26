/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/common/st_sensors_i2c.h>
#include "st_pressure.h"

#ifdef CONFIG_OF
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
	{},
};
MODULE_DEVICE_TABLE(of, st_press_of_match);
#else
#define st_press_of_match NULL
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id st_press_acpi_match[] = {
	{"SNO9210", LPS22HB},
	{ },
};
MODULE_DEVICE_TABLE(acpi, st_press_acpi_match);
#else
#define st_press_acpi_match NULL
#endif

static const struct i2c_device_id st_press_id_table[] = {
	{ LPS001WP_PRESS_DEV_NAME, LPS001WP },
	{ LPS25H_PRESS_DEV_NAME,  LPS25H },
	{ LPS331AP_PRESS_DEV_NAME, LPS331AP },
	{ LPS22HB_PRESS_DEV_NAME, LPS22HB },
	{ LPS33HW_PRESS_DEV_NAME, LPS33HW },
	{ LPS35HW_PRESS_DEV_NAME, LPS35HW },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_press_id_table);

static int st_press_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct st_sensor_data *press_data;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*press_data));
	if (!indio_dev)
		return -ENOMEM;

	press_data = iio_priv(indio_dev);

	if (client->dev.of_node) {
		st_sensors_of_name_probe(&client->dev, st_press_of_match,
					 client->name, sizeof(client->name));
	} else if (ACPI_HANDLE(&client->dev)) {
		ret = st_sensors_match_acpi_device(&client->dev);
		if ((ret < 0) || (ret >= ST_PRESS_MAX))
			return -ENODEV;

		strlcpy(client->name, st_press_id_table[ret].name,
				sizeof(client->name));
	} else if (!id)
		return -ENODEV;

	st_sensors_i2c_configure(indio_dev, client, press_data);

	ret = st_press_common_probe(indio_dev);
	if (ret < 0)
		return ret;

	return 0;
}

static int st_press_i2c_remove(struct i2c_client *client)
{
	st_press_common_remove(i2c_get_clientdata(client));

	return 0;
}

static struct i2c_driver st_press_driver = {
	.driver = {
		.name = "st-press-i2c",
		.of_match_table = of_match_ptr(st_press_of_match),
		.acpi_match_table = ACPI_PTR(st_press_acpi_match),
	},
	.probe = st_press_i2c_probe,
	.remove = st_press_i2c_remove,
	.id_table = st_press_id_table,
};
module_i2c_driver(st_press_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics pressures i2c driver");
MODULE_LICENSE("GPL v2");

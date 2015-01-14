/*
 * si702x.c - SI702x Ambient light Sensor driver
 *
 * Copyright (C) 2014 Hardkernel Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/err.h>

#define SI702X_NAME		"si702x"
#define SI702X_ADDRESS		0x40
#define SI702X_CHIP_ID		0x32

#define SI702X_CMD_MEASURE_TEMPERATURE_HOLD	0xE3
#define SI702X_CMD_MEASURE_HUMIDITY_HOLD	0xE5
#define SI702X_CMD_READ_PREVIOUS_TEMPERATURE	0xE0

static const unsigned short normal_i2c[] = { SI702X_ADDRESS,
						I2C_CLIENT_END };
struct si702x_data {
	struct device *dev;
	struct regmap *regmap;
	struct mutex lock;
	u16	raw_temperature;
	u16	raw_humidity;
	u8	chip_id;
};

static s32 si702x_update_raw_humidity(struct si702x_data *data)
{
	u16 humidity;
	s32 status;

	mutex_lock(&data->lock);

	status = regmap_bulk_read(data->regmap,
		SI702X_CMD_MEASURE_HUMIDITY_HOLD, &humidity, sizeof(humidity));
	if (status < 0) {
		dev_err(data->dev,
			"Error while reading ir index measurement result\n");
		goto exit;
	}
	msleep(10);
	data->raw_humidity = be16_to_cpu(humidity);
	status = 0;

exit:
	mutex_unlock(&data->lock);
	return status;
}

static u32 si702x_humidity_calibration(u16 raw_humidity)
{
	u32 humidity;
	humidity = raw_humidity;
        humidity = (humidity*12500/65536) - 600;

	return humidity;
}

static s32 si702x_get_humidity(struct si702x_data *data, u32 *humidity)
{
	int status;
	status = si702x_update_raw_humidity(data);
	if (status < 0)
		goto exit;
	*humidity = si702x_humidity_calibration(data->raw_humidity);
exit:
	return status;
}

static ssize_t show_humidity(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int humidity;
	int status;
	struct si702x_data *data = dev_get_drvdata(dev);

	status = si702x_get_humidity(data, &humidity);
	if (status < 0)
		return status;
	else
		return sprintf(buf, "%d.%02d\n", humidity/100, humidity%100);
}
static DEVICE_ATTR(humidity, S_IRUGO, show_humidity, NULL);

static s32 si702x_update_raw_temperature(struct si702x_data *data)
{
	u16 temperature;
	s32 status;

	mutex_lock(&data->lock);
	status = regmap_bulk_read(data->regmap,
					SI702X_CMD_MEASURE_TEMPERATURE_HOLD,
					&temperature, sizeof(temperature));
	if (status < 0) {
		dev_err(data->dev,
			"Error while reading ir index measurement result\n");
		goto exit;
	}
	msleep(10);

	data->raw_temperature = be16_to_cpu(temperature);
	status = 0;
exit:
	mutex_unlock(&data->lock);
	return status;
}

static u32 si702x_temperature_calibration(u16 raw_temperature)
{
	u32 temperature;
	temperature = raw_temperature;
        temperature = (temperature*17572/65536) - 4685;

	return temperature;
}

static s32 si702x_get_temperature(struct si702x_data *data, int *temperature)
{
	int status;
	status = si702x_update_raw_temperature(data);
	if (status < 0)
		goto exit;
	*temperature = si702x_temperature_calibration(data->raw_temperature);
exit:
	return status;
}

static ssize_t show_temperature(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int temperature;
	int status;
	struct si702x_data *data = dev_get_drvdata(dev);

	status = si702x_get_temperature(data, &temperature);
	if (status < 0)
		return status;
	else
		return sprintf(buf, "%d.%02d\n", temperature/100, temperature%100);
}
static DEVICE_ATTR(temperature, S_IRUGO, show_temperature, NULL);

static struct attribute *si702x_attributes[] = {
	&dev_attr_temperature.attr,
	&dev_attr_humidity.attr,
	NULL
};

static const struct attribute_group si702x_attr_group = {
	.attrs = si702x_attributes,
};

struct regmap_config si702x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8
};

static int si702x_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct si702x_data *data;
	int err = 0;

	struct regmap *regmap = devm_regmap_init_i2c(client,
							&si702x_regmap_config);
	if (IS_ERR(regmap)) {
		err = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to init regmap: %d\n", err);
		return err;
	}

	data = kzalloc(sizeof(struct si702x_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	dev_set_drvdata(&client->dev, data);
	data->dev = &client->dev;
	data->regmap = regmap;

	mutex_init(&data->lock);

	/* Register sysfs hooks */
	err = sysfs_create_group(&data->dev->kobj, &si702x_attr_group);
	if (err)
		goto exit_free;

	dev_info(&client->dev, "Successfully initialized %s!\n", SI702X_NAME);

	return 0;

exit_free:
	kfree(data);
exit:
	return err;
}

static int si702x_remove(struct i2c_client *client)
{
	struct si702x_data *data = dev_get_drvdata(&client->dev);

	sysfs_remove_group(&data->dev->kobj, &si702x_attr_group);
	kfree(data);

	return 0;
}

static const struct i2c_device_id si702x_id[] = {
	{ SI702X_NAME, 0 },
	{ "si702x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si702x_id);

static struct i2c_driver bmp085_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SI702X_NAME,
	},
	.id_table	= si702x_id,
	.probe		= si702x_probe,
	.remove		= si702x_remove,
	.address_list	= normal_i2c
};

module_i2c_driver(bmp085_i2c_driver);

MODULE_AUTHOR("John Lee <john.lee@hardkernel.com>");
MODULE_DESCRIPTION("SI702X I2C bus driver");
MODULE_LICENSE("GPL");

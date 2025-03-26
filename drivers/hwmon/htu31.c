// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The driver for Measurement Specialties HTU31 Temperature and Humidity sensor.
 *
 * Copyright (C) 2025
 * Author: Andrei Lalaev <andrey.lalaev@gmail.com>
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>

#define HTU31_READ_TEMP_HUM_CMD	0x00
#define HTU31_READ_SERIAL_CMD		0x0a
#define HTU31_CONVERSION_CMD		0x5e
#define HTU31_HEATER_OFF_CMD		0x02
#define HTU31_HEATER_ON_CMD		0x04

#define HTU31_TEMP_HUM_LEN		6

/* Conversion time for the highest resolution */
#define HTU31_HUMIDITY_CONV_TIME	10000 /* us */
#define HTU31_TEMPERATURE_CONV_TIME	15000 /* us */

#define HTU31_SERIAL_NUMBER_LEN	3
#define HTU31_SERIAL_NUMBER_CRC_LEN	1
#define HTU31_SERIAL_NUMBER_CRC_OFFSET	3

#define HTU31_CRC8_INIT_VAL		0
#define HTU31_CRC8_POLYNOMIAL		0x31
DECLARE_CRC8_TABLE(htu31_crc8_table);

/**
 * struct htu31_data - all the data required to operate a HTU31 chip
 * @client: the i2c client associated with the HTU31
 * @lock: a mutex to prevent parallel access to the data
 * @wait_time: the time needed by sensor to convert values
 * @temperature: the latest temperature value in millidegrees
 * @humidity: the latest relative humidity value in millipercent
 * @serial_number: the serial number of the sensor
 * @heater_enable: the internal state of the heater
 */
struct htu31_data {
	struct i2c_client *client;
	struct mutex lock; /* Used to protect against parallel data updates */
	long wait_time;
	long temperature;
	long humidity;
	u8 serial_number[HTU31_SERIAL_NUMBER_LEN];
	bool heater_enable;
};

static long htu31_temp_to_millicelsius(u16 val)
{
	return -40000 + DIV_ROUND_CLOSEST_ULL(165000ULL * val, 65535);
}

static long htu31_relative_humidity(u16 val)
{
	return DIV_ROUND_CLOSEST_ULL(100000ULL * val, 65535);
}

static int htu31_data_fetch_command(struct htu31_data *data)
{
	struct i2c_client *client = data->client;
	u8 conversion_on = HTU31_CONVERSION_CMD;
	u8 read_data_cmd = HTU31_READ_TEMP_HUM_CMD;
	u8 t_h_buf[HTU31_TEMP_HUM_LEN] = {};
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &read_data_cmd,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(t_h_buf),
			.buf = t_h_buf,
		},
	};
	int ret;
	u8 crc;

	guard(mutex)(&data->lock);

	ret = i2c_master_send(client, &conversion_on, 1);
	if (ret != 1) {
		ret = ret < 0 ? ret : -EIO;
		dev_err(&client->dev,
			"Conversion command is failed. Error code: %d\n", ret);
		return ret;
	}

	fsleep(data->wait_time);

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		ret = ret < 0 ? ret : -EIO;
		dev_err(&client->dev,
			"T&H command is failed. Error code: %d\n", ret);
		return ret;
	}

	crc = crc8(htu31_crc8_table, &t_h_buf[0], 2, HTU31_CRC8_INIT_VAL);
	if (crc != t_h_buf[2]) {
		dev_err(&client->dev, "Temperature CRC mismatch\n");
		return -EIO;
	}

	crc = crc8(htu31_crc8_table, &t_h_buf[3], 2, HTU31_CRC8_INIT_VAL);
	if (crc != t_h_buf[5]) {
		dev_err(&client->dev, "Humidity CRC mismatch\n");
		return -EIO;
	}

	data->temperature = htu31_temp_to_millicelsius(be16_to_cpup((__be16 *)&t_h_buf[0]));
	data->humidity = htu31_relative_humidity(be16_to_cpup((__be16 *)&t_h_buf[3]));

	return 0;
}

static umode_t htu31_is_visible(const void *data, enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
	case hwmon_humidity:
		return 0444;
	default:
		return 0;
	}
}

static int htu31_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	struct htu31_data *data = dev_get_drvdata(dev);
	int ret;

	ret = htu31_data_fetch_command(data);
	if (ret < 0)
		return ret;

	switch (type) {
	case hwmon_temp:
		if (attr != hwmon_temp_input)
			return -EINVAL;

		*val = data->temperature;
		break;
	case hwmon_humidity:
		if (attr != hwmon_humidity_input)
			return -EINVAL;

		*val = data->humidity;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int htu31_read_serial_number(struct htu31_data *data)
{
	struct i2c_client *client = data->client;
	u8 read_sn_cmd = HTU31_READ_SERIAL_CMD;
	u8 sn_buf[HTU31_SERIAL_NUMBER_LEN + HTU31_SERIAL_NUMBER_CRC_LEN];
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &read_sn_cmd,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(sn_buf),
			.buf = sn_buf,
		},
	};
	int ret;
	u8 crc;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;

	crc = crc8(htu31_crc8_table, sn_buf, HTU31_SERIAL_NUMBER_LEN, HTU31_CRC8_INIT_VAL);
	if (crc != sn_buf[HTU31_SERIAL_NUMBER_CRC_OFFSET]) {
		dev_err(&client->dev, "Serial number CRC mismatch\n");
		return -EIO;
	}

	memcpy(data->serial_number, sn_buf, HTU31_SERIAL_NUMBER_LEN);

	return 0;
}

static ssize_t heater_enable_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct htu31_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", data->heater_enable);
}

static ssize_t heater_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	struct htu31_data *data = dev_get_drvdata(dev);
	u8 heater_cmd;
	bool status;
	int ret;

	ret = kstrtobool(buf, &status);
	if (ret)
		return ret;

	heater_cmd = status ? HTU31_HEATER_ON_CMD : HTU31_HEATER_OFF_CMD;

	guard(mutex)(&data->lock);

	ret = i2c_master_send(data->client, &heater_cmd, 1);
	if (ret < 0)
		return ret;

	data->heater_enable = status;

	return count;
}

static DEVICE_ATTR_RW(heater_enable);

static int serial_number_show(struct seq_file *seq_file,
			      void *unused)
{
	struct htu31_data *data = seq_file->private;

	seq_printf(seq_file, "%X%X%X\n", data->serial_number[0],
		   data->serial_number[1], data->serial_number[2]);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(serial_number);

static struct attribute *htu31_attrs[] = {
	&dev_attr_heater_enable.attr,
	NULL
};

ATTRIBUTE_GROUPS(htu31);

static const struct hwmon_channel_info * const htu31_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(humidity, HWMON_H_INPUT),
	NULL
};

static const struct hwmon_ops htu31_hwmon_ops = {
	.is_visible = htu31_is_visible,
	.read = htu31_read,
};

static const struct hwmon_chip_info htu31_chip_info = {
	.info = htu31_info,
	.ops = &htu31_hwmon_ops,
};

static int htu31_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct htu31_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->wait_time = HTU31_TEMPERATURE_CONV_TIME + HTU31_HUMIDITY_CONV_TIME;

	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	crc8_populate_msb(htu31_crc8_table, HTU31_CRC8_POLYNOMIAL);

	ret = htu31_read_serial_number(data);
	if (ret) {
		dev_err(dev, "Failed to read serial number\n");
		return ret;
	}

	debugfs_create_file("serial_number",
			    0444,
			    client->debugfs,
			    data,
			    &serial_number_fops);

	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 client->name,
							 data,
							 &htu31_chip_info,
							 htu31_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id htu31_id[] = {
	{ "htu31" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, htu31_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id htu31_of_match[] = {
	{ .compatible = "meas,htu31" },
	{ }
};
MODULE_DEVICE_TABLE(of, htu31_of_match);
#endif

static struct i2c_driver htu31_driver = {
	.driver = {
		.name = "htu31",
		.of_match_table = of_match_ptr(htu31_of_match),
	},
	.probe = htu31_probe,
	.id_table = htu31_id,
};
module_i2c_driver(htu31_driver);

MODULE_AUTHOR("Andrei Lalaev <andrey.lalaev@gmail.com>");
MODULE_DESCRIPTION("HTU31 Temperature and Humidity sensor driver");
MODULE_LICENSE("GPL");

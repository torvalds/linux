/* Sensirion SHT3x-DIS humidity and temperature sensor driver.
 * The SHT3x comes in many different versions, this driver is for the
 * I2C version only.
 *
 * Copyright (C) 2016 Sensirion AG, Switzerland
 * Author: David Frey <david.frey@sensirion.com>
 * Author: Pascal Sachs <pascal.sachs@sensirion.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/page.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_data/sht3x.h>

/* commands (high precision mode) */
static const unsigned char sht3x_cmd_measure_blocking_hpm[]    = { 0x2c, 0x06 };
static const unsigned char sht3x_cmd_measure_nonblocking_hpm[] = { 0x24, 0x00 };

/* commands (low power mode) */
static const unsigned char sht3x_cmd_measure_blocking_lpm[]    = { 0x2c, 0x10 };
static const unsigned char sht3x_cmd_measure_nonblocking_lpm[] = { 0x24, 0x16 };

/* commands for periodic mode */
static const unsigned char sht3x_cmd_measure_periodic_mode[]   = { 0xe0, 0x00 };
static const unsigned char sht3x_cmd_break[]                   = { 0x30, 0x93 };

/* commands for heater control */
static const unsigned char sht3x_cmd_heater_on[]               = { 0x30, 0x6d };
static const unsigned char sht3x_cmd_heater_off[]              = { 0x30, 0x66 };

/* other commands */
static const unsigned char sht3x_cmd_read_status_reg[]         = { 0xf3, 0x2d };
static const unsigned char sht3x_cmd_clear_status_reg[]        = { 0x30, 0x41 };

/* delays for non-blocking i2c commands, both in us */
#define SHT3X_NONBLOCKING_WAIT_TIME_HPM  15000
#define SHT3X_NONBLOCKING_WAIT_TIME_LPM   4000

#define SHT3X_WORD_LEN         2
#define SHT3X_CMD_LENGTH       2
#define SHT3X_CRC8_LEN         1
#define SHT3X_RESPONSE_LENGTH  6
#define SHT3X_CRC8_POLYNOMIAL  0x31
#define SHT3X_CRC8_INIT        0xFF
#define SHT3X_MIN_TEMPERATURE  -45000
#define SHT3X_MAX_TEMPERATURE  130000
#define SHT3X_MIN_HUMIDITY     0
#define SHT3X_MAX_HUMIDITY     100000

enum sht3x_chips {
	sht3x,
	sts3x,
};

enum sht3x_limits {
	limit_max = 0,
	limit_max_hyst,
	limit_min,
	limit_min_hyst,
};

DECLARE_CRC8_TABLE(sht3x_crc8_table);

/* periodic measure commands (high precision mode) */
static const char periodic_measure_commands_hpm[][SHT3X_CMD_LENGTH] = {
	/* 0.5 measurements per second */
	{0x20, 0x32},
	/* 1 measurements per second */
	{0x21, 0x30},
	/* 2 measurements per second */
	{0x22, 0x36},
	/* 4 measurements per second */
	{0x23, 0x34},
	/* 10 measurements per second */
	{0x27, 0x37},
};

/* periodic measure commands (low power mode) */
static const char periodic_measure_commands_lpm[][SHT3X_CMD_LENGTH] = {
	/* 0.5 measurements per second */
	{0x20, 0x2f},
	/* 1 measurements per second */
	{0x21, 0x2d},
	/* 2 measurements per second */
	{0x22, 0x2b},
	/* 4 measurements per second */
	{0x23, 0x29},
	/* 10 measurements per second */
	{0x27, 0x2a},
};

struct sht3x_limit_commands {
	const char read_command[SHT3X_CMD_LENGTH];
	const char write_command[SHT3X_CMD_LENGTH];
};

static const struct sht3x_limit_commands limit_commands[] = {
	/* temp1_max, humidity1_max */
	[limit_max] = { {0xe1, 0x1f}, {0x61, 0x1d} },
	/* temp_1_max_hyst, humidity1_max_hyst */
	[limit_max_hyst] = { {0xe1, 0x14}, {0x61, 0x16} },
	/* temp1_min, humidity1_min */
	[limit_min] = { {0xe1, 0x02}, {0x61, 0x00} },
	/* temp_1_min_hyst, humidity1_min_hyst */
	[limit_min_hyst] = { {0xe1, 0x09}, {0x61, 0x0B} },
};

#define SHT3X_NUM_LIMIT_CMD  ARRAY_SIZE(limit_commands)

static const u16 mode_to_update_interval[] = {
	   0,
	2000,
	1000,
	 500,
	 250,
	 100,
};

struct sht3x_data {
	struct i2c_client *client;
	struct mutex i2c_lock; /* lock for sending i2c commands */
	struct mutex data_lock; /* lock for updating driver data */

	u8 mode;
	const unsigned char *command;
	u32 wait_time;			/* in us*/
	unsigned long last_update;	/* last update in periodic mode*/

	struct sht3x_platform_data setup;

	/*
	 * cached values for temperature and humidity and limits
	 * the limits arrays have the following order:
	 * max, max_hyst, min, min_hyst
	 */
	int temperature;
	int temperature_limits[SHT3X_NUM_LIMIT_CMD];
	u32 humidity;
	u32 humidity_limits[SHT3X_NUM_LIMIT_CMD];
};

static u8 get_mode_from_update_interval(u16 value)
{
	size_t index;
	u8 number_of_modes = ARRAY_SIZE(mode_to_update_interval);

	if (value == 0)
		return 0;

	/* find next faster update interval */
	for (index = 1; index < number_of_modes; index++) {
		if (mode_to_update_interval[index] <= value)
			return index;
	}

	return number_of_modes - 1;
}

static int sht3x_read_from_command(struct i2c_client *client,
				   struct sht3x_data *data,
				   const char *command,
				   char *buf, int length, u32 wait_time)
{
	int ret;

	mutex_lock(&data->i2c_lock);
	ret = i2c_master_send(client, command, SHT3X_CMD_LENGTH);

	if (ret != SHT3X_CMD_LENGTH) {
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	if (wait_time)
		usleep_range(wait_time, wait_time + 1000);

	ret = i2c_master_recv(client, buf, length);
	if (ret != length) {
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	ret = 0;
out:
	mutex_unlock(&data->i2c_lock);
	return ret;
}

static int sht3x_extract_temperature(u16 raw)
{
	/*
	 * From datasheet:
	 * T = -45 + 175 * ST / 2^16
	 * Adapted for integer fixed point (3 digit) arithmetic.
	 */
	return ((21875 * (int)raw) >> 13) - 45000;
}

static u32 sht3x_extract_humidity(u16 raw)
{
	/*
	 * From datasheet:
	 * RH = 100 * SRH / 2^16
	 * Adapted for integer fixed point (3 digit) arithmetic.
	 */
	return (12500 * (u32)raw) >> 13;
}

static struct sht3x_data *sht3x_update_client(struct device *dev)
{
	struct sht3x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u16 interval_ms = mode_to_update_interval[data->mode];
	unsigned long interval_jiffies = msecs_to_jiffies(interval_ms);
	unsigned char buf[SHT3X_RESPONSE_LENGTH];
	u16 val;
	int ret = 0;

	mutex_lock(&data->data_lock);
	/*
	 * Only update cached readings once per update interval in periodic
	 * mode. In single shot mode the sensor measures values on demand, so
	 * every time the sysfs interface is called, a measurement is triggered.
	 * In periodic mode however, the measurement process is handled
	 * internally by the sensor and reading out sensor values only makes
	 * sense if a new reading is available.
	 */
	if (time_after(jiffies, data->last_update + interval_jiffies)) {
		ret = sht3x_read_from_command(client, data, data->command, buf,
					      sizeof(buf), data->wait_time);
		if (ret)
			goto out;

		val = be16_to_cpup((__be16 *)buf);
		data->temperature = sht3x_extract_temperature(val);
		val = be16_to_cpup((__be16 *)(buf + 3));
		data->humidity = sht3x_extract_humidity(val);
		data->last_update = jiffies;
	}

out:
	mutex_unlock(&data->data_lock);
	if (ret)
		return ERR_PTR(ret);

	return data;
}

/* sysfs attributes */
static ssize_t temp1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sht3x_data *data = sht3x_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->temperature);
}

static ssize_t humidity1_input_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct sht3x_data *data = sht3x_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%u\n", data->humidity);
}

/*
 * limits_update must only be called from probe or with data_lock held
 */
static int limits_update(struct sht3x_data *data)
{
	int ret;
	u8 index;
	int temperature;
	u32 humidity;
	u16 raw;
	char buffer[SHT3X_RESPONSE_LENGTH];
	const struct sht3x_limit_commands *commands;
	struct i2c_client *client = data->client;

	for (index = 0; index < SHT3X_NUM_LIMIT_CMD; index++) {
		commands = &limit_commands[index];
		ret = sht3x_read_from_command(client, data,
					      commands->read_command, buffer,
					      SHT3X_RESPONSE_LENGTH, 0);

		if (ret)
			return ret;

		raw = be16_to_cpup((__be16 *)buffer);
		temperature = sht3x_extract_temperature((raw & 0x01ff) << 7);
		humidity = sht3x_extract_humidity(raw & 0xfe00);
		data->temperature_limits[index] = temperature;
		data->humidity_limits[index] = humidity;
	}

	return ret;
}

static ssize_t temp1_limit_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct sht3x_data *data = dev_get_drvdata(dev);
	u8 index = to_sensor_dev_attr(attr)->index;
	int temperature_limit = data->temperature_limits[index];

	return scnprintf(buf, PAGE_SIZE, "%d\n", temperature_limit);
}

static ssize_t humidity1_limit_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct sht3x_data *data = dev_get_drvdata(dev);
	u8 index = to_sensor_dev_attr(attr)->index;
	u32 humidity_limit = data->humidity_limits[index];

	return scnprintf(buf, PAGE_SIZE, "%u\n", humidity_limit);
}

/*
 * limit_store must only be called with data_lock held
 */
static size_t limit_store(struct device *dev,
			  size_t count,
			  u8 index,
			  int temperature,
			  u32 humidity)
{
	char buffer[SHT3X_CMD_LENGTH + SHT3X_WORD_LEN + SHT3X_CRC8_LEN];
	char *position = buffer;
	int ret;
	u16 raw;
	struct sht3x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	const struct sht3x_limit_commands *commands;

	commands = &limit_commands[index];

	memcpy(position, commands->write_command, SHT3X_CMD_LENGTH);
	position += SHT3X_CMD_LENGTH;
	/*
	 * ST = (T + 45) / 175 * 2^16
	 * SRH = RH / 100 * 2^16
	 * adapted for fixed point arithmetic and packed the same as
	 * in limit_show()
	 */
	raw = ((u32)(temperature + 45000) * 24543) >> (16 + 7);
	raw |= ((humidity * 42950) >> 16) & 0xfe00;

	*((__be16 *)position) = cpu_to_be16(raw);
	position += SHT3X_WORD_LEN;
	*position = crc8(sht3x_crc8_table,
			 position - SHT3X_WORD_LEN,
			 SHT3X_WORD_LEN,
			 SHT3X_CRC8_INIT);

	mutex_lock(&data->i2c_lock);
	ret = i2c_master_send(client, buffer, sizeof(buffer));
	mutex_unlock(&data->i2c_lock);

	if (ret != sizeof(buffer))
		return ret < 0 ? ret : -EIO;

	data->temperature_limits[index] = temperature;
	data->humidity_limits[index] = humidity;
	return count;
}

static ssize_t temp1_limit_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	int temperature;
	int ret;
	struct sht3x_data *data = dev_get_drvdata(dev);
	u8 index = to_sensor_dev_attr(attr)->index;

	ret = kstrtoint(buf, 0, &temperature);
	if (ret)
		return ret;

	temperature = clamp_val(temperature, SHT3X_MIN_TEMPERATURE,
				SHT3X_MAX_TEMPERATURE);
	mutex_lock(&data->data_lock);
	ret = limit_store(dev, count, index, temperature,
			  data->humidity_limits[index]);
	mutex_unlock(&data->data_lock);

	return ret;
}

static ssize_t humidity1_limit_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	u32 humidity;
	int ret;
	struct sht3x_data *data = dev_get_drvdata(dev);
	u8 index = to_sensor_dev_attr(attr)->index;

	ret = kstrtou32(buf, 0, &humidity);
	if (ret)
		return ret;

	humidity = clamp_val(humidity, SHT3X_MIN_HUMIDITY, SHT3X_MAX_HUMIDITY);
	mutex_lock(&data->data_lock);
	ret = limit_store(dev, count, index, data->temperature_limits[index],
			  humidity);
	mutex_unlock(&data->data_lock);

	return ret;
}

static void sht3x_select_command(struct sht3x_data *data)
{
	/*
	 * In blocking mode (clock stretching mode) the I2C bus
	 * is blocked for other traffic, thus the call to i2c_master_recv()
	 * will wait until the data is ready. For non blocking mode, we
	 * have to wait ourselves.
	 */
	if (data->mode > 0) {
		data->command = sht3x_cmd_measure_periodic_mode;
		data->wait_time = 0;
	} else if (data->setup.blocking_io) {
		data->command = data->setup.high_precision ?
				sht3x_cmd_measure_blocking_hpm :
				sht3x_cmd_measure_blocking_lpm;
		data->wait_time = 0;
	} else {
		if (data->setup.high_precision) {
			data->command = sht3x_cmd_measure_nonblocking_hpm;
			data->wait_time = SHT3X_NONBLOCKING_WAIT_TIME_HPM;
		} else {
			data->command = sht3x_cmd_measure_nonblocking_lpm;
			data->wait_time = SHT3X_NONBLOCKING_WAIT_TIME_LPM;
		}
	}
}

static int status_register_read(struct device *dev,
				struct device_attribute *attr,
				char *buffer, int length)
{
	int ret;
	struct sht3x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	ret = sht3x_read_from_command(client, data, sht3x_cmd_read_status_reg,
				      buffer, length, 0);

	return ret;
}

static ssize_t temp1_alarm_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	char buffer[SHT3X_WORD_LEN + SHT3X_CRC8_LEN];
	int ret;

	ret = status_register_read(dev, attr, buffer,
				   SHT3X_WORD_LEN + SHT3X_CRC8_LEN);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%d\n", !!(buffer[0] & 0x04));
}

static ssize_t humidity1_alarm_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	char buffer[SHT3X_WORD_LEN + SHT3X_CRC8_LEN];
	int ret;

	ret = status_register_read(dev, attr, buffer,
				   SHT3X_WORD_LEN + SHT3X_CRC8_LEN);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%d\n", !!(buffer[0] & 0x08));
}

static ssize_t heater_enable_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	char buffer[SHT3X_WORD_LEN + SHT3X_CRC8_LEN];
	int ret;

	ret = status_register_read(dev, attr, buffer,
				   SHT3X_WORD_LEN + SHT3X_CRC8_LEN);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%d\n", !!(buffer[0] & 0x20));
}

static ssize_t heater_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	struct sht3x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	bool status;

	ret = kstrtobool(buf, &status);
	if (ret)
		return ret;

	mutex_lock(&data->i2c_lock);

	if (status)
		ret = i2c_master_send(client, (char *)&sht3x_cmd_heater_on,
				      SHT3X_CMD_LENGTH);
	else
		ret = i2c_master_send(client, (char *)&sht3x_cmd_heater_off,
				      SHT3X_CMD_LENGTH);

	mutex_unlock(&data->i2c_lock);

	return ret;
}

static ssize_t update_interval_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct sht3x_data *data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 mode_to_update_interval[data->mode]);
}

static ssize_t update_interval_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	u16 update_interval;
	u8 mode;
	int ret;
	const char *command;
	struct sht3x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	ret = kstrtou16(buf, 0, &update_interval);
	if (ret)
		return ret;

	mode = get_mode_from_update_interval(update_interval);

	mutex_lock(&data->data_lock);
	/* mode did not change */
	if (mode == data->mode) {
		mutex_unlock(&data->data_lock);
		return count;
	}

	mutex_lock(&data->i2c_lock);
	/*
	 * Abort periodic measure mode.
	 * To do any changes to the configuration while in periodic mode, we
	 * have to send a break command to the sensor, which then falls back
	 * to single shot (mode = 0).
	 */
	if (data->mode > 0) {
		ret = i2c_master_send(client, sht3x_cmd_break,
				      SHT3X_CMD_LENGTH);
		if (ret != SHT3X_CMD_LENGTH)
			goto out;
		data->mode = 0;
	}

	if (mode > 0) {
		if (data->setup.high_precision)
			command = periodic_measure_commands_hpm[mode - 1];
		else
			command = periodic_measure_commands_lpm[mode - 1];

		/* select mode */
		ret = i2c_master_send(client, command, SHT3X_CMD_LENGTH);
		if (ret != SHT3X_CMD_LENGTH)
			goto out;
	}

	/* select mode and command */
	data->mode = mode;
	sht3x_select_command(data);

out:
	mutex_unlock(&data->i2c_lock);
	mutex_unlock(&data->data_lock);
	if (ret != SHT3X_CMD_LENGTH)
		return ret < 0 ? ret : -EIO;

	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp1_input, 0);
static SENSOR_DEVICE_ATTR_RO(humidity1_input, humidity1_input, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp1_limit, limit_max);
static SENSOR_DEVICE_ATTR_RW(humidity1_max, humidity1_limit, limit_max);
static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, temp1_limit, limit_max_hyst);
static SENSOR_DEVICE_ATTR_RW(humidity1_max_hyst, humidity1_limit,
			     limit_max_hyst);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp1_limit, limit_min);
static SENSOR_DEVICE_ATTR_RW(humidity1_min, humidity1_limit, limit_min);
static SENSOR_DEVICE_ATTR_RW(temp1_min_hyst, temp1_limit, limit_min_hyst);
static SENSOR_DEVICE_ATTR_RW(humidity1_min_hyst, humidity1_limit,
			     limit_min_hyst);
static SENSOR_DEVICE_ATTR_RO(temp1_alarm, temp1_alarm, 0);
static SENSOR_DEVICE_ATTR_RO(humidity1_alarm, humidity1_alarm, 0);
static SENSOR_DEVICE_ATTR_RW(heater_enable, heater_enable, 0);
static SENSOR_DEVICE_ATTR_RW(update_interval, update_interval, 0);

static struct attribute *sht3x_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_humidity1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_humidity1_max.dev_attr.attr,
	&sensor_dev_attr_humidity1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_min_hyst.dev_attr.attr,
	&sensor_dev_attr_humidity1_min.dev_attr.attr,
	&sensor_dev_attr_humidity1_min_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_humidity1_alarm.dev_attr.attr,
	&sensor_dev_attr_heater_enable.dev_attr.attr,
	&sensor_dev_attr_update_interval.dev_attr.attr,
	NULL
};

static struct attribute *sts3x_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(sht3x);
ATTRIBUTE_GROUPS(sts3x);

static int sht3x_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	int ret;
	struct sht3x_data *data;
	struct device *hwmon_dev;
	struct i2c_adapter *adap = client->adapter;
	struct device *dev = &client->dev;
	const struct attribute_group **attribute_groups;

	/*
	 * we require full i2c support since the sht3x uses multi-byte read and
	 * writes as well as multi-byte commands which are not supported by
	 * the smbus protocol
	 */
	if (!i2c_check_functionality(adap, I2C_FUNC_I2C))
		return -ENODEV;

	ret = i2c_master_send(client, sht3x_cmd_clear_status_reg,
			      SHT3X_CMD_LENGTH);
	if (ret != SHT3X_CMD_LENGTH)
		return ret < 0 ? ret : -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->setup.blocking_io = false;
	data->setup.high_precision = true;
	data->mode = 0;
	data->last_update = jiffies - msecs_to_jiffies(3000);
	data->client = client;
	crc8_populate_msb(sht3x_crc8_table, SHT3X_CRC8_POLYNOMIAL);

	if (client->dev.platform_data)
		data->setup = *(struct sht3x_platform_data *)dev->platform_data;

	sht3x_select_command(data);

	mutex_init(&data->i2c_lock);
	mutex_init(&data->data_lock);

	/*
	 * An attempt to read limits register too early
	 * causes a NACK response from the chip.
	 * Waiting for an empirical delay of 500 us solves the issue.
	 */
	usleep_range(500, 600);

	ret = limits_update(data);
	if (ret)
		return ret;

	if (id->driver_data == sts3x)
		attribute_groups = sts3x_groups;
	else
		attribute_groups = sht3x_groups;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
							   client->name,
							   data,
							   attribute_groups);

	if (IS_ERR(hwmon_dev))
		dev_dbg(dev, "unable to register hwmon device\n");

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/* device ID table */
static const struct i2c_device_id sht3x_ids[] = {
	{"sht3x", sht3x},
	{"sts3x", sts3x},
	{}
};

MODULE_DEVICE_TABLE(i2c, sht3x_ids);

static struct i2c_driver sht3x_i2c_driver = {
	.driver.name = "sht3x",
	.probe       = sht3x_probe,
	.id_table    = sht3x_ids,
};

module_i2c_driver(sht3x_i2c_driver);

MODULE_AUTHOR("David Frey <david.frey@sensirion.com>");
MODULE_AUTHOR("Pascal Sachs <pascal.sachs@sensirion.com>");
MODULE_DESCRIPTION("Sensirion SHT3x humidity and temperature sensor driver");
MODULE_LICENSE("GPL");

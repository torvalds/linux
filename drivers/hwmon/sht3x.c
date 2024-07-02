// SPDX-License-Identifier: GPL-2.0-or-later
/* Sensirion SHT3x-DIS humidity and temperature sensor driver.
 * The SHT3x comes in many different versions, this driver is for the
 * I2C version only.
 *
 * Copyright (C) 2016 Sensirion AG, Switzerland
 * Author: David Frey <david.frey@sensirion.com>
 * Author: Pascal Sachs <pascal.sachs@sensirion.com>
 */

#include <asm/page.h>
#include <linux/crc8.h>
#include <linux/debugfs.h>
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

/* commands (high repeatability mode) */
static const unsigned char sht3x_cmd_measure_single_hpm[] = { 0x24, 0x00 };

/* commands (medium repeatability mode) */
static const unsigned char sht3x_cmd_measure_single_mpm[] = { 0x24, 0x0b };

/* commands (low repeatability mode) */
static const unsigned char sht3x_cmd_measure_single_lpm[] = { 0x24, 0x16 };

/* commands for periodic mode */
static const unsigned char sht3x_cmd_measure_periodic_mode[]   = { 0xe0, 0x00 };
static const unsigned char sht3x_cmd_break[]                   = { 0x30, 0x93 };

/* commands for heater control */
static const unsigned char sht3x_cmd_heater_on[]               = { 0x30, 0x6d };
static const unsigned char sht3x_cmd_heater_off[]              = { 0x30, 0x66 };

/* other commands */
static const unsigned char sht3x_cmd_read_status_reg[]         = { 0xf3, 0x2d };
static const unsigned char sht3x_cmd_clear_status_reg[]        = { 0x30, 0x41 };
static const unsigned char sht3x_cmd_read_serial_number[]      = { 0x37, 0x80 };

static struct dentry *debugfs;

/* delays for single-shot mode i2c commands, both in us */
#define SHT3X_SINGLE_WAIT_TIME_HPM  15000
#define SHT3X_SINGLE_WAIT_TIME_MPM   6000
#define SHT3X_SINGLE_WAIT_TIME_LPM   4000

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

enum sht3x_repeatability {
	low_repeatability,
	medium_repeatability,
	high_repeatability,
};

DECLARE_CRC8_TABLE(sht3x_crc8_table);

/* periodic measure commands (high repeatability mode) */
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

/* periodic measure commands (medium repeatability) */
static const char periodic_measure_commands_mpm[][SHT3X_CMD_LENGTH] = {
	/* 0.5 measurements per second */
	{0x20, 0x24},
	/* 1 measurements per second */
	{0x21, 0x26},
	/* 2 measurements per second */
	{0x22, 0x20},
	/* 4 measurements per second */
	{0x23, 0x22},
	/* 10 measurements per second */
	{0x27, 0x21},
};

/* periodic measure commands (low repeatability mode) */
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

static const struct hwmon_channel_info * const sht3x_channel_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MIN |
				HWMON_T_MIN_HYST | HWMON_T_MAX |
				HWMON_T_MAX_HYST | HWMON_T_ALARM),
	HWMON_CHANNEL_INFO(humidity, HWMON_H_INPUT | HWMON_H_MIN |
				HWMON_H_MIN_HYST | HWMON_H_MAX |
				HWMON_H_MAX_HYST | HWMON_H_ALARM),
	NULL,
};

struct sht3x_data {
	struct i2c_client *client;
	enum sht3x_chips chip_id;
	struct mutex i2c_lock; /* lock for sending i2c commands */
	struct mutex data_lock; /* lock for updating driver data */
	struct dentry *sensor_dir;

	u8 mode;
	const unsigned char *command;
	u32 wait_time;			/* in us*/
	unsigned long last_update;	/* last update in periodic mode*/
	enum sht3x_repeatability repeatability;
	u32 serial_number;

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

static int temp1_input_read(struct device *dev)
{
	struct sht3x_data *data = sht3x_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return data->temperature;
}

static int humidity1_input_read(struct device *dev)
{
	struct sht3x_data *data = sht3x_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return data->humidity;
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

static int temp1_limit_read(struct device *dev, int index)
{
	struct sht3x_data *data = dev_get_drvdata(dev);

	return data->temperature_limits[index];
}

static int humidity1_limit_read(struct device *dev, int index)
{
	struct sht3x_data *data = dev_get_drvdata(dev);

	return data->humidity_limits[index];
}

/*
 * limit_write must only be called with data_lock held
 */
static size_t limit_write(struct device *dev,
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
	 * in limit_read()
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

	return 0;
}

static int temp1_limit_write(struct device *dev, int index, int val)
{
	int temperature;
	int ret;
	struct sht3x_data *data = dev_get_drvdata(dev);

	temperature = clamp_val(val, SHT3X_MIN_TEMPERATURE,
				SHT3X_MAX_TEMPERATURE);
	mutex_lock(&data->data_lock);
	ret = limit_write(dev, index, temperature,
			  data->humidity_limits[index]);
	mutex_unlock(&data->data_lock);

	return ret;
}

static int humidity1_limit_write(struct device *dev, int index, int val)
{
	u32 humidity;
	int ret;
	struct sht3x_data *data = dev_get_drvdata(dev);

	humidity = clamp_val(val, SHT3X_MIN_HUMIDITY, SHT3X_MAX_HUMIDITY);
	mutex_lock(&data->data_lock);
	ret = limit_write(dev, index, data->temperature_limits[index],
			  humidity);
	mutex_unlock(&data->data_lock);

	return ret;
}

static void sht3x_select_command(struct sht3x_data *data)
{
	/*
	 * For single-shot mode, only non blocking mode is support,
	 * we have to wait ourselves for result.
	 */
	if (data->mode > 0) {
		data->command = sht3x_cmd_measure_periodic_mode;
		data->wait_time = 0;
	} else {
		if (data->repeatability == high_repeatability) {
			data->command = sht3x_cmd_measure_single_hpm;
			data->wait_time = SHT3X_SINGLE_WAIT_TIME_HPM;
		} else if (data->repeatability ==  medium_repeatability) {
			data->command = sht3x_cmd_measure_single_mpm;
			data->wait_time = SHT3X_SINGLE_WAIT_TIME_MPM;
		} else {
			data->command = sht3x_cmd_measure_single_lpm;
			data->wait_time = SHT3X_SINGLE_WAIT_TIME_LPM;
		}
	}
}

static int status_register_read(struct device *dev,
				char *buffer, int length)
{
	int ret;
	struct sht3x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	ret = sht3x_read_from_command(client, data, sht3x_cmd_read_status_reg,
				      buffer, length, 0);

	return ret;
}

static int temp1_alarm_read(struct device *dev)
{
	char buffer[SHT3X_WORD_LEN + SHT3X_CRC8_LEN];
	int ret;

	ret = status_register_read(dev, buffer,
				   SHT3X_WORD_LEN + SHT3X_CRC8_LEN);
	if (ret)
		return ret;

	return !!(buffer[0] & 0x04);
}

static int humidity1_alarm_read(struct device *dev)
{
	char buffer[SHT3X_WORD_LEN + SHT3X_CRC8_LEN];
	int ret;

	ret = status_register_read(dev, buffer,
				   SHT3X_WORD_LEN + SHT3X_CRC8_LEN);
	if (ret)
		return ret;

	return !!(buffer[0] & 0x08);
}

static ssize_t heater_enable_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	char buffer[SHT3X_WORD_LEN + SHT3X_CRC8_LEN];
	int ret;

	ret = status_register_read(dev, buffer,
				   SHT3X_WORD_LEN + SHT3X_CRC8_LEN);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", !!(buffer[0] & 0x20));
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

static int update_interval_read(struct device *dev)
{
	struct sht3x_data *data = dev_get_drvdata(dev);

	return mode_to_update_interval[data->mode];
}

static int update_interval_write(struct device *dev, int val)
{
	u8 mode;
	int ret;
	const char *command;
	struct sht3x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mode = get_mode_from_update_interval(val);

	mutex_lock(&data->data_lock);
	/* mode did not change */
	if (mode == data->mode) {
		mutex_unlock(&data->data_lock);
		return 0;
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
		if (data->repeatability == high_repeatability)
			command = periodic_measure_commands_hpm[mode - 1];
		else if (data->repeatability == medium_repeatability)
			command = periodic_measure_commands_mpm[mode - 1];
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

	return 0;
}

static ssize_t repeatability_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct sht3x_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", data->repeatability);
}

static ssize_t repeatability_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	int ret;
	u8 val;

	struct sht3x_data *data = dev_get_drvdata(dev);

	ret = kstrtou8(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 2)
		return -EINVAL;

	data->repeatability = val;

	return count;
}

static SENSOR_DEVICE_ATTR_RW(heater_enable, heater_enable, 0);
static SENSOR_DEVICE_ATTR_RW(repeatability, repeatability, 0);

static struct attribute *sht3x_attrs[] = {
	&sensor_dev_attr_heater_enable.dev_attr.attr,
	&sensor_dev_attr_repeatability.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(sht3x);

static umode_t sht3x_is_visible(const void *data, enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	const struct sht3x_data *chip_data = data;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_alarm:
			return 0444;
		case hwmon_temp_max:
		case hwmon_temp_max_hyst:
		case hwmon_temp_min:
		case hwmon_temp_min_hyst:
			return 0644;
		default:
			break;
		}
		break;
	case hwmon_humidity:
		if (chip_data->chip_id == sts3x)
			break;
		switch (attr) {
		case hwmon_humidity_input:
		case hwmon_humidity_alarm:
			return 0444;
		case hwmon_humidity_max:
		case hwmon_humidity_max_hyst:
		case hwmon_humidity_min:
		case hwmon_humidity_min_hyst:
			return 0644;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int sht3x_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	enum sht3x_limits index;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			*val = update_interval_read(dev);
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			*val = temp1_input_read(dev);
			break;
		case hwmon_temp_alarm:
			*val = temp1_alarm_read(dev);
			break;
		case hwmon_temp_max:
			index = limit_max;
			*val = temp1_limit_read(dev, index);
			break;
		case hwmon_temp_max_hyst:
			index = limit_max_hyst;
			*val = temp1_limit_read(dev, index);
			break;
		case hwmon_temp_min:
			index = limit_min;
			*val = temp1_limit_read(dev, index);
			break;
		case hwmon_temp_min_hyst:
			index = limit_min_hyst;
			*val = temp1_limit_read(dev, index);
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_humidity:
		switch (attr) {
		case hwmon_humidity_input:
			*val = humidity1_input_read(dev);
			break;
		case hwmon_humidity_alarm:
			*val = humidity1_alarm_read(dev);
			break;
		case hwmon_humidity_max:
			index = limit_max;
			*val = humidity1_limit_read(dev, index);
			break;
		case hwmon_humidity_max_hyst:
			index = limit_max_hyst;
			*val = humidity1_limit_read(dev, index);
			break;
		case hwmon_humidity_min:
			index = limit_min;
			*val = humidity1_limit_read(dev, index);
			break;
		case hwmon_humidity_min_hyst:
			index = limit_min_hyst;
			*val = humidity1_limit_read(dev, index);
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int sht3x_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	enum sht3x_limits index;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return update_interval_write(dev, val);
		default:
			return -EOPNOTSUPP;
		}
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_max:
			index = limit_max;
			break;
		case hwmon_temp_max_hyst:
			index = limit_max_hyst;
			break;
		case hwmon_temp_min:
			index = limit_min;
			break;
		case hwmon_temp_min_hyst:
			index = limit_min_hyst;
			break;
		default:
			return -EOPNOTSUPP;
		}
		return temp1_limit_write(dev, index, val);
	case hwmon_humidity:
		switch (attr) {
		case hwmon_humidity_max:
			index = limit_max;
			break;
		case hwmon_humidity_max_hyst:
			index = limit_max_hyst;
			break;
		case hwmon_humidity_min:
			index = limit_min;
			break;
		case hwmon_humidity_min_hyst:
			index = limit_min_hyst;
			break;
		default:
			return -EOPNOTSUPP;
		}
		return humidity1_limit_write(dev, index, val);
	default:
		return -EOPNOTSUPP;
	}
}

static void sht3x_debugfs_init(struct sht3x_data *data)
{
	char name[32];

	snprintf(name, sizeof(name), "i2c%u-%02x",
		 data->client->adapter->nr, data->client->addr);
	data->sensor_dir = debugfs_create_dir(name, debugfs);
	debugfs_create_u32("serial_number", 0444,
			   data->sensor_dir, &data->serial_number);
}

static void sht3x_debugfs_remove(void *sensor_dir)
{
	debugfs_remove_recursive(sensor_dir);
}

static int sht3x_serial_number_read(struct sht3x_data *data)
{
	int ret;
	char buffer[SHT3X_RESPONSE_LENGTH];
	struct i2c_client *client = data->client;

	ret = sht3x_read_from_command(client, data,
				      sht3x_cmd_read_serial_number,
				      buffer,
				      SHT3X_RESPONSE_LENGTH, 0);
	if (ret)
		return ret;

	data->serial_number = (buffer[0] << 24) | (buffer[1] << 16) |
			      (buffer[3] << 8) | buffer[4];
	return ret;
}

static const struct hwmon_ops sht3x_ops = {
	.is_visible = sht3x_is_visible,
	.read = sht3x_read,
	.write = sht3x_write,
};

static const struct hwmon_chip_info sht3x_chip_info = {
	.ops = &sht3x_ops,
	.info = sht3x_channel_info,
};

/* device ID table */
static const struct i2c_device_id sht3x_ids[] = {
	{"sht3x", sht3x},
	{"sts3x", sts3x},
	{}
};

MODULE_DEVICE_TABLE(i2c, sht3x_ids);

static int sht3x_probe(struct i2c_client *client)
{
	int ret;
	struct sht3x_data *data;
	struct device *hwmon_dev;
	struct i2c_adapter *adap = client->adapter;
	struct device *dev = &client->dev;

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

	data->repeatability = high_repeatability;
	data->mode = 0;
	data->last_update = jiffies - msecs_to_jiffies(3000);
	data->client = client;
	data->chip_id = i2c_match_id(sht3x_ids, client)->driver_data;
	crc8_populate_msb(sht3x_crc8_table, SHT3X_CRC8_POLYNOMIAL);

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

	ret = sht3x_serial_number_read(data);
	if (ret) {
		dev_dbg(dev, "unable to read serial number\n");
	} else {
		sht3x_debugfs_init(data);
		ret = devm_add_action_or_reset(dev,
					       sht3x_debugfs_remove,
					       data->sensor_dir);
		if (ret)
			return ret;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 client->name,
							 data,
							 &sht3x_chip_info,
							 sht3x_groups);

	if (IS_ERR(hwmon_dev))
		dev_dbg(dev, "unable to register hwmon device\n");

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct i2c_driver sht3x_i2c_driver = {
	.driver.name = "sht3x",
	.probe       = sht3x_probe,
	.id_table    = sht3x_ids,
};

static int __init sht3x_init(void)
{
	debugfs = debugfs_create_dir("sht3x", NULL);
	return i2c_add_driver(&sht3x_i2c_driver);
}
module_init(sht3x_init);

static void __exit sht3x_cleanup(void)
{
	debugfs_remove_recursive(debugfs);
	i2c_del_driver(&sht3x_i2c_driver);
}
module_exit(sht3x_cleanup);

MODULE_AUTHOR("David Frey <david.frey@sensirion.com>");
MODULE_AUTHOR("Pascal Sachs <pascal.sachs@sensirion.com>");
MODULE_DESCRIPTION("Sensirion SHT3x humidity and temperature sensor driver");
MODULE_LICENSE("GPL");

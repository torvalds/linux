// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for the FTS Systemmonitoring Chip "Teutates"
 *
 * Copyright (C) 2016 Fujitsu Technology Solutions GmbH,
 *		  Thilo Cestonaro <thilo.cestonaro@ts.fujitsu.com>
 */
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

#define FTS_DEVICE_ID_REG		0x0000
#define FTS_DEVICE_REVISION_REG		0x0001
#define FTS_DEVICE_STATUS_REG		0x0004
#define FTS_SATELLITE_STATUS_REG	0x0005
#define FTS_EVENT_STATUS_REG		0x0006
#define FTS_GLOBAL_CONTROL_REG		0x0007

#define FTS_DEVICE_DETECT_REG_1		0x0C
#define FTS_DEVICE_DETECT_REG_2		0x0D
#define FTS_DEVICE_DETECT_REG_3		0x0E

#define FTS_SENSOR_EVENT_REG		0x0010

#define FTS_FAN_EVENT_REG		0x0014
#define FTS_FAN_PRESENT_REG		0x0015

#define FTS_POWER_ON_TIME_COUNTER_A	0x007A
#define FTS_POWER_ON_TIME_COUNTER_B	0x007B
#define FTS_POWER_ON_TIME_COUNTER_C	0x007C

#define FTS_PAGE_SELECT_REG		0x007F

#define FTS_WATCHDOG_TIME_PRESET	0x000B
#define FTS_WATCHDOG_CONTROL		0x5081

#define FTS_NO_FAN_SENSORS		0x08
#define FTS_NO_TEMP_SENSORS		0x10
#define FTS_NO_VOLT_SENSORS		0x04

#define FTS_FAN_SOURCE_INVALID		0xff

static const unsigned short normal_i2c[] = { 0x73, I2C_CLIENT_END };

static const struct i2c_device_id fts_id[] = {
	{ "ftsteutates" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fts_id);

enum WATCHDOG_RESOLUTION {
	seconds = 1,
	minutes = 60
};

struct fts_data {
	struct i2c_client *client;
	/* update sensor data lock */
	struct mutex update_lock;
	/* read/write register lock */
	struct mutex access_lock;
	unsigned long last_updated; /* in jiffies */
	struct watchdog_device wdd;
	enum WATCHDOG_RESOLUTION resolution;
	bool valid; /* false until following fields are valid */

	u8 volt[FTS_NO_VOLT_SENSORS];

	u8 temp_input[FTS_NO_TEMP_SENSORS];
	u8 temp_alarm;

	u8 fan_present;
	u8 fan_input[FTS_NO_FAN_SENSORS]; /* in rps */
	u8 fan_source[FTS_NO_FAN_SENSORS];
	u8 fan_alarm;
};

#define FTS_REG_FAN_INPUT(idx) ((idx) + 0x20)
#define FTS_REG_FAN_SOURCE(idx) ((idx) + 0x30)
#define FTS_REG_FAN_CONTROL(idx) (((idx) << 16) + 0x4881)

#define FTS_REG_TEMP_INPUT(idx) ((idx) + 0x40)
#define FTS_REG_TEMP_CONTROL(idx) (((idx) << 16) + 0x0681)

#define FTS_REG_VOLT(idx) ((idx) + 0x18)

/*****************************************************************************/
/* I2C Helper functions							     */
/*****************************************************************************/
static int fts_read_byte(struct i2c_client *client, unsigned short reg)
{
	int ret;
	unsigned char page = reg >> 8;
	struct fts_data *data = dev_get_drvdata(&client->dev);

	mutex_lock(&data->access_lock);

	dev_dbg(&client->dev, "page select - page: 0x%.02x\n", page);
	ret = i2c_smbus_write_byte_data(client, FTS_PAGE_SELECT_REG, page);
	if (ret < 0)
		goto error;

	reg &= 0xFF;
	ret = i2c_smbus_read_byte_data(client, reg);
	dev_dbg(&client->dev, "read - reg: 0x%.02x: val: 0x%.02x\n", reg, ret);

error:
	mutex_unlock(&data->access_lock);
	return ret;
}

static int fts_write_byte(struct i2c_client *client, unsigned short reg,
			  unsigned char value)
{
	int ret;
	unsigned char page = reg >> 8;
	struct fts_data *data = dev_get_drvdata(&client->dev);

	mutex_lock(&data->access_lock);

	dev_dbg(&client->dev, "page select - page: 0x%.02x\n", page);
	ret = i2c_smbus_write_byte_data(client, FTS_PAGE_SELECT_REG, page);
	if (ret < 0)
		goto error;

	reg &= 0xFF;
	dev_dbg(&client->dev,
		"write - reg: 0x%.02x: val: 0x%.02x\n", reg, value);
	ret = i2c_smbus_write_byte_data(client, reg, value);

error:
	mutex_unlock(&data->access_lock);
	return ret;
}

/*****************************************************************************/
/* Data Updater Helper function						     */
/*****************************************************************************/
static int fts_update_device(struct fts_data *data)
{
	int i;
	int err = 0;

	mutex_lock(&data->update_lock);
	if (!time_after(jiffies, data->last_updated + 2 * HZ) && data->valid)
		goto exit;

	err = fts_read_byte(data->client, FTS_DEVICE_STATUS_REG);
	if (err < 0)
		goto exit;

	data->valid = !!(err & 0x02); /* Data not ready yet */
	if (unlikely(!data->valid)) {
		err = -EAGAIN;
		goto exit;
	}

	err = fts_read_byte(data->client, FTS_FAN_PRESENT_REG);
	if (err < 0)
		goto exit;
	data->fan_present = err;

	err = fts_read_byte(data->client, FTS_FAN_EVENT_REG);
	if (err < 0)
		goto exit;
	data->fan_alarm = err;

	for (i = 0; i < FTS_NO_FAN_SENSORS; i++) {
		if (data->fan_present & BIT(i)) {
			err = fts_read_byte(data->client, FTS_REG_FAN_INPUT(i));
			if (err < 0)
				goto exit;
			data->fan_input[i] = err;

			err = fts_read_byte(data->client,
					    FTS_REG_FAN_SOURCE(i));
			if (err < 0)
				goto exit;
			data->fan_source[i] = err;
		} else {
			data->fan_input[i] = 0;
			data->fan_source[i] = FTS_FAN_SOURCE_INVALID;
		}
	}

	err = fts_read_byte(data->client, FTS_SENSOR_EVENT_REG);
	if (err < 0)
		goto exit;
	data->temp_alarm = err;

	for (i = 0; i < FTS_NO_TEMP_SENSORS; i++) {
		err = fts_read_byte(data->client, FTS_REG_TEMP_INPUT(i));
		if (err < 0)
			goto exit;
		data->temp_input[i] = err;
	}

	for (i = 0; i < FTS_NO_VOLT_SENSORS; i++) {
		err = fts_read_byte(data->client, FTS_REG_VOLT(i));
		if (err < 0)
			goto exit;
		data->volt[i] = err;
	}
	data->last_updated = jiffies;
	err = 0;
exit:
	mutex_unlock(&data->update_lock);
	return err;
}

/*****************************************************************************/
/* Watchdog functions							     */
/*****************************************************************************/
static int fts_wd_set_resolution(struct fts_data *data,
				 enum WATCHDOG_RESOLUTION resolution)
{
	int ret;

	if (data->resolution == resolution)
		return 0;

	ret = fts_read_byte(data->client, FTS_WATCHDOG_CONTROL);
	if (ret < 0)
		return ret;

	if ((resolution == seconds && ret & BIT(1)) ||
	    (resolution == minutes && (ret & BIT(1)) == 0)) {
		data->resolution = resolution;
		return 0;
	}

	if (resolution == seconds)
		ret |= BIT(1);
	else
		ret &= ~BIT(1);

	ret = fts_write_byte(data->client, FTS_WATCHDOG_CONTROL, ret);
	if (ret < 0)
		return ret;

	data->resolution = resolution;
	return ret;
}

static int fts_wd_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	struct fts_data *data;
	enum WATCHDOG_RESOLUTION resolution = seconds;
	int ret;

	data = watchdog_get_drvdata(wdd);
	/* switch watchdog resolution to minutes if timeout does not fit
	 * into a byte
	 */
	if (timeout > 0xFF) {
		timeout = DIV_ROUND_UP(timeout, 60) * 60;
		resolution = minutes;
	}

	ret = fts_wd_set_resolution(data, resolution);
	if (ret < 0)
		return ret;

	wdd->timeout = timeout;
	return 0;
}

static int fts_wd_start(struct watchdog_device *wdd)
{
	struct fts_data *data = watchdog_get_drvdata(wdd);

	return fts_write_byte(data->client, FTS_WATCHDOG_TIME_PRESET,
			      wdd->timeout / (u8)data->resolution);
}

static int fts_wd_stop(struct watchdog_device *wdd)
{
	struct fts_data *data;

	data = watchdog_get_drvdata(wdd);
	return fts_write_byte(data->client, FTS_WATCHDOG_TIME_PRESET, 0);
}

static const struct watchdog_info fts_wd_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "FTS Teutates Hardware Watchdog",
};

static const struct watchdog_ops fts_wd_ops = {
	.owner = THIS_MODULE,
	.start = fts_wd_start,
	.stop = fts_wd_stop,
	.set_timeout = fts_wd_set_timeout,
};

static int fts_watchdog_init(struct fts_data *data)
{
	int timeout, ret;

	watchdog_set_drvdata(&data->wdd, data);

	timeout = fts_read_byte(data->client, FTS_WATCHDOG_TIME_PRESET);
	if (timeout < 0)
		return timeout;

	/* watchdog not running, set timeout to a default of 60 sec. */
	if (timeout == 0) {
		ret = fts_wd_set_resolution(data, seconds);
		if (ret < 0)
			return ret;
		data->wdd.timeout = 60;
	} else {
		ret = fts_read_byte(data->client, FTS_WATCHDOG_CONTROL);
		if (ret < 0)
			return ret;

		data->resolution = ret & BIT(1) ? seconds : minutes;
		data->wdd.timeout = timeout * (u8)data->resolution;
		set_bit(WDOG_HW_RUNNING, &data->wdd.status);
	}

	/* Register our watchdog part */
	data->wdd.info = &fts_wd_info;
	data->wdd.ops = &fts_wd_ops;
	data->wdd.parent = &data->client->dev;
	data->wdd.min_timeout = 1;

	/* max timeout 255 minutes. */
	data->wdd.max_hw_heartbeat_ms = 0xFF * 60 * MSEC_PER_SEC;

	return devm_watchdog_register_device(&data->client->dev, &data->wdd);
}

static umode_t fts_is_visible(const void *devdata, enum hwmon_sensor_types type, u32 attr,
			      int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_fault:
			return 0444;
		case hwmon_temp_alarm:
			return 0644;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
		case hwmon_fan_fault:
			return 0444;
		case hwmon_fan_alarm:
			return 0644;
		default:
			break;
		}
		break;
	case hwmon_pwm:
	case hwmon_in:
		return 0444;
	default:
		break;
	}

	return 0;
}

static int fts_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
		    long *val)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int ret = fts_update_device(data);

	if (ret < 0)
		return ret;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			*val = (data->temp_input[channel] - 64) * 1000;

			return 0;
		case hwmon_temp_alarm:
			*val = !!(data->temp_alarm & BIT(channel));

			return 0;
		case hwmon_temp_fault:
			/* 00h Temperature = Sensor Error */;
			*val = (data->temp_input[channel] == 0);

			return 0;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			*val = data->fan_input[channel] * 60;

			return 0;
		case hwmon_fan_alarm:
			*val = !!(data->fan_alarm & BIT(channel));

			return 0;
		case hwmon_fan_fault:
			*val = !(data->fan_present & BIT(channel));

			return 0;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_auto_channels_temp: {
			u8 fan_source = data->fan_source[channel];

			if (fan_source == FTS_FAN_SOURCE_INVALID || fan_source >= BITS_PER_LONG)
				*val = 0;
			else
				*val = BIT(fan_source);

			return 0;
		}
		default:
			break;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			*val = DIV_ROUND_CLOSEST(data->volt[channel] * 3300, 255);

			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int fts_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
		     long val)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int ret = fts_update_device(data);

	if (ret < 0)
		return ret;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_alarm:
			if (val)
				return -EINVAL;

			mutex_lock(&data->update_lock);
			ret = fts_read_byte(data->client, FTS_REG_TEMP_CONTROL(channel));
			if (ret >= 0)
				ret = fts_write_byte(data->client, FTS_REG_TEMP_CONTROL(channel),
						     ret | 0x1);
			if (ret >= 0)
				data->valid = false;

			mutex_unlock(&data->update_lock);
			if (ret < 0)
				return ret;

			return 0;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_alarm:
			if (val)
				return -EINVAL;

			mutex_lock(&data->update_lock);
			ret = fts_read_byte(data->client, FTS_REG_FAN_CONTROL(channel));
			if (ret >= 0)
				ret = fts_write_byte(data->client, FTS_REG_FAN_CONTROL(channel),
						     ret | 0x1);
			if (ret >= 0)
				data->valid = false;

			mutex_unlock(&data->update_lock);
			if (ret < 0)
				return ret;

			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops fts_ops = {
	.is_visible = fts_is_visible,
	.read = fts_read,
	.write = fts_write,
};

static const struct hwmon_channel_info * const fts_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_FAULT
			   ),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_ALARM | HWMON_F_FAULT
			   ),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP
			   ),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT,
			   HWMON_I_INPUT,
			   HWMON_I_INPUT,
			   HWMON_I_INPUT
			   ),
	NULL
};

static const struct hwmon_chip_info fts_chip_info = {
	.ops = &fts_ops,
	.info = fts_info,
};

/*****************************************************************************/
/* Module initialization / remove functions				     */
/*****************************************************************************/
static int fts_detect(struct i2c_client *client,
		      struct i2c_board_info *info)
{
	int val;

	/* detection works with revision greater or equal to 0x2b */
	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_REVISION_REG);
	if (val < 0x2b)
		return -ENODEV;

	/* Device Detect Regs must have 0x17 0x34 and 0x54 */
	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_DETECT_REG_1);
	if (val != 0x17)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_DETECT_REG_2);
	if (val != 0x34)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_DETECT_REG_3);
	if (val != 0x54)
		return -ENODEV;

	/*
	 * 0x10 == Baseboard Management Controller, 0x01 == Teutates
	 * Device ID Reg needs to be 0x11
	 */
	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_ID_REG);
	if (val != 0x11)
		return -ENODEV;

	strscpy(info->type, fts_id[0].name, I2C_NAME_SIZE);
	info->flags = 0;
	return 0;
}

static int fts_probe(struct i2c_client *client)
{
	u8 revision;
	struct fts_data *data;
	int err;
	s8 deviceid;
	struct device *hwmon_dev;

	if (client->addr != 0x73)
		return -ENODEV;

	/* Baseboard Management Controller check */
	deviceid = i2c_smbus_read_byte_data(client, FTS_DEVICE_ID_REG);
	if (deviceid > 0 && (deviceid & 0xF0) == 0x10) {
		switch (deviceid & 0x0F) {
		case 0x01:
			break;
		default:
			dev_dbg(&client->dev,
				"No Baseboard Management Controller\n");
			return -ENODEV;
		}
	} else {
		dev_dbg(&client->dev, "No fujitsu board\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct fts_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->update_lock);
	mutex_init(&data->access_lock);
	data->client = client;
	dev_set_drvdata(&client->dev, data);

	err = i2c_smbus_read_byte_data(client, FTS_DEVICE_REVISION_REG);
	if (err < 0)
		return err;
	revision = err;

	hwmon_dev = devm_hwmon_device_register_with_info(&client->dev, "ftsteutates", data,
							 &fts_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	err = fts_watchdog_init(data);
	if (err)
		return err;

	dev_info(&client->dev, "Detected FTS Teutates chip, revision: %d.%d\n",
		 (revision & 0xF0) >> 4, revision & 0x0F);
	return 0;
}

/*****************************************************************************/
/* Module Details							     */
/*****************************************************************************/
static struct i2c_driver fts_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "ftsteutates",
	},
	.id_table = fts_id,
	.probe = fts_probe,
	.detect = fts_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(fts_driver);

MODULE_AUTHOR("Thilo Cestonaro <thilo.cestonaro@ts.fujitsu.com>");
MODULE_DESCRIPTION("FTS Teutates driver");
MODULE_LICENSE("GPL");

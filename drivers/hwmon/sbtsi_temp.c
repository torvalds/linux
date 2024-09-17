// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sbtsi_temp.c - hwmon driver for a SBI Temperature Sensor Interface (SB-TSI)
 *                compliant AMD SoC temperature device.
 *
 * Copyright (c) 2020, Google Inc.
 * Copyright (c) 2020, Kun Yi <kunyi@google.com>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of.h>

/*
 * SB-TSI registers only support SMBus byte data access. "_INT" registers are
 * the integer part of a temperature value or limit, and "_DEC" registers are
 * corresponding decimal parts.
 */
#define SBTSI_REG_TEMP_INT		0x01 /* RO */
#define SBTSI_REG_STATUS		0x02 /* RO */
#define SBTSI_REG_CONFIG		0x03 /* RO */
#define SBTSI_REG_TEMP_HIGH_INT		0x07 /* RW */
#define SBTSI_REG_TEMP_LOW_INT		0x08 /* RW */
#define SBTSI_REG_TEMP_DEC		0x10 /* RW */
#define SBTSI_REG_TEMP_HIGH_DEC		0x13 /* RW */
#define SBTSI_REG_TEMP_LOW_DEC		0x14 /* RW */

#define SBTSI_CONFIG_READ_ORDER_SHIFT	5

#define SBTSI_TEMP_MIN	0
#define SBTSI_TEMP_MAX	255875

/* Each client has this additional data */
struct sbtsi_data {
	struct i2c_client *client;
	struct mutex lock;
};

/*
 * From SB-TSI spec: CPU temperature readings and limit registers encode the
 * temperature in increments of 0.125 from 0 to 255.875. The "high byte"
 * register encodes the base-2 of the integer portion, and the upper 3 bits of
 * the "low byte" encode in base-2 the decimal portion.
 *
 * e.g. INT=0x19, DEC=0x20 represents 25.125 degrees Celsius
 *
 * Therefore temperature in millidegree Celsius =
 *   (INT + DEC / 256) * 1000 = (INT * 8 + DEC / 32) * 125
 */
static inline int sbtsi_reg_to_mc(s32 integer, s32 decimal)
{
	return ((integer << 3) + (decimal >> 5)) * 125;
}

/*
 * Inversely, given temperature in millidegree Celsius
 *   INT = (TEMP / 125) / 8
 *   DEC = ((TEMP / 125) % 8) * 32
 * Caller have to make sure temp doesn't exceed 255875, the max valid value.
 */
static inline void sbtsi_mc_to_reg(s32 temp, u8 *integer, u8 *decimal)
{
	temp /= 125;
	*integer = temp >> 3;
	*decimal = (temp & 0x7) << 5;
}

static int sbtsi_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	struct sbtsi_data *data = dev_get_drvdata(dev);
	s32 temp_int, temp_dec;
	int err;

	switch (attr) {
	case hwmon_temp_input:
		/*
		 * ReadOrder bit specifies the reading order of integer and
		 * decimal part of CPU temp for atomic reads. If bit == 0,
		 * reading integer part triggers latching of the decimal part,
		 * so integer part should be read first. If bit == 1, read
		 * order should be reversed.
		 */
		err = i2c_smbus_read_byte_data(data->client, SBTSI_REG_CONFIG);
		if (err < 0)
			return err;

		mutex_lock(&data->lock);
		if (err & BIT(SBTSI_CONFIG_READ_ORDER_SHIFT)) {
			temp_dec = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_DEC);
			temp_int = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_INT);
		} else {
			temp_int = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_INT);
			temp_dec = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_DEC);
		}
		mutex_unlock(&data->lock);
		break;
	case hwmon_temp_max:
		mutex_lock(&data->lock);
		temp_int = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_HIGH_INT);
		temp_dec = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_HIGH_DEC);
		mutex_unlock(&data->lock);
		break;
	case hwmon_temp_min:
		mutex_lock(&data->lock);
		temp_int = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_LOW_INT);
		temp_dec = i2c_smbus_read_byte_data(data->client, SBTSI_REG_TEMP_LOW_DEC);
		mutex_unlock(&data->lock);
		break;
	default:
		return -EINVAL;
	}


	if (temp_int < 0)
		return temp_int;
	if (temp_dec < 0)
		return temp_dec;

	*val = sbtsi_reg_to_mc(temp_int, temp_dec);

	return 0;
}

static int sbtsi_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	struct sbtsi_data *data = dev_get_drvdata(dev);
	int reg_int, reg_dec, err;
	u8 temp_int, temp_dec;

	switch (attr) {
	case hwmon_temp_max:
		reg_int = SBTSI_REG_TEMP_HIGH_INT;
		reg_dec = SBTSI_REG_TEMP_HIGH_DEC;
		break;
	case hwmon_temp_min:
		reg_int = SBTSI_REG_TEMP_LOW_INT;
		reg_dec = SBTSI_REG_TEMP_LOW_DEC;
		break;
	default:
		return -EINVAL;
	}

	val = clamp_val(val, SBTSI_TEMP_MIN, SBTSI_TEMP_MAX);
	sbtsi_mc_to_reg(val, &temp_int, &temp_dec);

	mutex_lock(&data->lock);
	err = i2c_smbus_write_byte_data(data->client, reg_int, temp_int);
	if (err)
		goto exit;

	err = i2c_smbus_write_byte_data(data->client, reg_dec, temp_dec);
exit:
	mutex_unlock(&data->lock);
	return err;
}

static umode_t sbtsi_is_visible(const void *data,
				enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_min:
			return 0644;
		case hwmon_temp_max:
			return 0644;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info *sbtsi_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX),
	NULL
};

static const struct hwmon_ops sbtsi_hwmon_ops = {
	.is_visible = sbtsi_is_visible,
	.read = sbtsi_read,
	.write = sbtsi_write,
};

static const struct hwmon_chip_info sbtsi_chip_info = {
	.ops = &sbtsi_hwmon_ops,
	.info = sbtsi_info,
};

static int sbtsi_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct sbtsi_data *data;

	data = devm_kzalloc(dev, sizeof(struct sbtsi_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data, &sbtsi_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id sbtsi_id[] = {
	{"sbtsi", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sbtsi_id);

static const struct of_device_id __maybe_unused sbtsi_of_match[] = {
	{
		.compatible = "amd,sbtsi",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sbtsi_of_match);

static struct i2c_driver sbtsi_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "sbtsi",
		.of_match_table = of_match_ptr(sbtsi_of_match),
	},
	.probe = sbtsi_probe,
	.id_table = sbtsi_id,
};

module_i2c_driver(sbtsi_driver);

MODULE_AUTHOR("Kun Yi <kunyi@google.com>");
MODULE_DESCRIPTION("Hwmon driver for AMD SB-TSI emulated sensor");
MODULE_LICENSE("GPL");

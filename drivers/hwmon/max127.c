// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for MAX127.
 *
 * Copyright (c) 2020 Facebook Inc.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>

/*
 * MAX127 Control Byte. Refer to MAX127 datasheet, Table 1 "Control-Byte
 * Format" for details.
 */
#define MAX127_CTRL_START	BIT(7)
#define MAX127_CTRL_SEL_SHIFT	4
#define MAX127_CTRL_RNG		BIT(3)
#define MAX127_CTRL_BIP		BIT(2)
#define MAX127_CTRL_PD1		BIT(1)
#define MAX127_CTRL_PD0		BIT(0)

#define MAX127_NUM_CHANNELS	8
#define MAX127_SET_CHANNEL(ch)	(((ch) & 7) << MAX127_CTRL_SEL_SHIFT)

/*
 * MAX127 channel input ranges. Refer to MAX127 datasheet, Table 3 "Range
 * and Polarity Selection" for details.
 */
#define MAX127_FULL_RANGE	10000	/* 10V */
#define MAX127_HALF_RANGE	5000	/* 5V */

/*
 * MAX127 returns 2 bytes at read:
 *   - the first byte contains data[11:4].
 *   - the second byte contains data[3:0] (MSB) and 4 dummy 0s (LSB).
 * Refer to MAX127 datasheet, "Read a Conversion (Read Cycle)" section
 * for details.
 */
#define MAX127_DATA_LEN		2
#define MAX127_DATA_SHIFT	4

#define MAX127_SIGN_BIT		BIT(11)

struct max127_data {
	struct mutex lock;
	struct i2c_client *client;
	u8 ctrl_byte[MAX127_NUM_CHANNELS];
};

static int max127_select_channel(struct i2c_client *client, u8 ctrl_byte)
{
	int status;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = sizeof(ctrl_byte),
		.buf = &ctrl_byte,
	};

	status = i2c_transfer(client->adapter, &msg, 1);
	if (status < 0)
		return status;
	if (status != 1)
		return -EIO;

	return 0;
}

static int max127_read_channel(struct i2c_client *client, long *val)
{
	int status;
	u8 i2c_data[MAX127_DATA_LEN];
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = I2C_M_RD,
		.len = sizeof(i2c_data),
		.buf = i2c_data,
	};

	status = i2c_transfer(client->adapter, &msg, 1);
	if (status < 0)
		return status;
	if (status != 1)
		return -EIO;

	*val = (i2c_data[1] >> MAX127_DATA_SHIFT) |
		((u16)i2c_data[0] << MAX127_DATA_SHIFT);
	return 0;
}

static long max127_process_raw(u8 ctrl_byte, long raw)
{
	long scale, weight;

	/*
	 * MAX127's data coding is binary in unipolar mode with 1 LSB =
	 * (Full-Scale/4096) and two’s complement binary in bipolar mode
	 * with 1 LSB = [(2 x |FS|)/4096].
	 * Refer to MAX127 datasheet, "Transfer Function" section for
	 * details.
	 */
	scale = (ctrl_byte & MAX127_CTRL_RNG) ? MAX127_FULL_RANGE :
						MAX127_HALF_RANGE;
	if (ctrl_byte & MAX127_CTRL_BIP) {
		weight = (raw & MAX127_SIGN_BIT);
		raw &= ~MAX127_SIGN_BIT;
		raw -= weight;
		raw *= 2;
	}

	return raw * scale / 4096;
}

static int max127_read_input(struct max127_data *data, int channel, long *val)
{
	long raw;
	int status;
	struct i2c_client *client = data->client;
	u8 ctrl_byte = data->ctrl_byte[channel];

	mutex_lock(&data->lock);

	status = max127_select_channel(client, ctrl_byte);
	if (status)
		goto exit;

	status = max127_read_channel(client, &raw);
	if (status)
		goto exit;

	*val = max127_process_raw(ctrl_byte, raw);

exit:
	mutex_unlock(&data->lock);
	return status;
}

static int max127_read_min(struct max127_data *data, int channel, long *val)
{
	u8 rng_bip = (data->ctrl_byte[channel] >> 2) & 3;
	static const int min_input_map[4] = {
		0,			/* RNG=0, BIP=0 */
		-MAX127_HALF_RANGE,	/* RNG=0, BIP=1 */
		0,			/* RNG=1, BIP=0 */
		-MAX127_FULL_RANGE,	/* RNG=1, BIP=1 */
	};

	*val = min_input_map[rng_bip];
	return 0;
}

static int max127_read_max(struct max127_data *data, int channel, long *val)
{
	u8 rng_bip = (data->ctrl_byte[channel] >> 2) & 3;
	static const int max_input_map[4] = {
		MAX127_HALF_RANGE,	/* RNG=0, BIP=0 */
		MAX127_HALF_RANGE,	/* RNG=0, BIP=1 */
		MAX127_FULL_RANGE,	/* RNG=1, BIP=0 */
		MAX127_FULL_RANGE,	/* RNG=1, BIP=1 */
	};

	*val = max_input_map[rng_bip];
	return 0;
}

static int max127_write_min(struct max127_data *data, int channel, long val)
{
	u8 ctrl;

	mutex_lock(&data->lock);

	ctrl = data->ctrl_byte[channel];
	if (val <= -MAX127_FULL_RANGE) {
		ctrl |= (MAX127_CTRL_RNG | MAX127_CTRL_BIP);
	} else if (val < 0) {
		ctrl |= MAX127_CTRL_BIP;
		ctrl &= ~MAX127_CTRL_RNG;
	} else {
		ctrl &= ~MAX127_CTRL_BIP;
	}
	data->ctrl_byte[channel] = ctrl;

	mutex_unlock(&data->lock);

	return 0;
}

static int max127_write_max(struct max127_data *data, int channel, long val)
{
	mutex_lock(&data->lock);

	if (val >= MAX127_FULL_RANGE)
		data->ctrl_byte[channel] |= MAX127_CTRL_RNG;
	else
		data->ctrl_byte[channel] &= ~MAX127_CTRL_RNG;

	mutex_unlock(&data->lock);

	return 0;
}

static umode_t max127_is_visible(const void *_data,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	if (type == hwmon_in) {
		switch (attr) {
		case hwmon_in_input:
			return 0444;

		case hwmon_in_min:
		case hwmon_in_max:
			return 0644;

		default:
			break;
		}
	}

	return 0;
}

static int max127_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	int status;
	struct max127_data *data = dev_get_drvdata(dev);

	if (type != hwmon_in)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_in_input:
		status = max127_read_input(data, channel, val);
		break;

	case hwmon_in_min:
		status = max127_read_min(data, channel, val);
		break;

	case hwmon_in_max:
		status = max127_read_max(data, channel, val);
		break;

	default:
		status = -EOPNOTSUPP;
		break;
	}

	return status;
}

static int max127_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	int status;
	struct max127_data *data = dev_get_drvdata(dev);

	if (type != hwmon_in)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_in_min:
		status = max127_write_min(data, channel, val);
		break;

	case hwmon_in_max:
		status = max127_write_max(data, channel, val);
		break;

	default:
		status = -EOPNOTSUPP;
		break;
	}

	return status;
}

static const struct hwmon_ops max127_hwmon_ops = {
	.is_visible = max127_is_visible,
	.read = max127_read,
	.write = max127_write,
};

static const struct hwmon_channel_info * const max127_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX),
	NULL,
};

static const struct hwmon_chip_info max127_chip_info = {
	.ops = &max127_hwmon_ops,
	.info = max127_info,
};

static int max127_probe(struct i2c_client *client)
{
	int i;
	struct device *hwmon_dev;
	struct max127_data *data;
	struct device *dev = &client->dev;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);
	for (i = 0; i < ARRAY_SIZE(data->ctrl_byte); i++)
		data->ctrl_byte[i] = (MAX127_CTRL_START |
				      MAX127_SET_CHANNEL(i));

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &max127_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max127_id[] = {
	{ "max127", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max127_id);

static struct i2c_driver max127_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "max127",
	},
	.probe_new	= max127_probe,
	.id_table	= max127_id,
};

module_i2c_driver(max127_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Choi <mikechoi@fb.com>");
MODULE_AUTHOR("Tao Ren <rentao.bupt@gmail.com>");
MODULE_DESCRIPTION("MAX127 Hardware Monitoring driver");

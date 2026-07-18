// SPDX-License-Identifier: GPL-2.0+
/*
 * IIO driver for Broadcom APDS9999 Lux Light Sensor
 *
 * Copyright (C) 2026
 * Author: Jose A. Perez de Azpillaga <azpijr@gmail.com>
 *
 * TODO: proximity sensor
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#define APDS9999_REG_MAIN_CTRL		0x00
#define   APDS9999_MAIN_CTRL_LS_EN	BIT(1)
#define APDS9999_REG_LS_MEAS_RATE	0x04
#define   APDS9999_LS_RES_MASK		GENMASK(6, 4)
#define   APDS9999_LS_RATE_MASK	GENMASK(2, 0)
#define APDS9999_REG_LS_GAIN		0x05
#define APDS9999_REG_PART_ID		0x06
#define APDS9999_REG_MAIN_STATUS	0x07
#define   APDS9999_MAIN_STATUS_LS_DATA	BIT(3)
#define APDS9999_REG_LS_DATA_IR_0	0x0A
#define APDS9999_REG_LS_DATA_GREEN_0	0x0D
#define APDS9999_REG_LS_DATA_BLUE_0	0x10
#define APDS9999_REG_LS_DATA_RED_0	0x13

#define APDS9999_PART_ID		0xC2

#define APDS9999_GAIN_1X		0
#define APDS9999_GAIN_3X		1
#define APDS9999_GAIN_6X		2
#define APDS9999_GAIN_9X		3
#define APDS9999_GAIN_18X		4

static const int apds9999_gains[] = {
	[APDS9999_GAIN_1X]  = 1,
	[APDS9999_GAIN_3X]  = 3,
	[APDS9999_GAIN_6X]  = 6,
	[APDS9999_GAIN_9X]  = 9,
	[APDS9999_GAIN_18X] = 18,
};

#define APDS9999_RES_20BIT		0
#define APDS9999_RES_19BIT		1
#define APDS9999_RES_18BIT		2
#define APDS9999_RES_17BIT		3
#define APDS9999_RES_16BIT		4
#define APDS9999_RES_13BIT		5

static const int apds9999_itimes_us[] = {
	[APDS9999_RES_20BIT] = 400 * USEC_PER_MSEC,
	[APDS9999_RES_19BIT] = 200 * USEC_PER_MSEC,
	[APDS9999_RES_18BIT] = 100 * USEC_PER_MSEC,
	[APDS9999_RES_17BIT] =  50 * USEC_PER_MSEC,
	[APDS9999_RES_16BIT] =  25 * USEC_PER_MSEC,
	[APDS9999_RES_13BIT] =   3125,
};

#define APDS9999_RATE_25_MS		0
#define APDS9999_RATE_50_MS		1
#define APDS9999_RATE_100_MS		2
#define APDS9999_RATE_200_MS		3
#define APDS9999_RATE_500_MS		4
#define APDS9999_RATE_1000_MS		5
#define APDS9999_RATE_2000_MS		6

struct apds9999_data {
	struct i2c_client *client;
	/* lock: serializes access to device registers and cached values */
	struct mutex lock;
	int als_gain_idx;
	int als_res;
	int als_rate;
};

static void apds9999_standby(void *client)
{
	i2c_smbus_write_byte_data(client, APDS9999_REG_MAIN_CTRL, 0);
}

/*
 * Apply power-on defaults: 18-bit / 100 ms resolution and rate,
 * 3x gain. These match the datasheet reset values.
 */
static int apds9999_init(struct apds9999_data *data)
{
	struct device *dev = &data->client->dev;
	struct i2c_client *client = data->client;
	u8 regval;
	int ret;

	ret = devm_add_action_or_reset(dev, apds9999_standby, client);
	if (ret)
		return ret;

	guard(mutex)(&data->lock);

	regval = FIELD_PREP(APDS9999_LS_RES_MASK, APDS9999_RES_18BIT) |
		 FIELD_PREP(APDS9999_LS_RATE_MASK, APDS9999_RATE_100_MS);
	ret = i2c_smbus_write_byte_data(client, APDS9999_REG_LS_MEAS_RATE,
					regval);
	if (ret)
		return ret;
	data->als_res = APDS9999_RES_18BIT;
	data->als_rate = APDS9999_RATE_100_MS;

	ret = i2c_smbus_write_byte_data(client, APDS9999_REG_LS_GAIN,
					APDS9999_GAIN_3X);
	if (ret)
		return ret;
	data->als_gain_idx = APDS9999_GAIN_3X;

	return i2c_smbus_write_byte_data(client, APDS9999_REG_MAIN_CTRL,
					 APDS9999_MAIN_CTRL_LS_EN);
}

static int apds9999_read_channel(struct apds9999_data *data, u8 reg,
				 u32 *counts)
{
	struct i2c_client *client = data->client;
	u8 buf[3];
	int ret, tries;

	guard(mutex)(&data->lock);

	/*
	 * Poll MAIN_STATUS for new data.  Timeout: ~2 integration periods
	 * plus margin.  Each try sleeps 20 ms.
	 */
	tries = max(2, (apds9999_itimes_us[data->als_res] * 2) / 20000);

	while (tries--) {
		ret = i2c_smbus_read_byte_data(client,
					       APDS9999_REG_MAIN_STATUS);
		if (ret < 0)
			return ret;
		if (ret & APDS9999_MAIN_STATUS_LS_DATA)
			break;
		fsleep(20000);
	}

	if (tries < 0)
		return -ETIMEDOUT;

	ret = i2c_smbus_read_i2c_block_data(client, reg, sizeof(buf), buf);
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	*counts = get_unaligned_le24(buf) & GENMASK(19, 0);
	return 0;
}

static int apds9999_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct apds9999_data *data = iio_priv(indio_dev);
	int gain, itime_us;
	u64 scale_nano;
	u32 counts;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = apds9999_read_channel(data, chan->address, &counts);
		if (ret)
			return ret;
		*val = (int)counts;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE: {
		u32 remainder;

		/*
		 * Scale (lux per count) = 54 / (gain * integration_time_ms)
		 *
		 * The constant 54 is derived from the datasheet table:
		 *   at gain = 3x, itime = 100 ms -> 0.180 lux/count
		 *   -> C = 0.180 * 3 * 100 = 54
		 *
		 * Expressed as IIO_VAL_INT_PLUS_NANO.
		 */
		gain = apds9999_gains[data->als_gain_idx];
		itime_us = apds9999_itimes_us[data->als_res];

		/* scale_nano = 54 * 1e12 / (gain * itime_us) nano-lux/count */
		scale_nano = div_u64(54ULL * NSEC_PER_SEC * USEC_PER_MSEC, (u32)(gain * itime_us));
		*val = (int)div_u64_rem(scale_nano, NSEC_PER_SEC, &remainder);
		*val2 = (int)remainder;
		return IIO_VAL_INT_PLUS_NANO;
	}
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = apds9999_itimes_us[data->als_res];
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static const struct iio_info apds9999_info = {
	.read_raw = apds9999_read_raw,
};

/*
 * The green channel uses optical coating to approximate the human eye
 * spectral response.  IIO_INTENSITY channels provide raw ADC data for
 * red, green, blue, and IR so userspace can compute weighted lux.
 */
static const struct iio_chan_spec apds9999_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.address = APDS9999_REG_LS_DATA_GREEN_0,
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_RED,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.address = APDS9999_REG_LS_DATA_RED_0,
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_GREEN,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.address = APDS9999_REG_LS_DATA_GREEN_0,
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BLUE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.address = APDS9999_REG_LS_DATA_BLUE_0,
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.address = APDS9999_REG_LS_DATA_IR_0,
	},
};

static int apds9999_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct apds9999_data *data;
	struct iio_dev *indio_dev;
	int ret, part_id;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;

	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	part_id = i2c_smbus_read_byte_data(client, APDS9999_REG_PART_ID);
	if (part_id < 0)
		return dev_err_probe(dev, part_id, "failed to read PART_ID\n");
	if (part_id != APDS9999_PART_ID)
		dev_info(dev, "unexpected PART_ID 0x%02x (expected 0x%02x)\n",
			 part_id, APDS9999_PART_ID);

	ret = apds9999_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize device\n");

	indio_dev->name = "apds9999";
	indio_dev->info = &apds9999_info;
	indio_dev->channels = apds9999_channels;
	indio_dev->num_channels = ARRAY_SIZE(apds9999_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register IIO device\n");

	return 0;
}

static const struct i2c_device_id apds9999_id[] = {
	{ .name = "apds9999" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, apds9999_id);

static const struct of_device_id apds9999_of_match[] = {
	{ .compatible = "brcm,apds9999" },
	{ }
};
MODULE_DEVICE_TABLE(of, apds9999_of_match);

static struct i2c_driver apds9999_driver = {
	.driver = {
		.name = "apds9999",
		.of_match_table = apds9999_of_match,
	},
	.probe = apds9999_probe,
	.id_table = apds9999_id,
};
module_i2c_driver(apds9999_driver);

MODULE_AUTHOR("Jose A. Perez de Azpillaga <azpijr@gmail.com>");
MODULE_DESCRIPTION("APDS-9999 Lux Light Sensor IIO Driver");
MODULE_LICENSE("GPL");

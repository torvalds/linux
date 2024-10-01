// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IIO driver for Lite-On LTR390 ALS and UV sensor
 * (7-bit I2C slave address 0x53)
 *
 * Based on the work of:
 *   Shreeya Patel and Shi Zhigang (LTRF216 Driver)
 *
 * Copyright (C) 2023 Anshul Dalal <anshulusr@gmail.com>
 *
 * Datasheet:
 *   https://optoelectronics.liteon.com/upload/download/DS86-2015-0004/LTR-390UV_Final_%20DS_V1%201.pdf
 *
 * TODO:
 *   - Support for configurable gain and resolution
 *   - Sensor suspend/resume support
 *   - Add support for reading the ALS
 *   - Interrupt support
 */

#include <linux/i2c.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include <linux/iio/iio.h>

#include <asm/unaligned.h>

#define LTR390_MAIN_CTRL		0x00
#define LTR390_ALS_UVS_MEAS_RATE	0x04
#define LTR390_ALS_UVS_GAIN		0x05
#define LTR390_PART_ID			0x06
#define LTR390_ALS_DATA			0x0D
#define LTR390_UVS_DATA			0x10
#define LTR390_INT_CFG			0x19

#define LTR390_PART_NUMBER_ID		0xb
#define LTR390_ALS_UVS_GAIN_MASK	0x07
#define LTR390_ALS_UVS_INT_TIME_MASK	0x70
#define LTR390_ALS_UVS_INT_TIME(x)	FIELD_PREP(LTR390_ALS_UVS_INT_TIME_MASK, (x))

#define LTR390_SW_RESET	      BIT(4)
#define LTR390_UVS_MODE	      BIT(3)
#define LTR390_SENSOR_ENABLE  BIT(1)

#define LTR390_FRACTIONAL_PRECISION 100

/*
 * At 20-bit resolution (integration time: 400ms) and 18x gain, 2300 counts of
 * the sensor are equal to 1 UV Index [Datasheet Page#8].
 *
 * For the default resolution of 18-bit (integration time: 100ms) and default
 * gain of 3x, the counts/uvi are calculated as follows:
 * 2300 / ((3/18) * (100/400)) = 95.83
 */
#define LTR390_COUNTS_PER_UVI 96

/*
 * Window Factor is needed when the device is under Window glass with coated
 * tinted ink. This is to compensate for the light loss due to the lower
 * transmission rate of the window glass and helps * in calculating lux.
 */
#define LTR390_WINDOW_FACTOR 1

enum ltr390_mode {
	LTR390_SET_ALS_MODE,
	LTR390_SET_UVS_MODE,
};

struct ltr390_data {
	struct regmap *regmap;
	struct i2c_client *client;
	/* Protects device from simulataneous reads */
	struct mutex lock;
	enum ltr390_mode mode;
	int gain;
	int int_time_us;
};

static const struct regmap_config ltr390_regmap_config = {
	.name = "ltr390",
	.reg_bits = 8,
	.reg_stride = 1,
	.val_bits = 8,
};

static int ltr390_register_read(struct ltr390_data *data, u8 register_address)
{
	struct device *dev = &data->client->dev;
	int ret;
	u8 recieve_buffer[3];

	ret = regmap_bulk_read(data->regmap, register_address, recieve_buffer,
			       sizeof(recieve_buffer));
	if (ret) {
		dev_err(dev, "failed to read measurement data");
		return ret;
	}

	return get_unaligned_le24(recieve_buffer);
}

static int ltr390_set_mode(struct ltr390_data *data, enum ltr390_mode mode)
{
	int ret;

	if (data->mode == mode)
		return 0;

	switch (mode) {
	case LTR390_SET_ALS_MODE:
		ret = regmap_clear_bits(data->regmap, LTR390_MAIN_CTRL, LTR390_UVS_MODE);
		break;

	case LTR390_SET_UVS_MODE:
		ret = regmap_set_bits(data->regmap, LTR390_MAIN_CTRL, LTR390_UVS_MODE);
		break;
	}

	if (ret)
		return ret;

	data->mode = mode;
	return 0;
}

static int ltr390_counts_per_uvi(struct ltr390_data *data)
{
	const int orig_gain = 18;
	const int orig_int_time = 400;

	return DIV_ROUND_CLOSEST(23 * data->gain * data->int_time_us, 10 * orig_gain * orig_int_time);
}

static int ltr390_read_raw(struct iio_dev *iio_device,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	int ret;
	struct ltr390_data *data = iio_priv(iio_device);

	guard(mutex)(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_UVINDEX:
			ret = ltr390_set_mode(data, LTR390_SET_UVS_MODE);
			if (ret < 0)
				return ret;

			ret = ltr390_register_read(data, LTR390_UVS_DATA);
			if (ret < 0)
				return ret;
			break;

		case IIO_LIGHT:
			ret = ltr390_set_mode(data, LTR390_SET_ALS_MODE);
			if (ret < 0)
				return ret;

			ret = ltr390_register_read(data, LTR390_ALS_DATA);
			if (ret < 0)
				return ret;
			break;

		default:
			return -EINVAL;
		}
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_UVINDEX:
			*val = LTR390_WINDOW_FACTOR * LTR390_FRACTIONAL_PRECISION;
			*val2 = ltr390_counts_per_uvi(data);
			return IIO_VAL_FRACTIONAL;

		case IIO_LIGHT:
			*val = LTR390_WINDOW_FACTOR * 6 * 100;
			*val2 = data->gain * data->int_time_us;
			return IIO_VAL_FRACTIONAL;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_INT_TIME:
		*val = data->int_time_us;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

/* integration time in us */
static const int ltr390_int_time_map_us[] = { 400000, 200000, 100000, 50000, 25000, 12500 };
static const int ltr390_gain_map[] = { 1, 3, 6, 9, 18 };

static const struct iio_chan_spec ltr390_channels[] = {
	/* UV sensor */
	{
		.type = IIO_UVINDEX,
		.scan_index = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) | BIT(IIO_CHAN_INFO_SCALE)
	},
	/* ALS sensor */
	{
		.type = IIO_LIGHT,
		.scan_index = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) | BIT(IIO_CHAN_INFO_SCALE)
	},
};

static int ltr390_set_gain(struct ltr390_data *data, int val)
{
	int ret, idx;

	for (idx = 0; idx < ARRAY_SIZE(ltr390_gain_map); idx++) {
		if (ltr390_gain_map[idx] != val)
			continue;

		guard(mutex)(&data->lock);
		ret = regmap_update_bits(data->regmap,
					LTR390_ALS_UVS_GAIN,
					LTR390_ALS_UVS_GAIN_MASK, idx);
		if (ret)
			return ret;

		data->gain = ltr390_gain_map[idx];
		return 0;
	}

	return -EINVAL;
}

static int ltr390_set_int_time(struct ltr390_data *data, int val)
{
	int ret, idx;

	for (idx = 0; idx < ARRAY_SIZE(ltr390_int_time_map_us); idx++) {
		if (ltr390_int_time_map_us[idx] != val)
			continue;

		guard(mutex)(&data->lock);
		ret = regmap_update_bits(data->regmap,
					LTR390_ALS_UVS_MEAS_RATE,
					LTR390_ALS_UVS_INT_TIME_MASK,
					LTR390_ALS_UVS_INT_TIME(idx));
		if (ret)
			return ret;

		data->int_time_us = ltr390_int_time_map_us[idx];
		return 0;
	}

	return -EINVAL;
}

static int ltr390_read_avail(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
				const int **vals, int *type, int *length, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*length = ARRAY_SIZE(ltr390_gain_map);
		*type = IIO_VAL_INT;
		*vals = ltr390_gain_map;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(ltr390_int_time_map_us);
		*type = IIO_VAL_INT;
		*vals = ltr390_int_time_map_us;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ltr390_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct ltr390_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val2 != 0)
			return -EINVAL;

		return ltr390_set_gain(data, val);

	case IIO_CHAN_INFO_INT_TIME:
		if (val2 != 0)
			return -EINVAL;

		return ltr390_set_int_time(data, val);

	default:
		return -EINVAL;
	}
}

static const struct iio_info ltr390_info = {
	.read_raw = ltr390_read_raw,
	.write_raw = ltr390_write_raw,
	.read_avail = ltr390_read_avail,
};

static int ltr390_probe(struct i2c_client *client)
{
	struct ltr390_data *data;
	struct iio_dev *indio_dev;
	struct device *dev;
	int ret, part_number;

	dev = &client->dev;
	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	data->regmap = devm_regmap_init_i2c(client, &ltr390_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "regmap initialization failed\n");

	data->client = client;
	/* default value of integration time from pg: 15 of the datasheet */
	data->int_time_us = 100000;
	/* default value of gain from pg: 16 of the datasheet */
	data->gain = 3;
	/* default mode for ltr390 is ALS mode */
	data->mode = LTR390_SET_ALS_MODE;

	mutex_init(&data->lock);

	indio_dev->info = &ltr390_info;
	indio_dev->channels = ltr390_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltr390_channels);
	indio_dev->name = "ltr390";

	ret = regmap_read(data->regmap, LTR390_PART_ID, &part_number);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get sensor's part id\n");
	/* Lower 4 bits of `part_number` change with hardware revisions */
	if (part_number >> 4 != LTR390_PART_NUMBER_ID)
		dev_info(dev, "received invalid product id: 0x%x", part_number);
	dev_dbg(dev, "LTR390, product id: 0x%x\n", part_number);

	/* reset sensor, chip fails to respond to this, so ignore any errors */
	regmap_set_bits(data->regmap, LTR390_MAIN_CTRL, LTR390_SW_RESET);

	/* Wait for the registers to reset before proceeding */
	usleep_range(1000, 2000);

	ret = regmap_set_bits(data->regmap, LTR390_MAIN_CTRL, LTR390_SENSOR_ENABLE);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable the sensor\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const struct i2c_device_id ltr390_id[] = {
	{ "ltr390" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, ltr390_id);

static const struct of_device_id ltr390_of_table[] = {
	{ .compatible = "liteon,ltr390" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, ltr390_of_table);

static struct i2c_driver ltr390_driver = {
	.driver = {
		.name = "ltr390",
		.of_match_table = ltr390_of_table,
	},
	.probe = ltr390_probe,
	.id_table = ltr390_id,
};
module_i2c_driver(ltr390_driver);

MODULE_AUTHOR("Anshul Dalal <anshulusr@gmail.com>");
MODULE_DESCRIPTION("Lite-On LTR390 ALS and UV sensor Driver");
MODULE_LICENSE("GPL");

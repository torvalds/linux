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

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/events.h>

#include <linux/unaligned.h>

#define LTR390_MAIN_CTRL		0x00
#define LTR390_ALS_UVS_MEAS_RATE	0x04
#define LTR390_ALS_UVS_GAIN		0x05
#define LTR390_PART_ID			0x06
#define LTR390_MAIN_STATUS		0x07
#define LTR390_ALS_DATA			0x0D
#define LTR390_UVS_DATA			0x10
#define LTR390_INT_CFG			0x19
#define LTR390_INT_PST			0x1A
#define LTR390_THRESH_UP		0x21
#define LTR390_THRESH_LOW		0x24

#define LTR390_PART_NUMBER_ID		0xb
#define LTR390_ALS_UVS_GAIN_MASK	GENMASK(2, 0)
#define LTR390_ALS_UVS_MEAS_RATE_MASK	GENMASK(2, 0)
#define LTR390_ALS_UVS_INT_TIME_MASK	GENMASK(6, 4)
#define LTR390_ALS_UVS_INT_TIME(x)	FIELD_PREP(LTR390_ALS_UVS_INT_TIME_MASK, (x))
#define LTR390_INT_PST_MASK		GENMASK(7, 4)
#define LTR390_INT_PST_VAL(x)		FIELD_PREP(LTR390_INT_PST_MASK, (x))

#define LTR390_SW_RESET	      BIT(4)
#define LTR390_UVS_MODE	      BIT(3)
#define LTR390_SENSOR_ENABLE  BIT(1)
#define LTR390_LS_INT_EN      BIT(2)
#define LTR390_LS_INT_SEL_UVS BIT(5)

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

enum ltr390_meas_rate {
	LTR390_GET_FREQ,
	LTR390_GET_PERIOD,
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

/* Sampling frequency is in mili Hz and mili Seconds */
static const int ltr390_samp_freq_table[][2] = {
		[0] = { 40000, 25 },
		[1] = { 20000, 50 },
		[2] = { 10000, 100 },
		[3] = { 5000, 200 },
		[4] = { 2000, 500 },
		[5] = { 1000, 1000 },
		[6] = { 500, 2000 },
		[7] = { 500, 2000 },
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

static int ltr390_get_samp_freq_or_period(struct ltr390_data *data,
					enum ltr390_meas_rate option)
{
	int ret, value;

	ret = regmap_read(data->regmap, LTR390_ALS_UVS_MEAS_RATE, &value);
	if (ret < 0)
		return ret;
	value = FIELD_GET(LTR390_ALS_UVS_MEAS_RATE_MASK, value);

	return ltr390_samp_freq_table[value][option];
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

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = ltr390_get_samp_freq_or_period(data, LTR390_GET_FREQ);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

/* integration time in us */
static const int ltr390_int_time_map_us[] = { 400000, 200000, 100000, 50000, 25000, 12500 };
static const int ltr390_gain_map[] = { 1, 3, 6, 9, 18 };
static const int ltr390_freq_map[] = { 40000, 20000, 10000, 5000, 2000, 1000, 500, 500 };

static const struct iio_event_spec ltr390_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				BIT(IIO_EV_INFO_PERIOD),
	}
};

static const struct iio_chan_spec ltr390_channels[] = {
	/* UV sensor */
	{
		.type = IIO_UVINDEX,
		.scan_index = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
							BIT(IIO_CHAN_INFO_SCALE) |
							BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.event_spec = ltr390_event_spec,
		.num_event_specs = ARRAY_SIZE(ltr390_event_spec),
	},
	/* ALS sensor */
	{
		.type = IIO_LIGHT,
		.scan_index = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
							BIT(IIO_CHAN_INFO_SCALE) |
							BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.event_spec = ltr390_event_spec,
		.num_event_specs = ARRAY_SIZE(ltr390_event_spec),
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

static int ltr390_set_samp_freq(struct ltr390_data *data, int val)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(ltr390_samp_freq_table); idx++) {
		if (ltr390_samp_freq_table[idx][0] != val)
			continue;

		guard(mutex)(&data->lock);
		return regmap_update_bits(data->regmap,
					LTR390_ALS_UVS_MEAS_RATE,
					LTR390_ALS_UVS_MEAS_RATE_MASK, idx);
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
	case IIO_CHAN_INFO_SAMP_FREQ:
		*length = ARRAY_SIZE(ltr390_freq_map);
		*type = IIO_VAL_INT;
		*vals = ltr390_freq_map;
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

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2 != 0)
			return -EINVAL;

		return ltr390_set_samp_freq(data, val);

	default:
		return -EINVAL;
	}
}

static int ltr390_read_intr_prst(struct ltr390_data *data, int *val)
{
	int ret, prst, samp_period;

	samp_period = ltr390_get_samp_freq_or_period(data, LTR390_GET_PERIOD);
	ret = regmap_read(data->regmap, LTR390_INT_PST, &prst);
	if (ret < 0)
		return ret;
	*val = prst * samp_period;

	return IIO_VAL_INT;
}

static int ltr390_write_intr_prst(struct ltr390_data *data, int val)
{
	int ret, samp_period, new_val;

	samp_period = ltr390_get_samp_freq_or_period(data, LTR390_GET_PERIOD);

	/* persist period should be greater than or equal to samp period */
	if (val < samp_period)
		return -EINVAL;

	new_val = DIV_ROUND_UP(val, samp_period);
	if (new_val < 0 || new_val > 0x0f)
		return -EINVAL;

	guard(mutex)(&data->lock);
	ret = regmap_update_bits(data->regmap,
				LTR390_INT_PST,
				LTR390_INT_PST_MASK,
				LTR390_INT_PST_VAL(new_val));
	if (ret)
		return ret;

	return 0;
}

static int ltr390_read_threshold(struct iio_dev *indio_dev,
				enum iio_event_direction dir,
				int *val, int *val2)
{
	struct ltr390_data *data = iio_priv(indio_dev);
	int ret;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		ret = ltr390_register_read(data, LTR390_THRESH_UP);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;

	case IIO_EV_DIR_FALLING:
		ret = ltr390_register_read(data, LTR390_THRESH_LOW);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ltr390_write_threshold(struct iio_dev *indio_dev,
				enum iio_event_direction dir,
				int val, int val2)
{
	struct ltr390_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->lock);
	switch (dir) {
	case IIO_EV_DIR_RISING:
		return regmap_bulk_write(data->regmap, LTR390_THRESH_UP, &val, 3);

	case IIO_EV_DIR_FALLING:
		return regmap_bulk_write(data->regmap, LTR390_THRESH_LOW, &val, 3);

	default:
		return -EINVAL;
	}
}

static int ltr390_read_event_value(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int *val, int *val2)
{
	switch (info) {
	case IIO_EV_INFO_VALUE:
		return ltr390_read_threshold(indio_dev, dir, val, val2);

	case IIO_EV_INFO_PERIOD:
		return ltr390_read_intr_prst(iio_priv(indio_dev), val);

	default:
		return -EINVAL;
	}
}

static int ltr390_write_event_value(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int val, int val2)
{
	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (val2 != 0)
			return -EINVAL;

		return ltr390_write_threshold(indio_dev, dir, val, val2);

	case IIO_EV_INFO_PERIOD:
		if (val2 != 0)
			return -EINVAL;

		return ltr390_write_intr_prst(iio_priv(indio_dev), val);

	default:
		return -EINVAL;
	}
}

static int ltr390_read_event_config(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir)
{
	struct ltr390_data *data = iio_priv(indio_dev);
	int ret, status;

	ret = regmap_read(data->regmap, LTR390_INT_CFG, &status);
	if (ret < 0)
		return ret;

	return FIELD_GET(LTR390_LS_INT_EN, status);
}

static int ltr390_write_event_config(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				bool state)
{
	struct ltr390_data *data = iio_priv(indio_dev);
	int ret;

	if (!state)
		return regmap_clear_bits(data->regmap, LTR390_INT_CFG, LTR390_LS_INT_EN);

	guard(mutex)(&data->lock);
	ret = regmap_set_bits(data->regmap, LTR390_INT_CFG, LTR390_LS_INT_EN);
	if (ret < 0)
		return ret;

	switch (chan->type) {
	case IIO_LIGHT:
		ret = ltr390_set_mode(data, LTR390_SET_ALS_MODE);
		if (ret < 0)
			return ret;

		return regmap_clear_bits(data->regmap, LTR390_INT_CFG, LTR390_LS_INT_SEL_UVS);

	case IIO_UVINDEX:
		ret = ltr390_set_mode(data, LTR390_SET_UVS_MODE);
		if (ret < 0)
			return ret;

		return regmap_set_bits(data->regmap, LTR390_INT_CFG, LTR390_LS_INT_SEL_UVS);

	default:
		return -EINVAL;
	}
}

static const struct iio_info ltr390_info = {
	.read_raw = ltr390_read_raw,
	.write_raw = ltr390_write_raw,
	.read_avail = ltr390_read_avail,
	.read_event_value = ltr390_read_event_value,
	.read_event_config = ltr390_read_event_config,
	.write_event_value = ltr390_write_event_value,
	.write_event_config = ltr390_write_event_config,
};

static irqreturn_t ltr390_interrupt_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ltr390_data *data = iio_priv(indio_dev);
	int ret, status;

	/* Reading the status register to clear the interrupt flag, Datasheet pg: 17*/
	ret = regmap_read(data->regmap, LTR390_MAIN_STATUS, &status);
	if (ret < 0)
		return ret;

	switch (data->mode) {
	case LTR390_SET_ALS_MODE:
		iio_push_event(indio_dev,
				IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
				IIO_EV_TYPE_THRESH,
				IIO_EV_DIR_EITHER),
				iio_get_time_ns(indio_dev));
		break;

	case LTR390_SET_UVS_MODE:
		iio_push_event(indio_dev,
				IIO_UNMOD_EVENT_CODE(IIO_UVINDEX, 0,
				IIO_EV_TYPE_THRESH,
				IIO_EV_DIR_EITHER),
				iio_get_time_ns(indio_dev));
		break;
	}

	return IRQ_HANDLED;
}

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

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq,
						NULL, ltr390_interrupt_handler,
						IRQF_ONESHOT,
						"ltr390_thresh_event",
						indio_dev);
		if (ret)
			return dev_err_probe(dev, ret,
					     "request irq (%d) failed\n", client->irq);
	}

	return devm_iio_device_register(dev, indio_dev);
}

static int ltr390_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ltr390_data *data = iio_priv(indio_dev);

	return regmap_clear_bits(data->regmap, LTR390_MAIN_CTRL,
				LTR390_SENSOR_ENABLE);
}

static int ltr390_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ltr390_data *data = iio_priv(indio_dev);

	return regmap_set_bits(data->regmap, LTR390_MAIN_CTRL,
				LTR390_SENSOR_ENABLE);
}

static DEFINE_SIMPLE_DEV_PM_OPS(ltr390_pm_ops, ltr390_suspend, ltr390_resume);

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
		.pm = pm_sleep_ptr(&ltr390_pm_ops),
	},
	.probe = ltr390_probe,
	.id_table = ltr390_id,
};
module_i2c_driver(ltr390_driver);

MODULE_AUTHOR("Anshul Dalal <anshulusr@gmail.com>");
MODULE_DESCRIPTION("Lite-On LTR390 ALS and UV sensor Driver");
MODULE_LICENSE("GPL");

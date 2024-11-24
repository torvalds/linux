// SPDX-License-Identifier: GPL-2.0+
/*
 * VEML6030, VMEL6035 and VEML7700 Ambient Light Sensors
 *
 * Copyright (c) 2019, Rishi Gupta <gupt21@gmail.com>
 *
 * VEML6030:
 * Datasheet: https://www.vishay.com/docs/84366/veml6030.pdf
 * Appnote-84367: https://www.vishay.com/docs/84367/designingveml6030.pdf
 *
 * VEML6035:
 * Datasheet: https://www.vishay.com/docs/84889/veml6035.pdf
 * Appnote-84944: https://www.vishay.com/docs/84944/designingveml6035.pdf
 *
 * VEML7700:
 * Datasheet: https://www.vishay.com/docs/84286/veml7700.pdf
 * Appnote-84323: https://www.vishay.com/docs/84323/designingveml7700.pdf
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/* Device registers */
#define VEML6030_REG_ALS_CONF   0x00
#define VEML6030_REG_ALS_WH     0x01
#define VEML6030_REG_ALS_WL     0x02
#define VEML6030_REG_ALS_PSM    0x03
#define VEML6030_REG_ALS_DATA   0x04
#define VEML6030_REG_WH_DATA    0x05
#define VEML6030_REG_ALS_INT    0x06
#define VEML6030_REG_DATA(ch)   (VEML6030_REG_ALS_DATA + (ch))

/* Bit masks for specific functionality */
#define VEML6030_ALS_IT       GENMASK(9, 6)
#define VEML6030_PSM          GENMASK(2, 1)
#define VEML6030_ALS_PERS     GENMASK(5, 4)
#define VEML6030_ALS_GAIN     GENMASK(12, 11)
#define VEML6030_PSM_EN       BIT(0)
#define VEML6030_INT_TH_LOW   BIT(15)
#define VEML6030_INT_TH_HIGH  BIT(14)
#define VEML6030_ALS_INT_EN   BIT(1)
#define VEML6030_ALS_SD       BIT(0)

#define VEML6035_GAIN_M       GENMASK(12, 10)
#define VEML6035_GAIN         BIT(10)
#define VEML6035_DG           BIT(11)
#define VEML6035_SENS         BIT(12)
#define VEML6035_INT_CHAN     BIT(3)
#define VEML6035_CHAN_EN      BIT(2)

enum veml6030_scan {
	VEML6030_SCAN_ALS,
	VEML6030_SCAN_WH,
	VEML6030_SCAN_TIMESTAMP,
};

struct veml603x_chip {
	const char *name;
	const int(*scale_vals)[][2];
	const int num_scale_vals;
	const struct iio_chan_spec *channels;
	const int num_channels;
	int (*hw_init)(struct iio_dev *indio_dev, struct device *dev);
	int (*set_info)(struct iio_dev *indio_dev);
	int (*set_als_gain)(struct iio_dev *indio_dev, int val, int val2);
	int (*get_als_gain)(struct iio_dev *indio_dev, int *val, int *val2);
};

/*
 * The resolution depends on both gain and integration time. The
 * cur_resolution stores one of the resolution mentioned in the
 * table during startup and gets updated whenever integration time
 * or gain is changed.
 *
 * Table 'resolution and maximum detection range' in the appnotes
 * is visualized as a 2D array. The cur_gain stores index of gain
 * in this table (0-3 for VEML6030, 0-5 for VEML6035) while the
 * cur_integration_time holds index of integration time (0-5).
 */
struct veml6030_data {
	struct i2c_client *client;
	struct regmap *regmap;
	int cur_resolution;
	int cur_gain;
	int cur_integration_time;
	const struct veml603x_chip *chip;
};

static const int veml6030_it_times[][2] = {
	{ 0, 25000 },
	{ 0, 50000 },
	{ 0, 100000 },
	{ 0, 200000 },
	{ 0, 400000 },
	{ 0, 800000 },
};

/*
 * Scale is 1/gain. Value 0.125 is ALS gain x (1/8), 0.25 is
 * ALS gain x (1/4), 0.5 is ALS gain x (1/2), 1.0 is ALS gain x 1,
 * 2.0 is ALS gain x2, and 4.0 is ALS gain x 4.
 */
static const int veml6030_scale_vals[][2] = {
	{ 0, 125000 },
	{ 0, 250000 },
	{ 1, 0 },
	{ 2, 0 },
};

static const int veml6035_scale_vals[][2] = {
	{ 0, 125000 },
	{ 0, 250000 },
	{ 0, 500000 },
	{ 1, 0 },
	{ 2, 0 },
	{ 4, 0 },
};

/*
 * Persistence = 1/2/4/8 x integration time
 * Minimum time for which light readings must stay above configured
 * threshold to assert the interrupt.
 */
static const char * const period_values[] = {
		"0.1 0.2 0.4 0.8",
		"0.2 0.4 0.8 1.6",
		"0.4 0.8 1.6 3.2",
		"0.8 1.6 3.2 6.4",
		"0.05 0.1 0.2 0.4",
		"0.025 0.050 0.1 0.2"
};

/*
 * Return list of valid period values in seconds corresponding to
 * the currently active integration time.
 */
static ssize_t in_illuminance_period_available_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct veml6030_data *data = iio_priv(dev_to_iio_dev(dev));
	int ret, reg, x;

	ret = regmap_read(data->regmap, VEML6030_REG_ALS_CONF, &reg);
	if (ret) {
		dev_err(&data->client->dev,
				"can't read als conf register %d\n", ret);
		return ret;
	}

	ret = ((reg >> 6) & 0xF);
	switch (ret) {
	case 0:
	case 1:
	case 2:
	case 3:
		x = ret;
		break;
	case 8:
		x = 4;
		break;
	case 12:
		x = 5;
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%s\n", period_values[x]);
}

static IIO_DEVICE_ATTR_RO(in_illuminance_period_available, 0);

static struct attribute *veml6030_event_attributes[] = {
	&iio_dev_attr_in_illuminance_period_available.dev_attr.attr,
	NULL
};

static const struct attribute_group veml6030_event_attr_group = {
	.attrs = veml6030_event_attributes,
};

static int veml6030_als_pwr_on(struct veml6030_data *data)
{
	int ret;

	ret = regmap_clear_bits(data->regmap, VEML6030_REG_ALS_CONF,
				VEML6030_ALS_SD);
	if (ret)
		return ret;

	/* Wait 4 ms to let processor & oscillator start correctly */
	fsleep(4000);

	return 0;
}

static int veml6030_als_shut_down(struct veml6030_data *data)
{
	return regmap_set_bits(data->regmap, VEML6030_REG_ALS_CONF,
				 VEML6030_ALS_SD);
}

static void veml6030_als_shut_down_action(void *data)
{
	veml6030_als_shut_down(data);
}

static const struct iio_event_spec veml6030_event_spec[] = {
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
		.mask_separate = BIT(IIO_EV_INFO_PERIOD) |
		BIT(IIO_EV_INFO_ENABLE),
	},
};

/* Channel number */
enum veml6030_chan {
	CH_ALS,
	CH_WHITE,
};

static const struct iio_chan_spec veml6030_channels[] = {
	{
		.type = IIO_LIGHT,
		.channel = CH_ALS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_PROCESSED) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.event_spec = veml6030_event_spec,
		.num_event_specs = ARRAY_SIZE(veml6030_event_spec),
		.scan_index = VEML6030_SCAN_ALS,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_INTENSITY,
		.channel = CH_WHITE,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6030_SCAN_WH,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(VEML6030_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec veml7700_channels[] = {
	{
		.type = IIO_LIGHT,
		.channel = CH_ALS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_PROCESSED) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6030_SCAN_ALS,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_INTENSITY,
		.channel = CH_WHITE,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6030_SCAN_WH,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(VEML6030_SCAN_TIMESTAMP),
};

static const struct regmap_config veml6030_regmap_config = {
	.name = "veml6030_regmap",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = VEML6030_REG_ALS_INT,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int veml6030_get_intgrn_tm(struct iio_dev *indio_dev,
						int *val, int *val2)
{
	int ret, reg;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = regmap_read(data->regmap, VEML6030_REG_ALS_CONF, &reg);
	if (ret) {
		dev_err(&data->client->dev,
				"can't read als conf register %d\n", ret);
		return ret;
	}

	switch ((reg >> 6) & 0xF) {
	case 0:
		*val2 = 100000;
		break;
	case 1:
		*val2 = 200000;
		break;
	case 2:
		*val2 = 400000;
		break;
	case 3:
		*val2 = 800000;
		break;
	case 8:
		*val2 = 50000;
		break;
	case 12:
		*val2 = 25000;
		break;
	default:
		return -EINVAL;
	}

	*val = 0;
	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6030_set_intgrn_tm(struct iio_dev *indio_dev,
						int val, int val2)
{
	int ret, new_int_time, int_idx;
	struct veml6030_data *data = iio_priv(indio_dev);

	if (val)
		return -EINVAL;

	switch (val2) {
	case 25000:
		new_int_time = 0x300;
		int_idx = 5;
		break;
	case 50000:
		new_int_time = 0x200;
		int_idx = 4;
		break;
	case 100000:
		new_int_time = 0x00;
		int_idx = 3;
		break;
	case 200000:
		new_int_time = 0x40;
		int_idx = 2;
		break;
	case 400000:
		new_int_time = 0x80;
		int_idx = 1;
		break;
	case 800000:
		new_int_time = 0xC0;
		int_idx = 0;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(data->regmap, VEML6030_REG_ALS_CONF,
					VEML6030_ALS_IT, new_int_time);
	if (ret) {
		dev_err(&data->client->dev,
				"can't update als integration time %d\n", ret);
		return ret;
	}

	/*
	 * Cache current integration time and update resolution. For every
	 * increase in integration time to next level, resolution is halved
	 * and vice-versa.
	 */
	if (data->cur_integration_time < int_idx)
		data->cur_resolution <<= int_idx - data->cur_integration_time;
	else if (data->cur_integration_time > int_idx)
		data->cur_resolution >>= data->cur_integration_time - int_idx;

	data->cur_integration_time = int_idx;

	return ret;
}

static int veml6030_read_persistence(struct iio_dev *indio_dev,
						int *val, int *val2)
{
	int ret, reg, period, x, y;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = veml6030_get_intgrn_tm(indio_dev, &x, &y);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, VEML6030_REG_ALS_CONF, &reg);
	if (ret) {
		dev_err(&data->client->dev,
				"can't read als conf register %d\n", ret);
	}

	/* integration time multiplied by 1/2/4/8 */
	period = y * (1 << ((reg >> 4) & 0x03));

	*val = period / 1000000;
	*val2 = period % 1000000;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6030_write_persistence(struct iio_dev *indio_dev,
						int val, int val2)
{
	int ret, period, x, y;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = veml6030_get_intgrn_tm(indio_dev, &x, &y);
	if (ret < 0)
		return ret;

	if (!val) {
		period = val2 / y;
	} else {
		if ((val == 1) && (val2 == 600000))
			period = 1600000 / y;
		else if ((val == 3) && (val2 == 200000))
			period = 3200000 / y;
		else if ((val == 6) && (val2 == 400000))
			period = 6400000 / y;
		else
			period = -1;
	}

	if (period <= 0 || period > 8 || hweight8(period) != 1)
		return -EINVAL;

	ret = regmap_update_bits(data->regmap, VEML6030_REG_ALS_CONF,
				VEML6030_ALS_PERS, (ffs(period) - 1) << 4);
	if (ret)
		dev_err(&data->client->dev,
				"can't set persistence value %d\n", ret);

	return ret;
}

/*
 * Cache currently set gain & update resolution. For every
 * increase in the gain to next level, resolution is halved
 * and vice-versa.
 */
static void veml6030_update_gain_res(struct veml6030_data *data, int gain_idx)
{
	if (data->cur_gain < gain_idx)
		data->cur_resolution <<= gain_idx - data->cur_gain;
	else if (data->cur_gain > gain_idx)
		data->cur_resolution >>= data->cur_gain - gain_idx;

	data->cur_gain = gain_idx;
}

static int veml6030_set_als_gain(struct iio_dev *indio_dev,
						int val, int val2)
{
	int ret, new_gain, gain_idx;
	struct veml6030_data *data = iio_priv(indio_dev);

	if (val == 0 && val2 == 125000) {
		new_gain = 0x1000; /* 0x02 << 11 */
		gain_idx = 3;
	} else if (val == 0 && val2 == 250000) {
		new_gain = 0x1800;
		gain_idx = 2;
	} else if (val == 1 && val2 == 0) {
		new_gain = 0x00;
		gain_idx = 1;
	} else if (val == 2 && val2 == 0) {
		new_gain = 0x800;
		gain_idx = 0;
	} else {
		return -EINVAL;
	}

	ret = regmap_update_bits(data->regmap, VEML6030_REG_ALS_CONF,
					VEML6030_ALS_GAIN, new_gain);
	if (ret) {
		dev_err(&data->client->dev,
				"can't set als gain %d\n", ret);
		return ret;
	}

	veml6030_update_gain_res(data, gain_idx);

	return 0;
}

static int veml6035_set_als_gain(struct iio_dev *indio_dev, int val, int val2)
{
	int ret, new_gain, gain_idx;
	struct veml6030_data *data = iio_priv(indio_dev);

	if (val == 0 && val2 == 125000) {
		new_gain = VEML6035_SENS;
		gain_idx = 5;
	} else if (val == 0 && val2 == 250000) {
		new_gain = VEML6035_SENS | VEML6035_GAIN;
		gain_idx = 4;
	} else if (val == 0 && val2 == 500000) {
		new_gain = VEML6035_SENS | VEML6035_GAIN |
			VEML6035_DG;
		gain_idx = 3;
	} else if (val == 1 && val2 == 0) {
		new_gain = 0x0000;
		gain_idx = 2;
	} else if (val == 2 && val2 == 0) {
		new_gain = VEML6035_GAIN;
		gain_idx = 1;
	} else if (val == 4 && val2 == 0) {
		new_gain = VEML6035_GAIN | VEML6035_DG;
		gain_idx = 0;
	} else {
		return -EINVAL;
	}

	ret = regmap_update_bits(data->regmap, VEML6030_REG_ALS_CONF,
				 VEML6035_GAIN_M, new_gain);
	if (ret) {
		dev_err(&data->client->dev, "can't set als gain %d\n", ret);
		return ret;
	}

	veml6030_update_gain_res(data, gain_idx);

	return 0;
}

static int veml6030_get_als_gain(struct iio_dev *indio_dev,
						int *val, int *val2)
{
	int ret, reg;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = regmap_read(data->regmap, VEML6030_REG_ALS_CONF, &reg);
	if (ret) {
		dev_err(&data->client->dev,
				"can't read als conf register %d\n", ret);
		return ret;
	}

	switch ((reg >> 11) & 0x03) {
	case 0:
		*val = 1;
		*val2 = 0;
		break;
	case 1:
		*val = 2;
		*val2 = 0;
		break;
	case 2:
		*val = 0;
		*val2 = 125000;
		break;
	case 3:
		*val = 0;
		*val2 = 250000;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6035_get_als_gain(struct iio_dev *indio_dev, int *val, int *val2)
{
	int ret, reg;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = regmap_read(data->regmap, VEML6030_REG_ALS_CONF, &reg);
	if (ret) {
		dev_err(&data->client->dev,
			"can't read als conf register %d\n", ret);
		return ret;
	}

	switch (FIELD_GET(VEML6035_GAIN_M, reg)) {
	case 0:
		*val = 1;
		*val2 = 0;
		break;
	case 1:
	case 2:
		*val = 2;
		*val2 = 0;
		break;
	case 3:
		*val = 4;
		*val2 = 0;
		break;
	case 4:
		*val = 0;
		*val2 = 125000;
		break;
	case 5:
	case 6:
		*val = 0;
		*val2 = 250000;
		break;
	case 7:
		*val = 0;
		*val2 = 500000;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6030_read_thresh(struct iio_dev *indio_dev,
						int *val, int *val2, int dir)
{
	int ret, reg;
	struct veml6030_data *data = iio_priv(indio_dev);

	if (dir == IIO_EV_DIR_RISING)
		ret = regmap_read(data->regmap, VEML6030_REG_ALS_WH, &reg);
	else
		ret = regmap_read(data->regmap, VEML6030_REG_ALS_WL, &reg);
	if (ret) {
		dev_err(&data->client->dev,
				"can't read als threshold value %d\n", ret);
		return ret;
	}

	*val = reg & 0xffff;
	return IIO_VAL_INT;
}

static int veml6030_write_thresh(struct iio_dev *indio_dev,
						int val, int val2, int dir)
{
	int ret;
	struct veml6030_data *data = iio_priv(indio_dev);

	if (val > 0xFFFF || val < 0 || val2)
		return -EINVAL;

	if (dir == IIO_EV_DIR_RISING) {
		ret = regmap_write(data->regmap, VEML6030_REG_ALS_WH, val);
		if (ret)
			dev_err(&data->client->dev,
					"can't set high threshold %d\n", ret);
	} else {
		ret = regmap_write(data->regmap, VEML6030_REG_ALS_WL, val);
		if (ret)
			dev_err(&data->client->dev,
					"can't set low threshold %d\n", ret);
	}

	return ret;
}

/*
 * Provide both raw as well as light reading in lux.
 * light (in lux) = resolution * raw reading
 */
static int veml6030_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	int ret, reg;
	struct veml6030_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	struct device *dev = &data->client->dev;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = regmap_read(regmap, VEML6030_REG_ALS_DATA, &reg);
			if (ret < 0) {
				dev_err(dev, "can't read als data %d\n", ret);
				return ret;
			}
			if (mask == IIO_CHAN_INFO_PROCESSED) {
				*val = (reg * data->cur_resolution) / 10000;
				*val2 = (reg * data->cur_resolution) % 10000 * 100;
				return IIO_VAL_INT_PLUS_MICRO;
			}
			*val = reg;
			return IIO_VAL_INT;
		case IIO_INTENSITY:
			ret = regmap_read(regmap, VEML6030_REG_WH_DATA, &reg);
			if (ret < 0) {
				dev_err(dev, "can't read white data %d\n", ret);
				return ret;
			}
			*val = reg;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_INT_TIME:
		return veml6030_get_intgrn_tm(indio_dev, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return data->chip->get_als_gain(indio_dev, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6030_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	struct veml6030_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*vals = (int *)&veml6030_it_times;
		*length = 2 * ARRAY_SIZE(veml6030_it_times);
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)*data->chip->scale_vals;
		*length = 2 * data->chip->num_scale_vals;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	}

	return -EINVAL;
}

static int veml6030_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct veml6030_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return veml6030_set_intgrn_tm(indio_dev, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return data->chip->set_als_gain(indio_dev, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6030_read_event_val(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, enum iio_event_info info,
		int *val, int *val2)
{
	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
		case IIO_EV_DIR_FALLING:
			return veml6030_read_thresh(indio_dev, val, val2, dir);
		default:
			return -EINVAL;
		}
		break;
	case IIO_EV_INFO_PERIOD:
		return veml6030_read_persistence(indio_dev, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6030_write_event_val(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, enum iio_event_info info,
		int val, int val2)
{
	switch (info) {
	case IIO_EV_INFO_VALUE:
		return veml6030_write_thresh(indio_dev, val, val2, dir);
	case IIO_EV_INFO_PERIOD:
		return veml6030_write_persistence(indio_dev, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6030_read_interrupt_config(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir)
{
	int ret, reg;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = regmap_read(data->regmap, VEML6030_REG_ALS_CONF, &reg);
	if (ret) {
		dev_err(&data->client->dev,
				"can't read als conf register %d\n", ret);
		return ret;
	}

	if (reg & VEML6030_ALS_INT_EN)
		return 1;
	else
		return 0;
}

/*
 * Sensor should not be measuring light when interrupt is configured.
 * Therefore correct sequence to configure interrupt functionality is:
 * shut down -> enable/disable interrupt -> power on
 *
 * state = 1 enables interrupt, state = 0 disables interrupt
 */
static int veml6030_write_interrupt_config(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, bool state)
{
	int ret;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = veml6030_als_shut_down(data);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"can't disable als to configure interrupt %d\n", ret);
		return ret;
	}

	/* enable interrupt + power on */
	ret = regmap_update_bits(data->regmap, VEML6030_REG_ALS_CONF,
			VEML6030_ALS_INT_EN | VEML6030_ALS_SD, state << 1);
	if (ret)
		dev_err(&data->client->dev,
			"can't enable interrupt & poweron als %d\n", ret);

	return ret;
}

static const struct iio_info veml6030_info = {
	.read_raw  = veml6030_read_raw,
	.read_avail  = veml6030_read_avail,
	.write_raw = veml6030_write_raw,
	.read_event_value = veml6030_read_event_val,
	.write_event_value	= veml6030_write_event_val,
	.read_event_config = veml6030_read_interrupt_config,
	.write_event_config	= veml6030_write_interrupt_config,
	.event_attrs = &veml6030_event_attr_group,
};

static const struct iio_info veml6030_info_no_irq = {
	.read_raw  = veml6030_read_raw,
	.read_avail  = veml6030_read_avail,
	.write_raw = veml6030_write_raw,
};

static irqreturn_t veml6030_event_handler(int irq, void *private)
{
	int ret, reg, evtdir;
	struct iio_dev *indio_dev = private;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = regmap_read(data->regmap, VEML6030_REG_ALS_INT, &reg);
	if (ret) {
		dev_err(&data->client->dev,
				"can't read als interrupt register %d\n", ret);
		return IRQ_HANDLED;
	}

	/* Spurious interrupt handling */
	if (!(reg & (VEML6030_INT_TH_HIGH | VEML6030_INT_TH_LOW)))
		return IRQ_NONE;

	if (reg & VEML6030_INT_TH_HIGH)
		evtdir = IIO_EV_DIR_RISING;
	else
		evtdir = IIO_EV_DIR_FALLING;

	iio_push_event(indio_dev, IIO_UNMOD_EVENT_CODE(IIO_INTENSITY,
					0, IIO_EV_TYPE_THRESH, evtdir),
					iio_get_time_ns(indio_dev));

	return IRQ_HANDLED;
}

static irqreturn_t veml6030_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio = pf->indio_dev;
	struct veml6030_data *data = iio_priv(iio);
	unsigned int reg;
	int ch, ret, i = 0;
	struct {
		u16 chans[2];
		aligned_s64 timestamp;
	} scan;

	memset(&scan, 0, sizeof(scan));

	iio_for_each_active_channel(iio, ch) {
		ret = regmap_read(data->regmap, VEML6030_REG_DATA(ch),
				  &reg);
		if (ret)
			goto done;

		scan.chans[i++] = reg;
	}

	iio_push_to_buffers_with_timestamp(iio, &scan, pf->timestamp);

done:
	iio_trigger_notify_done(iio->trig);

	return IRQ_HANDLED;
}

static int veml6030_set_info(struct iio_dev *indio_dev)
{
	struct veml6030_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int ret;

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, veml6030_event_handler,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						indio_dev->name, indio_dev);
		if (ret < 0)
			return dev_err_probe(&client->dev, ret,
					     "irq %d request failed\n",
					     client->irq);

		indio_dev->info = &veml6030_info;
	} else {
		indio_dev->info = &veml6030_info_no_irq;
	}

	return 0;
}

static int veml7700_set_info(struct iio_dev *indio_dev)
{
	indio_dev->info = &veml6030_info_no_irq;

	return 0;
}

/*
 * Set ALS gain to 1/8, integration time to 100 ms, PSM to mode 2,
 * persistence to 1 x integration time and the threshold
 * interrupt disabled by default. First shutdown the sensor,
 * update registers and then power on the sensor.
 */
static int veml6030_hw_init(struct iio_dev *indio_dev, struct device *dev)
{
	int ret, val;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = veml6030_als_shut_down(data);
	if (ret)
		return dev_err_probe(dev, ret, "can't shutdown als\n");

	ret = regmap_write(data->regmap, VEML6030_REG_ALS_CONF, 0x1001);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup als configs\n");

	ret = regmap_update_bits(data->regmap, VEML6030_REG_ALS_PSM,
				 VEML6030_PSM | VEML6030_PSM_EN, 0x03);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup default PSM\n");

	ret = regmap_write(data->regmap, VEML6030_REG_ALS_WH, 0xFFFF);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup high threshold\n");

	ret = regmap_write(data->regmap, VEML6030_REG_ALS_WL, 0x0000);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup low threshold\n");

	ret = veml6030_als_pwr_on(data);
	if (ret)
		return dev_err_probe(dev, ret, "can't poweron als\n");

	ret = devm_add_action_or_reset(dev, veml6030_als_shut_down_action, data);
	if (ret < 0)
		return ret;

	/* Clear stale interrupt status bits if any during start */
	ret = regmap_read(data->regmap, VEML6030_REG_ALS_INT, &val);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "can't clear als interrupt status\n");

	/* Cache currently active measurement parameters */
	data->cur_gain = 3;
	data->cur_resolution = 5376;
	data->cur_integration_time = 3;

	return ret;
}

/*
 * Set ALS gain to 1/8, integration time to 100 ms, ALS and WHITE
 * channel enabled, ALS channel interrupt, PSM enabled,
 * PSM_WAIT = 0.8 s, persistence to 1 x integration time and the
 * threshold interrupt disabled by default. First shutdown the sensor,
 * update registers and then power on the sensor.
 */
static int veml6035_hw_init(struct iio_dev *indio_dev, struct device *dev)
{
	int ret, val;
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = veml6030_als_shut_down(data);
	if (ret)
		return dev_err_probe(dev, ret, "can't shutdown als\n");

	ret = regmap_write(data->regmap, VEML6030_REG_ALS_CONF,
			   VEML6035_SENS | VEML6035_CHAN_EN | VEML6030_ALS_SD);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup als configs\n");

	ret = regmap_update_bits(data->regmap, VEML6030_REG_ALS_PSM,
				 VEML6030_PSM | VEML6030_PSM_EN, 0x03);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup default PSM\n");

	ret = regmap_write(data->regmap, VEML6030_REG_ALS_WH, 0xFFFF);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup high threshold\n");

	ret = regmap_write(data->regmap, VEML6030_REG_ALS_WL, 0x0000);
	if (ret)
		return dev_err_probe(dev, ret, "can't setup low threshold\n");

	ret = veml6030_als_pwr_on(data);
	if (ret)
		return dev_err_probe(dev, ret, "can't poweron als\n");

	ret = devm_add_action_or_reset(dev, veml6030_als_shut_down_action, data);
	if (ret < 0)
		return ret;

	/* Clear stale interrupt status bits if any during start */
	ret = regmap_read(data->regmap, VEML6030_REG_ALS_INT, &val);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "can't clear als interrupt status\n");

	/* Cache currently active measurement parameters */
	data->cur_gain = 5;
	data->cur_resolution = 1024;
	data->cur_integration_time = 3;

	return 0;
}

static int veml6030_probe(struct i2c_client *client)
{
	int ret;
	struct veml6030_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(&client->dev, -EOPNOTSUPP,
				     "i2c adapter doesn't support plain i2c\n");

	regmap = devm_regmap_init_i2c(client, &veml6030_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "can't setup regmap\n");

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;

	ret = devm_regulator_get_enable(&client->dev, "vdd");
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to enable regulator\n");

	data->chip = i2c_get_match_data(client);
	if (!data->chip)
		return -EINVAL;

	indio_dev->name = data->chip->name;
	indio_dev->channels = data->chip->channels;
	indio_dev->num_channels = data->chip->num_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = data->chip->set_info(indio_dev);
	if (ret < 0)
		return ret;

	ret = data->chip->hw_init(indio_dev, &client->dev);
	if (ret < 0)
		return ret;

	ret = devm_iio_triggered_buffer_setup(&client->dev, indio_dev, NULL,
					      veml6030_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "Failed to register triggered buffer");

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int veml6030_runtime_suspend(struct device *dev)
{
	int ret;
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = veml6030_als_shut_down(data);
	if (ret < 0)
		dev_err(&data->client->dev, "can't suspend als %d\n", ret);

	return ret;
}

static int veml6030_runtime_resume(struct device *dev)
{
	int ret;
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct veml6030_data *data = iio_priv(indio_dev);

	ret = veml6030_als_pwr_on(data);
	if (ret < 0)
		dev_err(&data->client->dev, "can't resume als %d\n", ret);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(veml6030_pm_ops, veml6030_runtime_suspend,
				 veml6030_runtime_resume, NULL);

static const struct veml603x_chip veml6030_chip = {
	.name = "veml6030",
	.scale_vals = &veml6030_scale_vals,
	.num_scale_vals = ARRAY_SIZE(veml6030_scale_vals),
	.channels = veml6030_channels,
	.num_channels = ARRAY_SIZE(veml6030_channels),
	.hw_init = veml6030_hw_init,
	.set_info = veml6030_set_info,
	.set_als_gain = veml6030_set_als_gain,
	.get_als_gain = veml6030_get_als_gain,
};

static const struct veml603x_chip veml6035_chip = {
	.name = "veml6035",
	.scale_vals = &veml6035_scale_vals,
	.num_scale_vals = ARRAY_SIZE(veml6035_scale_vals),
	.channels = veml6030_channels,
	.num_channels = ARRAY_SIZE(veml6030_channels),
	.hw_init = veml6035_hw_init,
	.set_info = veml6030_set_info,
	.set_als_gain = veml6035_set_als_gain,
	.get_als_gain = veml6035_get_als_gain,
};

static const struct veml603x_chip veml7700_chip = {
	.name = "veml7700",
	.scale_vals = &veml6030_scale_vals,
	.num_scale_vals = ARRAY_SIZE(veml6030_scale_vals),
	.channels = veml7700_channels,
	.num_channels = ARRAY_SIZE(veml7700_channels),
	.hw_init = veml6030_hw_init,
	.set_info = veml7700_set_info,
	.set_als_gain = veml6030_set_als_gain,
	.get_als_gain = veml6030_get_als_gain,
};

static const struct of_device_id veml6030_of_match[] = {
	{
		.compatible = "vishay,veml6030",
		.data = &veml6030_chip,
	},
	{
		.compatible = "vishay,veml6035",
		.data = &veml6035_chip,
	},
	{
		.compatible = "vishay,veml7700",
		.data = &veml7700_chip,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, veml6030_of_match);

static const struct i2c_device_id veml6030_id[] = {
	{ "veml6030", (kernel_ulong_t)&veml6030_chip},
	{ "veml6035", (kernel_ulong_t)&veml6035_chip},
	{ "veml7700", (kernel_ulong_t)&veml7700_chip},
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml6030_id);

static struct i2c_driver veml6030_driver = {
	.driver = {
		.name = "veml6030",
		.of_match_table = veml6030_of_match,
		.pm = pm_ptr(&veml6030_pm_ops),
	},
	.probe = veml6030_probe,
	.id_table = veml6030_id,
};
module_i2c_driver(veml6030_driver);

MODULE_AUTHOR("Rishi Gupta <gupt21@gmail.com>");
MODULE_DESCRIPTION("VEML6030 Ambient Light Sensor");
MODULE_LICENSE("GPL v2");

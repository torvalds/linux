/*  Copyright (C) 2010 Texas Instruments
    Author: Shubhrajyoti Datta <shubhrajyoti@ti.com>
    Acknowledgement: Jonathan Cameron <jic23@kernel.org> for valuable inputs.

    Support for HMC5883 and HMC5883L by Peter Meerwald <pmeerw@pmeerw.net>.

    Split to multiple files by Josef Gajdusek <atx@atx.name> - 2014

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/delay.h>

#include "hmc5843.h"

/*
 * Range gain settings in (+-)Ga
 * Beware: HMC5843 and HMC5883 have different recommended sensor field
 * ranges; default corresponds to +-1.0 Ga and +-1.3 Ga, respectively
 */
#define HMC5843_RANGE_GAIN_OFFSET		0x05
#define HMC5843_RANGE_GAIN_DEFAULT		0x01
#define HMC5843_RANGE_GAIN_MASK		0xe0

/* Device status */
#define HMC5843_DATA_READY			0x01
#define HMC5843_DATA_OUTPUT_LOCK		0x02

/* Mode register configuration */
#define HMC5843_MODE_CONVERSION_CONTINUOUS	0x00
#define HMC5843_MODE_CONVERSION_SINGLE		0x01
#define HMC5843_MODE_IDLE			0x02
#define HMC5843_MODE_SLEEP			0x03
#define HMC5843_MODE_MASK			0x03

/*
 * HMC5843: Minimum data output rate
 * HMC5883: Typical data output rate
 */
#define HMC5843_RATE_OFFSET			0x02
#define HMC5843_RATE_DEFAULT			0x04
#define HMC5843_RATE_MASK		0x1c

/* Device measurement configuration */
#define HMC5843_MEAS_CONF_NORMAL		0x00
#define HMC5843_MEAS_CONF_POSITIVE_BIAS		0x01
#define HMC5843_MEAS_CONF_NEGATIVE_BIAS		0x02
#define HMC5843_MEAS_CONF_MASK			0x03

/* Scaling factors: 10000000/Gain */
static const int hmc5843_regval_to_nanoscale[] = {
	6173, 7692, 10309, 12821, 18868, 21739, 25641, 35714
};

static const int hmc5883_regval_to_nanoscale[] = {
	7812, 9766, 13021, 16287, 24096, 27701, 32573, 45662
};

static const int hmc5883l_regval_to_nanoscale[] = {
	7299, 9174, 12195, 15152, 22727, 25641, 30303, 43478
};

/*
 * From the datasheet:
 * Value	| HMC5843		| HMC5883/HMC5883L
 *		| Data output rate (Hz)	| Data output rate (Hz)
 * 0		| 0.5			| 0.75
 * 1		| 1			| 1.5
 * 2		| 2			| 3
 * 3		| 5			| 7.5
 * 4		| 10 (default)		| 15
 * 5		| 20			| 30
 * 6		| 50			| 75
 * 7		| Not used		| Not used
 */
static const int hmc5843_regval_to_samp_freq[][2] = {
	{0, 500000}, {1, 0}, {2, 0}, {5, 0}, {10, 0}, {20, 0}, {50, 0}
};

static const int hmc5883_regval_to_samp_freq[][2] = {
	{0, 750000}, {1, 500000}, {3, 0}, {7, 500000}, {15, 0}, {30, 0},
	{75, 0}
};

static const int hmc5983_regval_to_samp_freq[][2] = {
	{0, 750000}, {1, 500000}, {3, 0}, {7, 500000}, {15, 0}, {30, 0},
	{75, 0}, {220, 0}
};

/* Describe chip variants */
struct hmc5843_chip_info {
	const struct iio_chan_spec *channels;
	const int (*regval_to_samp_freq)[2];
	const int n_regval_to_samp_freq;
	const int *regval_to_nanoscale;
	const int n_regval_to_nanoscale;
};

/* The lower two bits contain the current conversion mode */
static s32 hmc5843_set_mode(struct hmc5843_data *data, u8 operating_mode)
{
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_update_bits(data->regmap, HMC5843_MODE_REG,
			HMC5843_MODE_MASK, operating_mode);
	mutex_unlock(&data->lock);

	return ret;
}

static int hmc5843_wait_measurement(struct hmc5843_data *data)
{
	int tries = 150;
	unsigned int val;
	int ret;

	while (tries-- > 0) {
		ret = regmap_read(data->regmap, HMC5843_STATUS_REG, &val);
		if (ret < 0)
			return ret;
		if (val & HMC5843_DATA_READY)
			break;
		msleep(20);
	}

	if (tries < 0) {
		dev_err(data->dev, "data not ready\n");
		return -EIO;
	}

	return 0;
}

/* Return the measurement value from the specified channel */
static int hmc5843_read_measurement(struct hmc5843_data *data,
				    int idx, int *val)
{
	__be16 values[3];
	int ret;

	mutex_lock(&data->lock);
	ret = hmc5843_wait_measurement(data);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	}
	ret = regmap_bulk_read(data->regmap, HMC5843_DATA_OUT_MSB_REGS,
			values, sizeof(values));
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	*val = sign_extend32(be16_to_cpu(values[idx]), 15);
	return IIO_VAL_INT;
}

/*
 * API for setting the measurement configuration to
 * Normal, Positive bias and Negative bias
 *
 * From the datasheet:
 * 0 - Normal measurement configuration (default): In normal measurement
 *     configuration the device follows normal measurement flow. Pins BP
 *     and BN are left floating and high impedance.
 *
 * 1 - Positive bias configuration: In positive bias configuration, a
 *     positive current is forced across the resistive load on pins BP
 *     and BN.
 *
 * 2 - Negative bias configuration. In negative bias configuration, a
 *     negative current is forced across the resistive load on pins BP
 *     and BN.
 *
 */
static int hmc5843_set_meas_conf(struct hmc5843_data *data, u8 meas_conf)
{
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_update_bits(data->regmap, HMC5843_CONFIG_REG_A,
			HMC5843_MEAS_CONF_MASK, meas_conf);
	mutex_unlock(&data->lock);

	return ret;
}

static ssize_t hmc5843_show_measurement_configuration(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct hmc5843_data *data = iio_priv(dev_to_iio_dev(dev));
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, HMC5843_CONFIG_REG_A, &val);
	if (ret)
		return ret;
	val &= HMC5843_MEAS_CONF_MASK;

	return sprintf(buf, "%d\n", val);
}

static ssize_t hmc5843_set_measurement_configuration(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t count)
{
	struct hmc5843_data *data = iio_priv(dev_to_iio_dev(dev));
	unsigned long meas_conf = 0;
	int ret;

	ret = kstrtoul(buf, 10, &meas_conf);
	if (ret)
		return ret;
	if (meas_conf >= HMC5843_MEAS_CONF_MASK)
		return -EINVAL;

	ret = hmc5843_set_meas_conf(data, meas_conf);

	return (ret < 0) ? ret : count;
}

static IIO_DEVICE_ATTR(meas_conf,
			S_IWUSR | S_IRUGO,
			hmc5843_show_measurement_configuration,
			hmc5843_set_measurement_configuration,
			0);

static ssize_t hmc5843_show_samp_freq_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hmc5843_data *data = iio_priv(dev_to_iio_dev(dev));
	size_t len = 0;
	int i;

	for (i = 0; i < data->variant->n_regval_to_samp_freq; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"%d.%d ", data->variant->regval_to_samp_freq[i][0],
			data->variant->regval_to_samp_freq[i][1]);

	/* replace trailing space by newline */
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(hmc5843_show_samp_freq_avail);

static int hmc5843_set_samp_freq(struct hmc5843_data *data, u8 rate)
{
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_update_bits(data->regmap, HMC5843_CONFIG_REG_A,
			HMC5843_RATE_MASK, rate << HMC5843_RATE_OFFSET);
	mutex_unlock(&data->lock);

	return ret;
}

static int hmc5843_get_samp_freq_index(struct hmc5843_data *data,
				   int val, int val2)
{
	int i;

	for (i = 0; i < data->variant->n_regval_to_samp_freq; i++)
		if (val == data->variant->regval_to_samp_freq[i][0] &&
			val2 == data->variant->regval_to_samp_freq[i][1])
			return i;

	return -EINVAL;
}

static int hmc5843_set_range_gain(struct hmc5843_data *data, u8 range)
{
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_update_bits(data->regmap, HMC5843_CONFIG_REG_B,
			HMC5843_RANGE_GAIN_MASK,
			range << HMC5843_RANGE_GAIN_OFFSET);
	mutex_unlock(&data->lock);

	return ret;
}

static ssize_t hmc5843_show_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hmc5843_data *data = iio_priv(dev_to_iio_dev(dev));

	size_t len = 0;
	int i;

	for (i = 0; i < data->variant->n_regval_to_nanoscale; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"0.%09d ", data->variant->regval_to_nanoscale[i]);

	/* replace trailing space by newline */
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEVICE_ATTR(scale_available, S_IRUGO,
	hmc5843_show_scale_avail, NULL, 0);

static int hmc5843_get_scale_index(struct hmc5843_data *data, int val, int val2)
{
	int i;

	if (val != 0)
		return -EINVAL;

	for (i = 0; i < data->variant->n_regval_to_nanoscale; i++)
		if (val2 == data->variant->regval_to_nanoscale[i])
			return i;

	return -EINVAL;
}

static int hmc5843_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct hmc5843_data *data = iio_priv(indio_dev);
	unsigned int rval;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return hmc5843_read_measurement(data, chan->scan_index, val);
	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(data->regmap, HMC5843_CONFIG_REG_B, &rval);
		if (ret < 0)
			return ret;
		rval >>= HMC5843_RANGE_GAIN_OFFSET;
		*val = 0;
		*val2 = data->variant->regval_to_nanoscale[rval];
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_read(data->regmap, HMC5843_CONFIG_REG_A, &rval);
		if (ret < 0)
			return ret;
		rval >>= HMC5843_RATE_OFFSET;
		*val = data->variant->regval_to_samp_freq[rval][0];
		*val2 = data->variant->regval_to_samp_freq[rval][1];
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int hmc5843_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct hmc5843_data *data = iio_priv(indio_dev);
	int rate, range;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		rate = hmc5843_get_samp_freq_index(data, val, val2);
		if (rate < 0)
			return -EINVAL;

		return hmc5843_set_samp_freq(data, rate);
	case IIO_CHAN_INFO_SCALE:
		range = hmc5843_get_scale_index(data, val, val2);
		if (range < 0)
			return -EINVAL;

		return hmc5843_set_range_gain(data, range);
	default:
		return -EINVAL;
	}
}

static int hmc5843_write_raw_get_fmt(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static irqreturn_t hmc5843_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct hmc5843_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = hmc5843_wait_measurement(data);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		goto done;
	}

	ret = regmap_bulk_read(data->regmap, HMC5843_DATA_OUT_MSB_REGS,
			data->buffer, 3 * sizeof(__be16));

	mutex_unlock(&data->lock);
	if (ret < 0)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
		iio_get_time_ns());

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

#define HMC5843_CHANNEL(axis, idx)					\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.scan_index = idx,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec hmc5843_channels[] = {
	HMC5843_CHANNEL(X, 0),
	HMC5843_CHANNEL(Y, 1),
	HMC5843_CHANNEL(Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

/* Beware: Y and Z are exchanged on HMC5883 and 5983 */
static const struct iio_chan_spec hmc5883_channels[] = {
	HMC5843_CHANNEL(X, 0),
	HMC5843_CHANNEL(Z, 1),
	HMC5843_CHANNEL(Y, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static struct attribute *hmc5843_attributes[] = {
	&iio_dev_attr_meas_conf.dev_attr.attr,
	&iio_dev_attr_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group hmc5843_group = {
	.attrs = hmc5843_attributes,
};

static const struct hmc5843_chip_info hmc5843_chip_info_tbl[] = {
	[HMC5843_ID] = {
		.channels = hmc5843_channels,
		.regval_to_samp_freq = hmc5843_regval_to_samp_freq,
		.n_regval_to_samp_freq =
				ARRAY_SIZE(hmc5843_regval_to_samp_freq),
		.regval_to_nanoscale = hmc5843_regval_to_nanoscale,
		.n_regval_to_nanoscale =
				ARRAY_SIZE(hmc5843_regval_to_nanoscale),
	},
	[HMC5883_ID] = {
		.channels = hmc5883_channels,
		.regval_to_samp_freq = hmc5883_regval_to_samp_freq,
		.n_regval_to_samp_freq =
				ARRAY_SIZE(hmc5883_regval_to_samp_freq),
		.regval_to_nanoscale = hmc5883_regval_to_nanoscale,
		.n_regval_to_nanoscale =
				ARRAY_SIZE(hmc5883_regval_to_nanoscale),
	},
	[HMC5883L_ID] = {
		.channels = hmc5883_channels,
		.regval_to_samp_freq = hmc5883_regval_to_samp_freq,
		.n_regval_to_samp_freq =
				ARRAY_SIZE(hmc5883_regval_to_samp_freq),
		.regval_to_nanoscale = hmc5883l_regval_to_nanoscale,
		.n_regval_to_nanoscale =
				ARRAY_SIZE(hmc5883l_regval_to_nanoscale),
	},
	[HMC5983_ID] = {
		.channels = hmc5883_channels,
		.regval_to_samp_freq = hmc5983_regval_to_samp_freq,
		.n_regval_to_samp_freq =
				ARRAY_SIZE(hmc5983_regval_to_samp_freq),
		.regval_to_nanoscale = hmc5883l_regval_to_nanoscale,
		.n_regval_to_nanoscale =
				ARRAY_SIZE(hmc5883l_regval_to_nanoscale),
	}
};

static int hmc5843_init(struct hmc5843_data *data)
{
	int ret;
	u8 id[3];

	ret = regmap_bulk_read(data->regmap, HMC5843_ID_REG,
			id, ARRAY_SIZE(id));
	if (ret < 0)
		return ret;
	if (id[0] != 'H' || id[1] != '4' || id[2] != '3') {
		dev_err(data->dev, "no HMC5843/5883/5883L/5983 sensor\n");
		return -ENODEV;
	}

	ret = hmc5843_set_meas_conf(data, HMC5843_MEAS_CONF_NORMAL);
	if (ret < 0)
		return ret;
	ret = hmc5843_set_samp_freq(data, HMC5843_RATE_DEFAULT);
	if (ret < 0)
		return ret;
	ret = hmc5843_set_range_gain(data, HMC5843_RANGE_GAIN_DEFAULT);
	if (ret < 0)
		return ret;
	return hmc5843_set_mode(data, HMC5843_MODE_CONVERSION_CONTINUOUS);
}

static const struct iio_info hmc5843_info = {
	.attrs = &hmc5843_group,
	.read_raw = &hmc5843_read_raw,
	.write_raw = &hmc5843_write_raw,
	.write_raw_get_fmt = &hmc5843_write_raw_get_fmt,
	.driver_module = THIS_MODULE,
};

static const unsigned long hmc5843_scan_masks[] = {0x7, 0};


int hmc5843_common_suspend(struct device *dev)
{
	return hmc5843_set_mode(iio_priv(dev_get_drvdata(dev)),
			HMC5843_MODE_CONVERSION_CONTINUOUS);
}
EXPORT_SYMBOL(hmc5843_common_suspend);

int hmc5843_common_resume(struct device *dev)
{
	return hmc5843_set_mode(iio_priv(dev_get_drvdata(dev)),
			HMC5843_MODE_SLEEP);
}
EXPORT_SYMBOL(hmc5843_common_resume);

int hmc5843_common_probe(struct device *dev, struct regmap *regmap,
		enum hmc5843_ids id)
{
	struct hmc5843_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, indio_dev);

	/* default settings at probe */
	data = iio_priv(indio_dev);
	data->dev = dev;
	data->regmap = regmap;
	data->variant = &hmc5843_chip_info_tbl[id];
	mutex_init(&data->lock);

	indio_dev->dev.parent = dev;
	indio_dev->info = &hmc5843_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = data->variant->channels;
	indio_dev->num_channels = 4;
	indio_dev->available_scan_masks = hmc5843_scan_masks;

	ret = hmc5843_init(data);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		hmc5843_trigger_handler, NULL);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto buffer_cleanup;

	return 0;

buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
	return ret;
}
EXPORT_SYMBOL(hmc5843_common_probe);

int hmc5843_common_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	/*  sleep mode to save power */
	hmc5843_set_mode(iio_priv(indio_dev), HMC5843_MODE_SLEEP);

	return 0;
}
EXPORT_SYMBOL(hmc5843_common_remove);

MODULE_AUTHOR("Shubhrajyoti Datta <shubhrajyoti@ti.com>");
MODULE_DESCRIPTION("HMC5843/5883/5883L/5983 core driver");
MODULE_LICENSE("GPL");

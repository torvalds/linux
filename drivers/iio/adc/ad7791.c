// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD7787/AD7788/AD7789/AD7790/AD7791 SPI ADC driver
 *
 * Copyright 2012 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/adc/ad_sigma_delta.h>

#include <linux/platform_data/ad7791.h>

#define AD7791_REG_COMM			0x0 /* For writes */
#define AD7791_REG_STATUS		0x0 /* For reads */
#define AD7791_REG_MODE			0x1
#define AD7791_REG_FILTER		0x2
#define AD7791_REG_DATA			0x3

#define AD7791_MODE_CONTINUOUS		0x00
#define AD7791_MODE_SINGLE		0x02
#define AD7791_MODE_POWERDOWN		0x03

#define AD7791_CH_AIN1P_AIN1N		0x00
#define AD7791_CH_AIN2			0x01
#define AD7791_CH_AIN1N_AIN1N		0x02
#define AD7791_CH_AVDD_MONITOR		0x03

#define AD7791_FILTER_CLK_DIV_1		(0x0 << 4)
#define AD7791_FILTER_CLK_DIV_2		(0x1 << 4)
#define AD7791_FILTER_CLK_DIV_4		(0x2 << 4)
#define AD7791_FILTER_CLK_DIV_8		(0x3 << 4)
#define AD7791_FILTER_CLK_MASK		(0x3 << 4)
#define AD7791_FILTER_RATE_120		0x0
#define AD7791_FILTER_RATE_100		0x1
#define AD7791_FILTER_RATE_33_3		0x2
#define AD7791_FILTER_RATE_20		0x3
#define AD7791_FILTER_RATE_16_6		0x4
#define AD7791_FILTER_RATE_16_7		0x5
#define AD7791_FILTER_RATE_13_3		0x6
#define AD7791_FILTER_RATE_9_5		0x7
#define AD7791_FILTER_RATE_MASK		0x7

#define AD7791_MODE_BUFFER		BIT(1)
#define AD7791_MODE_UNIPOLAR		BIT(2)
#define AD7791_MODE_BURNOUT		BIT(3)
#define AD7791_MODE_SEL_MASK		(0x3 << 6)
#define AD7791_MODE_SEL(x)		((x) << 6)

#define __AD7991_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
	_storagebits, _shift, _extend_name, _type, _mask_all) \
	{ \
		.type = (_type), \
		.differential = (_channel2 == -1 ? 0 : 1), \
		.indexed = 1, \
		.channel = (_channel1), \
		.channel2 = (_channel2), \
		.address = (_address), \
		.extend_name = (_extend_name), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_OFFSET), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = _mask_all, \
		.scan_index = (_si), \
		.scan_type = { \
			.sign = 'u', \
			.realbits = (_bits), \
			.storagebits = (_storagebits), \
			.shift = (_shift), \
			.endianness = IIO_BE, \
		}, \
	}

#define AD7991_SHORTED_CHANNEL(_si, _channel, _address, _bits, \
	_storagebits, _shift) \
	__AD7991_CHANNEL(_si, _channel, _channel, _address, _bits, \
		_storagebits, _shift, "shorted", IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7991_CHANNEL(_si, _channel, _address, _bits, \
	_storagebits, _shift) \
	__AD7991_CHANNEL(_si, _channel, -1, _address, _bits, \
		_storagebits, _shift, NULL, IIO_VOLTAGE, \
		 BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7991_DIFF_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
	_storagebits, _shift) \
	__AD7991_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
		_storagebits, _shift, NULL, IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7991_SUPPLY_CHANNEL(_si, _channel, _address, _bits, _storagebits, \
	_shift) \
	__AD7991_CHANNEL(_si, _channel, -1, _address, _bits, \
		_storagebits, _shift, "supply", IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define DECLARE_AD7787_CHANNELS(name, bits, storagebits) \
const struct iio_chan_spec name[] = { \
	AD7991_DIFF_CHANNEL(0, 0, 0, AD7791_CH_AIN1P_AIN1N, \
		(bits), (storagebits), 0), \
	AD7991_CHANNEL(1, 1, AD7791_CH_AIN2, (bits), (storagebits), 0), \
	AD7991_SHORTED_CHANNEL(2, 0, AD7791_CH_AIN1N_AIN1N, \
		(bits), (storagebits), 0), \
	AD7991_SUPPLY_CHANNEL(3, 2, AD7791_CH_AVDD_MONITOR,  \
		(bits), (storagebits), 0), \
	IIO_CHAN_SOFT_TIMESTAMP(4), \
}

#define DECLARE_AD7791_CHANNELS(name, bits, storagebits) \
const struct iio_chan_spec name[] = { \
	AD7991_DIFF_CHANNEL(0, 0, 0, AD7791_CH_AIN1P_AIN1N, \
		(bits), (storagebits), 0), \
	AD7991_SHORTED_CHANNEL(1, 0, AD7791_CH_AIN1N_AIN1N, \
		(bits), (storagebits), 0), \
	AD7991_SUPPLY_CHANNEL(2, 1, AD7791_CH_AVDD_MONITOR, \
		(bits), (storagebits), 0), \
	IIO_CHAN_SOFT_TIMESTAMP(3), \
}

static DECLARE_AD7787_CHANNELS(ad7787_channels, 24, 32);
static DECLARE_AD7791_CHANNELS(ad7790_channels, 16, 16);
static DECLARE_AD7791_CHANNELS(ad7791_channels, 24, 32);

enum {
	AD7787,
	AD7788,
	AD7789,
	AD7790,
	AD7791,
};

enum ad7791_chip_info_flags {
	AD7791_FLAG_HAS_FILTER		= (1 << 0),
	AD7791_FLAG_HAS_BUFFER		= (1 << 1),
	AD7791_FLAG_HAS_UNIPOLAR	= (1 << 2),
	AD7791_FLAG_HAS_BURNOUT		= (1 << 3),
};

struct ad7791_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	enum ad7791_chip_info_flags flags;
};

static const struct ad7791_chip_info ad7791_chip_infos[] = {
	[AD7787] = {
		.channels = ad7787_channels,
		.num_channels = ARRAY_SIZE(ad7787_channels),
		.flags = AD7791_FLAG_HAS_FILTER | AD7791_FLAG_HAS_BUFFER |
			AD7791_FLAG_HAS_UNIPOLAR | AD7791_FLAG_HAS_BURNOUT,
	},
	[AD7788] = {
		.channels = ad7790_channels,
		.num_channels = ARRAY_SIZE(ad7790_channels),
		.flags = AD7791_FLAG_HAS_UNIPOLAR,
	},
	[AD7789] = {
		.channels = ad7791_channels,
		.num_channels = ARRAY_SIZE(ad7791_channels),
		.flags = AD7791_FLAG_HAS_UNIPOLAR,
	},
	[AD7790] = {
		.channels = ad7790_channels,
		.num_channels = ARRAY_SIZE(ad7790_channels),
		.flags = AD7791_FLAG_HAS_FILTER | AD7791_FLAG_HAS_BUFFER |
			AD7791_FLAG_HAS_BURNOUT,
	},
	[AD7791] = {
		.channels = ad7791_channels,
		.num_channels = ARRAY_SIZE(ad7791_channels),
		.flags = AD7791_FLAG_HAS_FILTER | AD7791_FLAG_HAS_BUFFER |
			AD7791_FLAG_HAS_UNIPOLAR | AD7791_FLAG_HAS_BURNOUT,
	},
};

struct ad7791_state {
	struct ad_sigma_delta sd;
	uint8_t mode;
	uint8_t filter;

	struct regulator *reg;
	const struct ad7791_chip_info *info;
};

static const int ad7791_sample_freq_avail[8][2] = {
	[AD7791_FILTER_RATE_120] =  { 120, 0 },
	[AD7791_FILTER_RATE_100] =  { 100, 0 },
	[AD7791_FILTER_RATE_33_3] = { 33,  300000 },
	[AD7791_FILTER_RATE_20] =   { 20,  0 },
	[AD7791_FILTER_RATE_16_6] = { 16,  600000 },
	[AD7791_FILTER_RATE_16_7] = { 16,  700000 },
	[AD7791_FILTER_RATE_13_3] = { 13,  300000 },
	[AD7791_FILTER_RATE_9_5] =  { 9,   500000 },
};

static struct ad7791_state *ad_sigma_delta_to_ad7791(struct ad_sigma_delta *sd)
{
	return container_of(sd, struct ad7791_state, sd);
}

static int ad7791_set_channel(struct ad_sigma_delta *sd, unsigned int channel)
{
	ad_sd_set_comm(sd, channel);

	return 0;
}

static int ad7791_set_mode(struct ad_sigma_delta *sd,
	enum ad_sigma_delta_mode mode)
{
	struct ad7791_state *st = ad_sigma_delta_to_ad7791(sd);

	switch (mode) {
	case AD_SD_MODE_CONTINUOUS:
		mode = AD7791_MODE_CONTINUOUS;
		break;
	case AD_SD_MODE_SINGLE:
		mode = AD7791_MODE_SINGLE;
		break;
	case AD_SD_MODE_IDLE:
	case AD_SD_MODE_POWERDOWN:
		mode = AD7791_MODE_POWERDOWN;
		break;
	}

	st->mode &= ~AD7791_MODE_SEL_MASK;
	st->mode |= AD7791_MODE_SEL(mode);

	return ad_sd_write_reg(sd, AD7791_REG_MODE, sizeof(st->mode), st->mode);
}

static const struct ad_sigma_delta_info ad7791_sigma_delta_info = {
	.set_channel = ad7791_set_channel,
	.set_mode = ad7791_set_mode,
	.has_registers = true,
	.addr_shift = 4,
	.read_mask = BIT(3),
	.irq_flags = IRQF_TRIGGER_LOW,
};

static int ad7791_read_raw(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *val, int *val2, long info)
{
	struct ad7791_state *st = iio_priv(indio_dev);
	bool unipolar = !!(st->mode & AD7791_MODE_UNIPOLAR);
	unsigned int rate;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return ad_sigma_delta_single_conversion(indio_dev, chan, val);
	case IIO_CHAN_INFO_OFFSET:
		/**
		 * Unipolar: 0 to VREF
		 * Bipolar -VREF to VREF
		 **/
		if (unipolar)
			*val = 0;
		else
			*val = -(1 << (chan->scan_type.realbits - 1));
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* The monitor channel uses an internal reference. */
		if (chan->address == AD7791_CH_AVDD_MONITOR) {
			/*
			 * The signal is attenuated by a factor of 5 and
			 * compared against a 1.17V internal reference.
			 */
			*val = 1170 * 5;
		} else {
			int voltage_uv;

			voltage_uv = regulator_get_voltage(st->reg);
			if (voltage_uv < 0)
				return voltage_uv;

			*val = voltage_uv / 1000;
		}
		if (unipolar)
			*val2 = chan->scan_type.realbits;
		else
			*val2 = chan->scan_type.realbits - 1;

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_SAMP_FREQ:
		rate = st->filter & AD7791_FILTER_RATE_MASK;
		*val = ad7791_sample_freq_avail[rate][0];
		*val2 = ad7791_sample_freq_avail[rate][1];
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int ad7791_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct ad7791_state *st = iio_priv(indio_dev);
	int ret, i;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		for (i = 0; i < ARRAY_SIZE(ad7791_sample_freq_avail); i++) {
			if (ad7791_sample_freq_avail[i][0] == val &&
			    ad7791_sample_freq_avail[i][1] == val2)
				break;
		}

		if (i == ARRAY_SIZE(ad7791_sample_freq_avail)) {
			ret = -EINVAL;
			break;
		}

		st->filter &= ~AD7791_FILTER_RATE_MASK;
		st->filter |= i;
		ad_sd_write_reg(&st->sd, AD7791_REG_FILTER,
				sizeof(st->filter),
				st->filter);
		break;
	default:
		ret = -EINVAL;
	}

	iio_device_release_direct_mode(indio_dev);
	return ret;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("120 100 33.3 20 16.7 16.6 13.3 9.5");

static struct attribute *ad7791_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7791_attribute_group = {
	.attrs = ad7791_attributes,
};

static const struct iio_info ad7791_info = {
	.read_raw = &ad7791_read_raw,
	.write_raw = &ad7791_write_raw,
	.attrs = &ad7791_attribute_group,
	.validate_trigger = ad_sd_validate_trigger,
};

static const struct iio_info ad7791_no_filter_info = {
	.read_raw = &ad7791_read_raw,
	.write_raw = &ad7791_write_raw,
	.validate_trigger = ad_sd_validate_trigger,
};

static int ad7791_setup(struct ad7791_state *st,
			struct ad7791_platform_data *pdata)
{
	/* Set to poweron-reset default values */
	st->mode = AD7791_MODE_BUFFER;
	st->filter = AD7791_FILTER_RATE_16_6;

	if (!pdata)
		return 0;

	if ((st->info->flags & AD7791_FLAG_HAS_BUFFER) && !pdata->buffered)
		st->mode &= ~AD7791_MODE_BUFFER;

	if ((st->info->flags & AD7791_FLAG_HAS_BURNOUT) &&
		pdata->burnout_current)
		st->mode |= AD7791_MODE_BURNOUT;

	if ((st->info->flags & AD7791_FLAG_HAS_UNIPOLAR) && pdata->unipolar)
		st->mode |= AD7791_MODE_UNIPOLAR;

	return ad_sd_write_reg(&st->sd, AD7791_REG_MODE, sizeof(st->mode),
		st->mode);
}

static int ad7791_probe(struct spi_device *spi)
{
	struct ad7791_platform_data *pdata = spi->dev.platform_data;
	struct iio_dev *indio_dev;
	struct ad7791_state *st;
	int ret;

	if (!spi->irq) {
		dev_err(&spi->dev, "Missing IRQ.\n");
		return -ENXIO;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = devm_regulator_get(&spi->dev, "refin");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	ret = regulator_enable(st->reg);
	if (ret)
		return ret;

	st->info = &ad7791_chip_infos[spi_get_device_id(spi)->driver_data];
	ad_sd_init(&st->sd, indio_dev, spi, &ad7791_sigma_delta_info);

	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->dev.of_node = spi->dev.of_node;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->info->channels;
	indio_dev->num_channels = st->info->num_channels;
	if (st->info->flags & AD7791_FLAG_HAS_FILTER)
		indio_dev->info = &ad7791_info;
	else
		indio_dev->info = &ad7791_no_filter_info;

	ret = ad_sd_setup_buffer_and_trigger(indio_dev);
	if (ret)
		goto error_disable_reg;

	ret = ad7791_setup(st, pdata);
	if (ret)
		goto error_remove_trigger;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_remove_trigger;

	return 0;

error_remove_trigger:
	ad_sd_cleanup_buffer_and_trigger(indio_dev);
error_disable_reg:
	regulator_disable(st->reg);

	return ret;
}

static int ad7791_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7791_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	ad_sd_cleanup_buffer_and_trigger(indio_dev);

	regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad7791_spi_ids[] = {
	{ "ad7787", AD7787 },
	{ "ad7788", AD7788 },
	{ "ad7789", AD7789 },
	{ "ad7790", AD7790 },
	{ "ad7791", AD7791 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad7791_spi_ids);

static struct spi_driver ad7791_driver = {
	.driver = {
		.name	= "ad7791",
	},
	.probe		= ad7791_probe,
	.remove		= ad7791_remove,
	.id_table	= ad7791_spi_ids,
};
module_spi_driver(ad7791_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD7787/AD7788/AD7789/AD7790/AD7791 ADC driver");
MODULE_LICENSE("GPL v2");

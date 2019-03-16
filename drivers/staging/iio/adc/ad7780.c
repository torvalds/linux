// SPDX-License-Identifier: GPL-2.0
/*
 * AD7170/AD7171 and AD7780/AD7781 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
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
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/bits.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/adc/ad_sigma_delta.h>

#define AD7780_RDY		BIT(7)
#define AD7780_FILTER		BIT(6)
#define AD7780_ERR		BIT(5)
#define AD7780_ID1		BIT(4)
#define AD7780_ID0		BIT(3)
#define AD7780_GAIN		BIT(2)

#define AD7170_ID		0
#define AD7171_ID		1
#define AD7780_ID		1
#define AD7781_ID		0

#define AD7780_ID_MASK		(AD7780_ID0 | AD7780_ID1)

#define AD7780_PATTERN_GOOD	1
#define AD7780_PATTERN_MASK	GENMASK(1, 0)

#define AD7170_PATTERN_GOOD	5
#define AD7170_PATTERN_MASK	GENMASK(2, 0)

#define AD7780_GAIN_MIDPOINT	64
#define AD7780_FILTER_MIDPOINT	13350

static const unsigned int ad778x_gain[2]      = { 1, 128 };
static const unsigned int ad778x_odr_avail[2] = { 10000, 16700 };

struct ad7780_chip_info {
	struct iio_chan_spec	channel;
	unsigned int		pattern_mask;
	unsigned int		pattern;
	bool			is_ad778x;
};

struct ad7780_state {
	const struct ad7780_chip_info	*chip_info;
	struct regulator		*reg;
	struct gpio_desc		*powerdown_gpio;
	struct gpio_desc		*gain_gpio;
	struct gpio_desc		*filter_gpio;
	unsigned int			gain;
	unsigned int			odr;
	unsigned int			int_vref_mv;

	struct ad_sigma_delta sd;
};

enum ad7780_supported_device_ids {
	ID_AD7170,
	ID_AD7171,
	ID_AD7780,
	ID_AD7781,
};

static struct ad7780_state *ad_sigma_delta_to_ad7780(struct ad_sigma_delta *sd)
{
	return container_of(sd, struct ad7780_state, sd);
}

static int ad7780_set_mode(struct ad_sigma_delta *sigma_delta,
			   enum ad_sigma_delta_mode mode)
{
	struct ad7780_state *st = ad_sigma_delta_to_ad7780(sigma_delta);
	unsigned int val;

	switch (mode) {
	case AD_SD_MODE_SINGLE:
	case AD_SD_MODE_CONTINUOUS:
		val = 1;
		break;
	default:
		val = 0;
		break;
	}

	gpiod_set_value(st->powerdown_gpio, val);

	return 0;
}

static int ad7780_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad7780_state *st = iio_priv(indio_dev);
	int voltage_uv;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		return ad_sigma_delta_single_conversion(indio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		voltage_uv = regulator_get_voltage(st->reg);
		if (voltage_uv < 0)
			return voltage_uv;
		voltage_uv /= 1000;
		*val = voltage_uv * st->gain;
		*val2 = chan->scan_type.realbits - 1;
		st->int_vref_mv = voltage_uv;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		*val = -(1 << (chan->scan_type.realbits - 1));
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->odr;
		return IIO_VAL_INT;
	default:
		break;
	}

	return -EINVAL;
}

static int ad7780_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long m)
{
	struct ad7780_state *st = iio_priv(indio_dev);
	const struct ad7780_chip_info *chip_info = st->chip_info;
	unsigned long long vref;
	unsigned int full_scale, gain;

	if (!chip_info->is_ad778x)
		return -EINVAL;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		vref = st->int_vref_mv * 1000000LL;
		full_scale = 1 << (chip_info->channel.scan_type.realbits - 1);
		gain = DIV_ROUND_CLOSEST_ULL(vref, full_scale);
		gain = DIV_ROUND_CLOSEST(gain, val2);
		st->gain = gain;
		if (gain < AD7780_GAIN_MIDPOINT)
			gain = 0;
		else
			gain = 1;
		gpiod_set_value(st->gain_gpio, gain);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (1000*val + val2/1000 < AD7780_FILTER_MIDPOINT)
			val = 0;
		else
			val = 1;
		st->odr = ad778x_odr_avail[val];
		gpiod_set_value(st->filter_gpio, val);
		break;
	default:
		break;
	}

	return 0;
}

static int ad7780_postprocess_sample(struct ad_sigma_delta *sigma_delta,
				     unsigned int raw_sample)
{
	struct ad7780_state *st = ad_sigma_delta_to_ad7780(sigma_delta);
	const struct ad7780_chip_info *chip_info = st->chip_info;

	if ((raw_sample & AD7780_ERR) ||
	    ((raw_sample & chip_info->pattern_mask) != chip_info->pattern))
		return -EIO;

	if (chip_info->is_ad778x) {
		st->gain = ad778x_gain[raw_sample & AD7780_GAIN];
		st->odr = ad778x_odr_avail[raw_sample & AD7780_FILTER];
	}

	return 0;
}

static const struct ad_sigma_delta_info ad7780_sigma_delta_info = {
	.set_mode = ad7780_set_mode,
	.postprocess_sample = ad7780_postprocess_sample,
	.has_registers = false,
};

#define AD7780_CHANNEL(bits, wordsize) \
	AD_SD_CHANNEL(1, 0, 0, bits, 32, wordsize - bits)
#define AD7170_CHANNEL(bits, wordsize) \
	AD_SD_CHANNEL_NO_SAMP_FREQ(1, 0, 0, bits, 32, wordsize - bits)

static const struct ad7780_chip_info ad7780_chip_info_tbl[] = {
	[ID_AD7170] = {
		.channel = AD7170_CHANNEL(12, 24),
		.pattern = AD7170_PATTERN_GOOD,
		.pattern_mask = AD7170_PATTERN_MASK,
		.is_ad778x = false,
	},
	[ID_AD7171] = {
		.channel = AD7170_CHANNEL(16, 24),
		.pattern = AD7170_PATTERN_GOOD,
		.pattern_mask = AD7170_PATTERN_MASK,
		.is_ad778x = false,
	},
	[ID_AD7780] = {
		.channel = AD7780_CHANNEL(24, 32),
		.pattern = AD7780_PATTERN_GOOD,
		.pattern_mask = AD7780_PATTERN_MASK,
		.is_ad778x = true,
	},
	[ID_AD7781] = {
		.channel = AD7780_CHANNEL(20, 32),
		.pattern = AD7780_PATTERN_GOOD,
		.pattern_mask = AD7780_PATTERN_MASK,
		.is_ad778x = true,
	},
};

static const struct iio_info ad7780_info = {
	.read_raw = ad7780_read_raw,
	.write_raw = ad7780_write_raw,
};

static int ad7780_init_gpios(struct device *dev, struct ad7780_state *st)
{
	int ret;

	st->powerdown_gpio = devm_gpiod_get_optional(dev,
						     "powerdown",
						     GPIOD_OUT_LOW);
	if (IS_ERR(st->powerdown_gpio)) {
		ret = PTR_ERR(st->powerdown_gpio);
		dev_err(dev, "Failed to request powerdown GPIO: %d\n", ret);
		return ret;
	}

	if (!st->chip_info->is_ad778x)
		return 0;


	st->gain_gpio = devm_gpiod_get_optional(dev,
						"adi,gain",
						GPIOD_OUT_HIGH);
	if (IS_ERR(st->gain_gpio)) {
		ret = PTR_ERR(st->gain_gpio);
		dev_err(dev, "Failed to request gain GPIO: %d\n", ret);
		return ret;
	}

	st->filter_gpio = devm_gpiod_get_optional(dev,
						  "adi,filter",
						  GPIOD_OUT_HIGH);
	if (IS_ERR(st->filter_gpio)) {
		ret = PTR_ERR(st->filter_gpio);
		dev_err(dev, "Failed to request filter GPIO: %d\n", ret);
		return ret;
	}

	return 0;
}

static int ad7780_probe(struct spi_device *spi)
{
	struct ad7780_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->gain = 1;

	ad_sd_init(&st->sd, indio_dev, spi, &ad7780_sigma_delta_info);

	st->chip_info =
		&ad7780_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;
	indio_dev->info = &ad7780_info;

	ret = ad7780_init_gpios(&spi->dev, st);
	if (ret)
		goto error_cleanup_buffer_and_trigger;

	st->reg = devm_regulator_get(&spi->dev, "avdd");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	ret = regulator_enable(st->reg);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable specified AVdd supply\n");
		return ret;
	}

	ret = ad_sd_setup_buffer_and_trigger(indio_dev);
	if (ret)
		goto error_disable_reg;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_buffer_and_trigger;

	return 0;

error_cleanup_buffer_and_trigger:
	ad_sd_cleanup_buffer_and_trigger(indio_dev);
error_disable_reg:
	regulator_disable(st->reg);

	return ret;
}

static int ad7780_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7780_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	ad_sd_cleanup_buffer_and_trigger(indio_dev);

	regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad7780_id[] = {
	{"ad7170", ID_AD7170},
	{"ad7171", ID_AD7171},
	{"ad7780", ID_AD7780},
	{"ad7781", ID_AD7781},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7780_id);

static struct spi_driver ad7780_driver = {
	.driver = {
		.name	= "ad7780",
	},
	.probe		= ad7780_probe,
	.remove		= ad7780_remove,
	.id_table	= ad7780_id,
};
module_spi_driver(ad7780_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7780 and similar ADCs");
MODULE_LICENSE("GPL v2");

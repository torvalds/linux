/*
 * AD7170/AD7171 and AD7780/AD7781 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
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
#include <linux/gpio.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/adc/ad_sigma_delta.h>

#include "ad7780.h"

#define AD7780_RDY	(1 << 7)
#define AD7780_FILTER	(1 << 6)
#define AD7780_ERR	(1 << 5)
#define AD7780_ID1	(1 << 4)
#define AD7780_ID0	(1 << 3)
#define AD7780_GAIN	(1 << 2)
#define AD7780_PAT1	(1 << 1)
#define AD7780_PAT0	(1 << 0)

struct ad7780_chip_info {
	struct iio_chan_spec	channel;
	unsigned int		pattern_mask;
	unsigned int		pattern;
};

struct ad7780_state {
	const struct ad7780_chip_info	*chip_info;
	struct regulator		*reg;
	int				powerdown_gpio;
	unsigned int	gain;
	u16				int_vref_mv;

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
	unsigned val;

	switch (mode) {
	case AD_SD_MODE_SINGLE:
	case AD_SD_MODE_CONTINUOUS:
		val = 1;
		break;
	default:
		val = 0;
		break;
	}

	if (gpio_is_valid(st->powerdown_gpio))
		gpio_set_value(st->powerdown_gpio, val);

	return 0;
}

static int ad7780_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad7780_state *st = iio_priv(indio_dev);
	unsigned long scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		return ad_sigma_delta_single_conversion(indio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->int_vref_mv * 100000 * st->gain)
			>> (chan->scan_type.realbits - 1);
		*val =  scale_uv / 100000;
		*val2 = (scale_uv % 100000) * 10;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val -= (1 << (chan->scan_type.realbits - 1));
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad7780_postprocess_sample(struct ad_sigma_delta *sigma_delta,
	unsigned int raw_sample)
{
	struct ad7780_state *st = ad_sigma_delta_to_ad7780(sigma_delta);
	const struct ad7780_chip_info *chip_info = st->chip_info;

	if ((raw_sample & AD7780_ERR) ||
		((raw_sample & chip_info->pattern_mask) != chip_info->pattern))
		return -EIO;

	if (raw_sample & AD7780_GAIN)
		st->gain = 1;
	else
		st->gain = 128;

	return 0;
}

static const struct ad_sigma_delta_info ad7780_sigma_delta_info = {
	.set_mode = ad7780_set_mode,
	.postprocess_sample = ad7780_postprocess_sample,
	.has_registers = false,
};

#define AD7780_CHANNEL(bits, wordsize) \
	AD_SD_CHANNEL(1, 0, 0, bits, 32, wordsize - bits)

static const struct ad7780_chip_info ad7780_chip_info_tbl[] = {
	[ID_AD7170] = {
		.channel = AD7780_CHANNEL(12, 24),
		.pattern = 0x5,
		.pattern_mask = 0x7,
	},
	[ID_AD7171] = {
		.channel = AD7780_CHANNEL(16, 24),
		.pattern = 0x5,
		.pattern_mask = 0x7,
	},
	[ID_AD7780] = {
		.channel = AD7780_CHANNEL(24, 32),
		.pattern = 0x1,
		.pattern_mask = 0x3,
	},
	[ID_AD7781] = {
		.channel = AD7780_CHANNEL(20, 32),
		.pattern = 0x1,
		.pattern_mask = 0x3,
	},
};

static const struct iio_info ad7780_info = {
	.read_raw = &ad7780_read_raw,
	.driver_module = THIS_MODULE,
};

static int ad7780_probe(struct spi_device *spi)
{
	struct ad7780_platform_data *pdata = spi->dev.platform_data;
	struct ad7780_state *st;
	struct iio_dev *indio_dev;
	int ret, voltage_uv = 0;

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->gain = 1;

	ad_sd_init(&st->sd, indio_dev, spi, &ad7780_sigma_delta_info);

	st->reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(st->reg);
	}

	st->chip_info =
		&ad7780_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	if (pdata && pdata->vref_mv)
		st->int_vref_mv = pdata->vref_mv;
	else if (voltage_uv)
		st->int_vref_mv = voltage_uv / 1000;
	else
		dev_warn(&spi->dev, "reference voltage unspecified\n");

	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;
	indio_dev->info = &ad7780_info;

	if (pdata && gpio_is_valid(pdata->gpio_pdrst)) {

		ret = gpio_request_one(pdata->gpio_pdrst, GPIOF_OUT_INIT_LOW,
			       "AD7780 /PDRST");
		if (ret) {
			dev_err(&spi->dev, "failed to request GPIO PDRST\n");
			goto error_disable_reg;
		}
		st->powerdown_gpio = pdata->gpio_pdrst;
	} else {
		st->powerdown_gpio = -1;
	}

	ret = ad_sd_setup_buffer_and_trigger(indio_dev);
	if (ret)
		goto error_free_gpio;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_buffer_and_trigger;

	return 0;

error_cleanup_buffer_and_trigger:
	ad_sd_cleanup_buffer_and_trigger(indio_dev);
error_free_gpio:
	if (pdata && gpio_is_valid(pdata->gpio_pdrst))
		gpio_free(pdata->gpio_pdrst);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);

	iio_device_free(indio_dev);

	return ret;
}

static int ad7780_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7780_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	ad_sd_cleanup_buffer_and_trigger(indio_dev);

	if (gpio_is_valid(st->powerdown_gpio))
		gpio_free(st->powerdown_gpio);

	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_device_free(indio_dev);

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
		.owner	= THIS_MODULE,
	},
	.probe		= ad7780_probe,
	.remove		= __devexit_p(ad7780_remove),
	.id_table	= ad7780_id,
};
module_spi_driver(ad7780_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7780 and similar ADCs");
MODULE_LICENSE("GPL v2");

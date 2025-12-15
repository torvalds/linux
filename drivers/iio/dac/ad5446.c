// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AD5446 CORE DAC driver
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/iio/iio.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>

#include "ad5446.h"

#define MODE_PWRDWN_1k		0x1
#define MODE_PWRDWN_100k	0x2
#define MODE_PWRDWN_TRISTATE	0x3

static const char * const ad5446_powerdown_modes[] = {
	"1kohm_to_gnd", "100kohm_to_gnd", "three_state"
};

static int ad5446_set_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     unsigned int mode)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	st->pwr_down_mode = mode + 1;

	return 0;
}

static int ad5446_get_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	return st->pwr_down_mode - 1;
}

static const struct iio_enum ad5446_powerdown_mode_enum = {
	.items = ad5446_powerdown_modes,
	.num_items = ARRAY_SIZE(ad5446_powerdown_modes),
	.get = ad5446_get_powerdown_mode,
	.set = ad5446_set_powerdown_mode,
};

static ssize_t ad5446_read_dac_powerdown(struct iio_dev *indio_dev,
					 uintptr_t private,
					 const struct iio_chan_spec *chan,
					 char *buf)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5446_write_dac_powerdown(struct iio_dev *indio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  const char *buf, size_t len)
{
	struct ad5446_state *st = iio_priv(indio_dev);
	unsigned int shift;
	unsigned int val;
	bool powerdown;
	int ret;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);
	st->pwr_down = powerdown;

	if (st->pwr_down) {
		shift = chan->scan_type.realbits + chan->scan_type.shift;
		val = st->pwr_down_mode << shift;
	} else {
		val = st->cached_val;
	}

	ret = st->chip_info->write(st, val);
	if (ret)
		return ret;

	return len;
}

const struct iio_chan_spec_ext_info ad5446_ext_info_powerdown[] = {
	{
		.name = "powerdown",
		.read = ad5446_read_dac_powerdown,
		.write = ad5446_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad5446_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &ad5446_powerdown_mode_enum),
	{ }
};
EXPORT_SYMBOL_NS_GPL(ad5446_ext_info_powerdown, "IIO_AD5446");

static int ad5446_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		*val = st->cached_val >> chan->scan_type.shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

static int ad5446_write_dac_raw(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				int val)
{
	struct ad5446_state *st = iio_priv(indio_dev);

	if (val >= (1 << chan->scan_type.realbits) || val < 0)
		return -EINVAL;

	val <<= chan->scan_type.shift;
	guard(mutex)(&st->lock);

	st->cached_val = val;
	if (st->pwr_down)
		return 0;

	return st->chip_info->write(st, val);
}

static int ad5446_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return ad5446_write_dac_raw(indio_dev, chan, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad5446_info = {
	.read_raw = ad5446_read_raw,
	.write_raw = ad5446_write_raw,
};

int ad5446_probe(struct device *dev, const char *name,
		 const struct ad5446_chip_info *chip_info)
{
	struct ad5446_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->chip_info = chip_info;

	st->dev = dev;

	indio_dev->name = name;
	indio_dev->info = &ad5446_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->pwr_down_mode = MODE_PWRDWN_1k;

	ret = devm_regulator_get_enable_read_voltage(dev, "vcc");
	if (ret < 0 && ret != -ENODEV)
		return ret;
	if (ret == -ENODEV) {
		if (!chip_info->int_vref_mv)
			return dev_err_probe(dev, ret,
					     "reference voltage unspecified\n");

		st->vref_mv = chip_info->int_vref_mv;
	} else {
		st->vref_mv = ret / 1000;
	}

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(ad5446_probe, "IIO_AD5446");

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices CORE AD5446 DAC and similar devices");
MODULE_LICENSE("GPL v2");

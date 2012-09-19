/*
 * AD5446 SPI DAC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "ad5446.h"

static int ad5446_write(struct ad5446_state *st, unsigned val)
{
	__be16 data = cpu_to_be16(val);
	return spi_write(st->spi, &data, sizeof(data));
}

static int ad5660_write(struct ad5446_state *st, unsigned val)
{
	uint8_t data[3];

	data[0] = (val >> 16) & 0xFF;
	data[1] = (val >> 8) & 0xFF;
	data[2] = val & 0xFF;

	return spi_write(st->spi, data, sizeof(data));
}

static const char * const ad5446_powerdown_modes[] = {
	"1kohm_to_gnd", "100kohm_to_gnd", "three_state"
};

static int ad5446_set_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int mode)
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

	return sprintf(buf, "%d\n", st->pwr_down);
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

	ret = strtobool(buf, &powerdown);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);
	st->pwr_down = powerdown;

	if (st->pwr_down) {
		shift = chan->scan_type.realbits + chan->scan_type.shift;
		val = st->pwr_down_mode << shift;
	} else {
		val = st->cached_val;
	}

	ret = st->chip_info->write(st, val);
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static const struct iio_chan_spec_ext_info ad5064_ext_info_powerdown[] = {
	{
		.name = "powerdown",
		.read = ad5446_read_dac_powerdown,
		.write = ad5446_write_dac_powerdown,
	},
	IIO_ENUM("powerdown_mode", false, &ad5446_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", &ad5446_powerdown_mode_enum),
	{ },
};

#define _AD5446_CHANNEL(bits, storage, shift, ext) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.output = 1, \
	.channel = 0, \
	.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT | \
	IIO_CHAN_INFO_SCALE_SHARED_BIT,	\
	.scan_type = IIO_ST('u', (bits), (storage), (shift)), \
	.ext_info = (ext), \
}

#define AD5446_CHANNEL(bits, storage, shift) \
	_AD5446_CHANNEL(bits, storage, shift, NULL)

#define AD5446_CHANNEL_POWERDOWN(bits, storage, shift) \
	_AD5446_CHANNEL(bits, storage, shift, ad5064_ext_info_powerdown)

static const struct ad5446_chip_info ad5446_chip_info_tbl[] = {
	[ID_AD5444] = {
		.channel = AD5446_CHANNEL(12, 16, 2),
		.write = ad5446_write,
	},
	[ID_AD5446] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5450] = {
		.channel = AD5446_CHANNEL(8, 16, 6),
		.write = ad5446_write,
	},
	[ID_AD5451] = {
		.channel = AD5446_CHANNEL(10, 16, 4),
		.write = ad5446_write,
	},
	[ID_AD5541A] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5512A] = {
		.channel = AD5446_CHANNEL(12, 16, 4),
		.write = ad5446_write,
	},
	[ID_AD5553] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.write = ad5446_write,
	},
	[ID_AD5601] = {
		.channel = AD5446_CHANNEL_POWERDOWN(8, 16, 6),
		.write = ad5446_write,
	},
	[ID_AD5611] = {
		.channel = AD5446_CHANNEL_POWERDOWN(10, 16, 4),
		.write = ad5446_write,
	},
	[ID_AD5621] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
		.write = ad5446_write,
	},
	[ID_AD5620_2500] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
		.int_vref_mv = 2500,
		.write = ad5446_write,
	},
	[ID_AD5620_1250] = {
		.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
		.int_vref_mv = 1250,
		.write = ad5446_write,
	},
	[ID_AD5640_2500] = {
		.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
		.int_vref_mv = 2500,
		.write = ad5446_write,
	},
	[ID_AD5640_1250] = {
		.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
		.int_vref_mv = 1250,
		.write = ad5446_write,
	},
	[ID_AD5660_2500] = {
		.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
		.int_vref_mv = 2500,
		.write = ad5660_write,
	},
	[ID_AD5660_1250] = {
		.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
		.int_vref_mv = 1250,
		.write = ad5660_write,
	},
	[ID_AD5662] = {
		.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
		.write = ad5660_write,
	},
};

static int ad5446_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5446_state *st = iio_priv(indio_dev);
	unsigned long scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		*val = st->cached_val;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->vref_mv * 1000) >> chan->scan_type.realbits;
		*val =  scale_uv / 1000;
		*val2 = (scale_uv % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;

	}
	return -EINVAL;
}

static int ad5446_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad5446_state *st = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		val <<= chan->scan_type.shift;
		mutex_lock(&indio_dev->mlock);
		st->cached_val = val;
		if (!st->pwr_down)
			ret = st->chip_info->write(st, val);
		mutex_unlock(&indio_dev->mlock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad5446_info = {
	.read_raw = ad5446_read_raw,
	.write_raw = ad5446_write_raw,
	.driver_module = THIS_MODULE,
};

static int __devinit ad5446_probe(struct spi_device *spi)
{
	struct ad5446_state *st;
	struct iio_dev *indio_dev;
	struct regulator *reg;
	int ret, voltage_uv = 0;

	reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(reg)) {
		ret = regulator_enable(reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(reg);
	}

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}
	st = iio_priv(indio_dev);
	st->chip_info =
		&ad5446_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	spi_set_drvdata(spi, indio_dev);
	st->reg = reg;
	st->spi = spi;

	/* Establish that the iio_dev is a child of the spi device */
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5446_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;

	st->pwr_down_mode = MODE_PWRDWN_1k;

	if (st->chip_info->int_vref_mv)
		st->vref_mv = st->chip_info->int_vref_mv;
	else if (voltage_uv)
		st->vref_mv = voltage_uv / 1000;
	else
		dev_warn(&spi->dev, "reference voltage unspecified\n");

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_device;

	return 0;

error_free_device:
	iio_device_free(indio_dev);
error_disable_reg:
	if (!IS_ERR(reg))
		regulator_disable(reg);
error_put_reg:
	if (!IS_ERR(reg))
		regulator_put(reg);

	return ret;
}

static int ad5446_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5446_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_device_free(indio_dev);

	return 0;
}

static const struct spi_device_id ad5446_id[] = {
	{"ad5444", ID_AD5444},
	{"ad5446", ID_AD5446},
	{"ad5450", ID_AD5450},
	{"ad5451", ID_AD5451},
	{"ad5452", ID_AD5444}, /* ad5452 is compatible to the ad5444 */
	{"ad5453", ID_AD5446}, /* ad5453 is compatible to the ad5446 */
	{"ad5512a", ID_AD5512A},
	{"ad5541a", ID_AD5541A},
	{"ad5542a", ID_AD5541A}, /* ad5541a and ad5542a are compatible */
	{"ad5543", ID_AD5541A}, /* ad5541a and ad5543 are compatible */
	{"ad5553", ID_AD5553},
	{"ad5601", ID_AD5601},
	{"ad5611", ID_AD5611},
	{"ad5621", ID_AD5621},
	{"ad5620-2500", ID_AD5620_2500}, /* AD5620/40/60: */
	{"ad5620-1250", ID_AD5620_1250}, /* part numbers may look differently */
	{"ad5640-2500", ID_AD5640_2500},
	{"ad5640-1250", ID_AD5640_1250},
	{"ad5660-2500", ID_AD5660_2500},
	{"ad5660-1250", ID_AD5660_1250},
	{"ad5662", ID_AD5662},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5446_id);

static struct spi_driver ad5446_driver = {
	.driver = {
		.name	= "ad5446",
		.owner	= THIS_MODULE,
	},
	.probe		= ad5446_probe,
	.remove		= __devexit_p(ad5446_remove),
	.id_table	= ad5446_id,
};
module_spi_driver(ad5446_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5444/AD5446 DAC");
MODULE_LICENSE("GPL v2");

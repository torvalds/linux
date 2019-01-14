/*
 * AD8366 SPI Dual-Digital Variable Gain Amplifier (VGA)
 *
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/bitrev.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

struct ad8366_state {
	struct spi_device	*spi;
	struct regulator	*reg;
	unsigned char		ch[2];
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	unsigned char		data[2] ____cacheline_aligned;
};

static int ad8366_write(struct iio_dev *indio_dev,
			unsigned char ch_a, unsigned char ch_b)
{
	struct ad8366_state *st = iio_priv(indio_dev);
	int ret;

	ch_a = bitrev8(ch_a & 0x3F);
	ch_b = bitrev8(ch_b & 0x3F);

	st->data[0] = ch_b >> 4;
	st->data[1] = (ch_b << 4) | (ch_a >> 2);

	ret = spi_write(st->spi, st->data, ARRAY_SIZE(st->data));
	if (ret < 0)
		dev_err(&indio_dev->dev, "write failed (%d)", ret);

	return ret;
}

static int ad8366_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad8366_state *st = iio_priv(indio_dev);
	int ret;
	unsigned code;

	mutex_lock(&indio_dev->mlock);
	switch (m) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		code = st->ch[chan->channel];

		/* Values in dB */
		code = code * 253 + 4500;
		*val = code / 1000;
		*val2 = (code % 1000) * 1000;

		ret = IIO_VAL_INT_PLUS_MICRO_DB;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&indio_dev->mlock);

	return ret;
};

static int ad8366_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad8366_state *st = iio_priv(indio_dev);
	unsigned code;
	int ret;

	if (val < 0 || val2 < 0)
		return -EINVAL;

	/* Values in dB */
	code = (((u8)val * 1000) + ((u32)val2 / 1000));

	if (code > 20500 || code < 4500)
		return -EINVAL;

	code = (code - 4500) / 253;

	mutex_lock(&indio_dev->mlock);
	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		st->ch[chan->channel] = code;
		ret = ad8366_write(indio_dev, st->ch[0], st->ch[1]);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static const struct iio_info ad8366_info = {
	.read_raw = &ad8366_read_raw,
	.write_raw = &ad8366_write_raw,
};

#define AD8366_CHAN(_channel) {				\
	.type = IIO_VOLTAGE,				\
	.output = 1,					\
	.indexed = 1,					\
	.channel = _channel,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_HARDWAREGAIN),\
}

static const struct iio_chan_spec ad8366_channels[] = {
	AD8366_CHAN(0),
	AD8366_CHAN(1),
};

static int ad8366_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad8366_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = devm_regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			return ret;
	}

	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad8366_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad8366_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad8366_channels);

	ret = ad8366_write(indio_dev, 0 , 0);
	if (ret < 0)
		goto error_disable_reg;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return ret;
}

static int ad8366_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad8366_state *st = iio_priv(indio_dev);
	struct regulator *reg = st->reg;

	iio_device_unregister(indio_dev);

	if (!IS_ERR(reg))
		regulator_disable(reg);

	return 0;
}

static const struct spi_device_id ad8366_id[] = {
	{"ad8366", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, ad8366_id);

static struct spi_driver ad8366_driver = {
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= ad8366_probe,
	.remove		= ad8366_remove,
	.id_table	= ad8366_id,
};

module_spi_driver(ad8366_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD8366 VGA");
MODULE_LICENSE("GPL v2");

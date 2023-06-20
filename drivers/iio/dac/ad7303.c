// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD7303 Digital to analog converters driver
 *
 * Copyright 2013 Analog Devices Inc.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define AD7303_CFG_EXTERNAL_VREF BIT(15)
#define AD7303_CFG_POWER_DOWN(ch) BIT(11 + (ch))
#define AD7303_CFG_ADDR_OFFSET	10

#define AD7303_CMD_UPDATE_DAC	(0x3 << 8)

/**
 * struct ad7303_state - driver instance specific data
 * @spi:		the device for this driver instance
 * @config:		cached config register value
 * @dac_cache:		current DAC raw value (chip does not support readback)
 * @vdd_reg:		reference to VDD regulator
 * @vref_reg:		reference to VREF regulator
 * @lock:		protect writes and cache updates
 * @data:		spi transfer buffer
 */

struct ad7303_state {
	struct spi_device *spi;
	uint16_t config;
	uint8_t dac_cache[2];

	struct regulator *vdd_reg;
	struct regulator *vref_reg;

	struct mutex lock;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be16 data ____cacheline_aligned;
};

static int ad7303_write(struct ad7303_state *st, unsigned int chan,
	uint8_t val)
{
	st->data = cpu_to_be16(AD7303_CMD_UPDATE_DAC |
		(chan << AD7303_CFG_ADDR_OFFSET) |
		st->config | val);

	return spi_write(st->spi, &st->data, sizeof(st->data));
}

static ssize_t ad7303_read_dac_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct ad7303_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", (bool)(st->config &
		AD7303_CFG_POWER_DOWN(chan->channel)));
}

static ssize_t ad7303_write_dac_powerdown(struct iio_dev *indio_dev,
	 uintptr_t private, const struct iio_chan_spec *chan, const char *buf,
	 size_t len)
{
	struct ad7303_state *st = iio_priv(indio_dev);
	bool pwr_down;
	int ret;

	ret = kstrtobool(buf, &pwr_down);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	if (pwr_down)
		st->config |= AD7303_CFG_POWER_DOWN(chan->channel);
	else
		st->config &= ~AD7303_CFG_POWER_DOWN(chan->channel);

	/* There is no noop cmd which allows us to only update the powerdown
	 * mode, so just write one of the DAC channels again */
	ad7303_write(st, chan->channel, st->dac_cache[chan->channel]);

	mutex_unlock(&st->lock);
	return len;
}

static int ad7303_get_vref(struct ad7303_state *st,
	struct iio_chan_spec const *chan)
{
	int ret;

	if (st->config & AD7303_CFG_EXTERNAL_VREF)
		return regulator_get_voltage(st->vref_reg);

	ret = regulator_get_voltage(st->vdd_reg);
	if (ret < 0)
		return ret;
	return ret / 2;
}

static int ad7303_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long info)
{
	struct ad7303_state *st = iio_priv(indio_dev);
	int vref_uv;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		*val = st->dac_cache[chan->channel];
		mutex_unlock(&st->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		vref_uv = ad7303_get_vref(st, chan);
		if (vref_uv < 0)
			return vref_uv;

		*val = 2 * vref_uv / 1000;
		*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		break;
	}
	return -EINVAL;
}

static int ad7303_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct ad7303_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		mutex_lock(&st->lock);
		ret = ad7303_write(st, chan->address, val);
		if (ret == 0)
			st->dac_cache[chan->channel] = val;
		mutex_unlock(&st->lock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad7303_info = {
	.read_raw = ad7303_read_raw,
	.write_raw = ad7303_write_raw,
};

static const struct iio_chan_spec_ext_info ad7303_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad7303_read_dac_powerdown,
		.write = ad7303_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	{ },
};

#define AD7303_CHANNEL(chan) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (chan),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.address = (chan),					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 8,					\
		.storagebits = 8,				\
		.shift = 0,					\
	},							\
	.ext_info = ad7303_ext_info,				\
}

static const struct iio_chan_spec ad7303_channels[] = {
	AD7303_CHANNEL(0),
	AD7303_CHANNEL(1),
};

static void ad7303_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static int ad7303_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct iio_dev *indio_dev;
	struct ad7303_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->spi = spi;

	mutex_init(&st->lock);

	st->vdd_reg = devm_regulator_get(&spi->dev, "Vdd");
	if (IS_ERR(st->vdd_reg))
		return PTR_ERR(st->vdd_reg);

	ret = regulator_enable(st->vdd_reg);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, ad7303_reg_disable, st->vdd_reg);
	if (ret)
		return ret;

	st->vref_reg = devm_regulator_get_optional(&spi->dev, "REF");
	if (IS_ERR(st->vref_reg)) {
		ret = PTR_ERR(st->vref_reg);
		if (ret != -ENODEV)
			return ret;
		st->vref_reg = NULL;
	}

	if (st->vref_reg) {
		ret = regulator_enable(st->vref_reg);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(&spi->dev, ad7303_reg_disable,
					       st->vref_reg);
		if (ret)
			return ret;

		st->config |= AD7303_CFG_EXTERNAL_VREF;
	}

	indio_dev->name = id->name;
	indio_dev->info = &ad7303_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad7303_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7303_channels);

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ad7303_spi_of_match[] = {
	{ .compatible = "adi,ad7303", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ad7303_spi_of_match);

static const struct spi_device_id ad7303_spi_ids[] = {
	{ "ad7303", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad7303_spi_ids);

static struct spi_driver ad7303_driver = {
	.driver = {
		.name = "ad7303",
		.of_match_table = ad7303_spi_of_match,
	},
	.probe = ad7303_probe,
	.id_table = ad7303_spi_ids,
};
module_spi_driver(ad7303_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD7303 DAC driver");
MODULE_LICENSE("GPL v2");

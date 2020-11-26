// SPDX-License-Identifier: GPL-2.0
/* ad7949.c - Analog Devices ADC driver 14/16 bits 4/8 channels
 *
 * Copyright (C) 2018 CMC NV
 *
 * https://www.analog.com/media/en/technical-documentation/data-sheets/AD7949.pdf
 */

#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#define AD7949_MASK_CHANNEL_SEL		GENMASK(9, 7)
#define AD7949_MASK_TOTAL		GENMASK(13, 0)
#define AD7949_OFFSET_CHANNEL_SEL	7
#define AD7949_CFG_READ_BACK		0x1
#define AD7949_CFG_REG_SIZE_BITS	14

enum {
	ID_AD7949 = 0,
	ID_AD7682,
	ID_AD7689,
};

struct ad7949_adc_spec {
	u8 num_channels;
	u8 resolution;
};

static const struct ad7949_adc_spec ad7949_adc_spec[] = {
	[ID_AD7949] = { .num_channels = 8, .resolution = 14 },
	[ID_AD7682] = { .num_channels = 4, .resolution = 16 },
	[ID_AD7689] = { .num_channels = 8, .resolution = 16 },
};

/**
 * struct ad7949_adc_chip - AD ADC chip
 * @lock: protects write sequences
 * @vref: regulator generating Vref
 * @indio_dev: reference to iio structure
 * @spi: reference to spi structure
 * @resolution: resolution of the chip
 * @cfg: copy of the configuration register
 * @current_channel: current channel in use
 * @buffer: buffer to send / receive data to / from device
 */
struct ad7949_adc_chip {
	struct mutex lock;
	struct regulator *vref;
	struct iio_dev *indio_dev;
	struct spi_device *spi;
	u8 resolution;
	u16 cfg;
	unsigned int current_channel;
	u16 buffer ____cacheline_aligned;
};

static int ad7949_spi_write_cfg(struct ad7949_adc_chip *ad7949_adc, u16 val,
				u16 mask)
{
	int ret;
	int bits_per_word = ad7949_adc->resolution;
	int shift = bits_per_word - AD7949_CFG_REG_SIZE_BITS;
	struct spi_message msg;
	struct spi_transfer tx[] = {
		{
			.tx_buf = &ad7949_adc->buffer,
			.len = 2,
			.bits_per_word = bits_per_word,
		},
	};

	ad7949_adc->cfg = (val & mask) | (ad7949_adc->cfg & ~mask);
	ad7949_adc->buffer = ad7949_adc->cfg << shift;
	spi_message_init_with_transfers(&msg, tx, 1);
	ret = spi_sync(ad7949_adc->spi, &msg);

	/*
	 * This delay is to avoid a new request before the required time to
	 * send a new command to the device
	 */
	udelay(2);
	return ret;
}

static int ad7949_spi_read_channel(struct ad7949_adc_chip *ad7949_adc, int *val,
				   unsigned int channel)
{
	int ret;
	int i;
	int bits_per_word = ad7949_adc->resolution;
	int mask = GENMASK(ad7949_adc->resolution, 0);
	struct spi_message msg;
	struct spi_transfer tx[] = {
		{
			.rx_buf = &ad7949_adc->buffer,
			.len = 2,
			.bits_per_word = bits_per_word,
		},
	};

	/*
	 * 1: write CFG for sample N and read old data (sample N-2)
	 * 2: if CFG was not changed since sample N-1 then we'll get good data
	 *    at the next xfer, so we bail out now, otherwise we write something
	 *    and we read garbage (sample N-1 configuration).
	 */
	for (i = 0; i < 2; i++) {
		ret = ad7949_spi_write_cfg(ad7949_adc,
					   channel << AD7949_OFFSET_CHANNEL_SEL,
					   AD7949_MASK_CHANNEL_SEL);
		if (ret)
			return ret;
		if (channel == ad7949_adc->current_channel)
			break;
	}

	/* 3: write something and read actual data */
	ad7949_adc->buffer = 0;
	spi_message_init_with_transfers(&msg, tx, 1);
	ret = spi_sync(ad7949_adc->spi, &msg);
	if (ret)
		return ret;

	/*
	 * This delay is to avoid a new request before the required time to
	 * send a new command to the device
	 */
	udelay(2);

	ad7949_adc->current_channel = channel;

	*val = ad7949_adc->buffer & mask;

	return 0;
}

#define AD7949_ADC_CHANNEL(chan) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (chan),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec ad7949_adc_channels[] = {
	AD7949_ADC_CHANNEL(0),
	AD7949_ADC_CHANNEL(1),
	AD7949_ADC_CHANNEL(2),
	AD7949_ADC_CHANNEL(3),
	AD7949_ADC_CHANNEL(4),
	AD7949_ADC_CHANNEL(5),
	AD7949_ADC_CHANNEL(6),
	AD7949_ADC_CHANNEL(7),
};

static int ad7949_spi_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ad7949_adc_chip *ad7949_adc = iio_priv(indio_dev);
	int ret;

	if (!val)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&ad7949_adc->lock);
		ret = ad7949_spi_read_channel(ad7949_adc, val, chan->channel);
		mutex_unlock(&ad7949_adc->lock);

		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(ad7949_adc->vref);
		if (ret < 0)
			return ret;

		*val = ret / 5000;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad7949_spi_reg_access(struct iio_dev *indio_dev,
			unsigned int reg, unsigned int writeval,
			unsigned int *readval)
{
	struct ad7949_adc_chip *ad7949_adc = iio_priv(indio_dev);
	int ret = 0;

	if (readval)
		*readval = ad7949_adc->cfg;
	else
		ret = ad7949_spi_write_cfg(ad7949_adc,
			writeval & AD7949_MASK_TOTAL, AD7949_MASK_TOTAL);

	return ret;
}

static const struct iio_info ad7949_spi_info = {
	.read_raw = ad7949_spi_read_raw,
	.debugfs_reg_access = ad7949_spi_reg_access,
};

static int ad7949_spi_init(struct ad7949_adc_chip *ad7949_adc)
{
	int ret;
	int val;

	/* Sequencer disabled, CFG readback disabled, IN0 as default channel */
	ad7949_adc->current_channel = 0;
	ret = ad7949_spi_write_cfg(ad7949_adc, 0x3C79, AD7949_MASK_TOTAL);

	/*
	 * Do two dummy conversions to apply the first configuration setting.
	 * Required only after the start up of the device.
	 */
	ad7949_spi_read_channel(ad7949_adc, &val, ad7949_adc->current_channel);
	ad7949_spi_read_channel(ad7949_adc, &val, ad7949_adc->current_channel);

	return ret;
}

static int ad7949_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct ad7949_adc_spec *spec;
	struct ad7949_adc_chip *ad7949_adc;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*ad7949_adc));
	if (!indio_dev) {
		dev_err(dev, "can not allocate iio device\n");
		return -ENOMEM;
	}

	indio_dev->info = &ad7949_spi_info;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad7949_adc_channels;
	spi_set_drvdata(spi, indio_dev);

	ad7949_adc = iio_priv(indio_dev);
	ad7949_adc->indio_dev = indio_dev;
	ad7949_adc->spi = spi;

	spec = &ad7949_adc_spec[spi_get_device_id(spi)->driver_data];
	indio_dev->num_channels = spec->num_channels;
	ad7949_adc->resolution = spec->resolution;

	ad7949_adc->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(ad7949_adc->vref)) {
		dev_err(dev, "fail to request regulator\n");
		return PTR_ERR(ad7949_adc->vref);
	}

	ret = regulator_enable(ad7949_adc->vref);
	if (ret < 0) {
		dev_err(dev, "fail to enable regulator\n");
		return ret;
	}

	mutex_init(&ad7949_adc->lock);

	ret = ad7949_spi_init(ad7949_adc);
	if (ret) {
		dev_err(dev, "enable to init this device: %d\n", ret);
		goto err;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "fail to register iio device: %d\n", ret);
		goto err;
	}

	return 0;

err:
	mutex_destroy(&ad7949_adc->lock);
	regulator_disable(ad7949_adc->vref);

	return ret;
}

static int ad7949_spi_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7949_adc_chip *ad7949_adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	mutex_destroy(&ad7949_adc->lock);
	regulator_disable(ad7949_adc->vref);

	return 0;
}

static const struct of_device_id ad7949_spi_of_id[] = {
	{ .compatible = "adi,ad7949" },
	{ .compatible = "adi,ad7682" },
	{ .compatible = "adi,ad7689" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7949_spi_of_id);

static const struct spi_device_id ad7949_spi_id[] = {
	{ "ad7949", ID_AD7949  },
	{ "ad7682", ID_AD7682 },
	{ "ad7689", ID_AD7689 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7949_spi_id);

static struct spi_driver ad7949_spi_driver = {
	.driver = {
		.name		= "ad7949",
		.of_match_table	= ad7949_spi_of_id,
	},
	.probe	  = ad7949_spi_probe,
	.remove   = ad7949_spi_remove,
	.id_table = ad7949_spi_id,
};
module_spi_driver(ad7949_spi_driver);

MODULE_AUTHOR("Charles-Antoine Couret <charles-antoine.couret@essensium.com>");
MODULE_DESCRIPTION("Analog Devices 14/16-bit 8-channel ADC driver");
MODULE_LICENSE("GPL v2");

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
#include <linux/bitfield.h>

#define AD7949_CFG_MASK_TOTAL		GENMASK(13, 0)

/* CFG: Configuration Update */
#define AD7949_CFG_MASK_OVERWRITE	BIT(13)

/* INCC: Input Channel Configuration */
#define AD7949_CFG_MASK_INCC		GENMASK(12, 10)
#define AD7949_CFG_VAL_INCC_UNIPOLAR_GND	7
#define AD7949_CFG_VAL_INCC_UNIPOLAR_COMM	6
#define AD7949_CFG_VAL_INCC_UNIPOLAR_DIFF	4
#define AD7949_CFG_VAL_INCC_TEMP		3
#define AD7949_CFG_VAL_INCC_BIPOLAR		2
#define AD7949_CFG_VAL_INCC_BIPOLAR_DIFF	0

/* INX: Input channel Selection in a binary fashion */
#define AD7949_CFG_MASK_INX		GENMASK(9, 7)

/* BW: select bandwidth for low-pass filter. Full or Quarter */
#define AD7949_CFG_MASK_BW_FULL		BIT(6)

/* REF: reference/buffer selection */
#define AD7949_CFG_MASK_REF		GENMASK(5, 3)
#define AD7949_CFG_VAL_REF_EXT_TEMP_BUF		3
#define AD7949_CFG_VAL_REF_EXT_TEMP		2
#define AD7949_CFG_VAL_REF_INT_4096		1
#define AD7949_CFG_VAL_REF_INT_2500		0
#define AD7949_CFG_VAL_REF_EXTERNAL		BIT(1)

/* SEQ: channel sequencer. Allows for scanning channels */
#define AD7949_CFG_MASK_SEQ		GENMASK(2, 1)

/* RB: Read back the CFG register */
#define AD7949_CFG_MASK_RBN		BIT(0)

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
 * @refsel: reference selection
 * @resolution: resolution of the chip
 * @cfg: copy of the configuration register
 * @current_channel: current channel in use
 * @buffer: buffer to send / receive data to / from device
 * @buf8b: be16 buffer to exchange data with the device in 8-bit transfers
 */
struct ad7949_adc_chip {
	struct mutex lock;
	struct regulator *vref;
	struct iio_dev *indio_dev;
	struct spi_device *spi;
	u32 refsel;
	u8 resolution;
	u16 cfg;
	unsigned int current_channel;
	u16 buffer __aligned(IIO_DMA_MINALIGN);
	__be16 buf8b;
};

static int ad7949_spi_write_cfg(struct ad7949_adc_chip *ad7949_adc, u16 val,
				u16 mask)
{
	int ret;

	ad7949_adc->cfg = (val & mask) | (ad7949_adc->cfg & ~mask);

	switch (ad7949_adc->spi->bits_per_word) {
	case 16:
		ad7949_adc->buffer = ad7949_adc->cfg << 2;
		ret = spi_write(ad7949_adc->spi, &ad7949_adc->buffer, 2);
		break;
	case 14:
		ad7949_adc->buffer = ad7949_adc->cfg;
		ret = spi_write(ad7949_adc->spi, &ad7949_adc->buffer, 2);
		break;
	case 8:
		/* Here, type is big endian as it must be sent in two transfers */
		ad7949_adc->buf8b = cpu_to_be16(ad7949_adc->cfg << 2);
		ret = spi_write(ad7949_adc->spi, &ad7949_adc->buf8b, 2);
		break;
	default:
		dev_err(&ad7949_adc->indio_dev->dev, "unsupported BPW\n");
		return -EINVAL;
	}

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

	/*
	 * 1: write CFG for sample N and read old data (sample N-2)
	 * 2: if CFG was not changed since sample N-1 then we'll get good data
	 *    at the next xfer, so we bail out now, otherwise we write something
	 *    and we read garbage (sample N-1 configuration).
	 */
	for (i = 0; i < 2; i++) {
		ret = ad7949_spi_write_cfg(ad7949_adc,
					   FIELD_PREP(AD7949_CFG_MASK_INX, channel),
					   AD7949_CFG_MASK_INX);
		if (ret)
			return ret;
		if (channel == ad7949_adc->current_channel)
			break;
	}

	/* 3: write something and read actual data */
	if (ad7949_adc->spi->bits_per_word == 8)
		ret = spi_read(ad7949_adc->spi, &ad7949_adc->buf8b, 2);
	else
		ret = spi_read(ad7949_adc->spi, &ad7949_adc->buffer, 2);

	if (ret)
		return ret;

	/*
	 * This delay is to avoid a new request before the required time to
	 * send a new command to the device
	 */
	udelay(2);

	ad7949_adc->current_channel = channel;

	switch (ad7949_adc->spi->bits_per_word) {
	case 16:
		*val = ad7949_adc->buffer;
		/* Shift-out padding bits */
		*val >>= 16 - ad7949_adc->resolution;
		break;
	case 14:
		*val = ad7949_adc->buffer & GENMASK(13, 0);
		break;
	case 8:
		/* Here, type is big endian as data was sent in two transfers */
		*val = be16_to_cpu(ad7949_adc->buf8b);
		/* Shift-out padding bits */
		*val >>= 16 - ad7949_adc->resolution;
		break;
	default:
		dev_err(&ad7949_adc->indio_dev->dev, "unsupported BPW\n");
		return -EINVAL;
	}

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
		switch (ad7949_adc->refsel) {
		case AD7949_CFG_VAL_REF_INT_2500:
			*val = 2500;
			break;
		case AD7949_CFG_VAL_REF_INT_4096:
			*val = 4096;
			break;
		case AD7949_CFG_VAL_REF_EXT_TEMP:
		case AD7949_CFG_VAL_REF_EXT_TEMP_BUF:
			ret = regulator_get_voltage(ad7949_adc->vref);
			if (ret < 0)
				return ret;

			/* convert value back to mV */
			*val = ret / 1000;
			break;
		}

		*val2 = (1 << ad7949_adc->resolution) - 1;
		return IIO_VAL_FRACTIONAL;
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
		ret = ad7949_spi_write_cfg(ad7949_adc, writeval,
					   AD7949_CFG_MASK_TOTAL);

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
	u16 cfg;

	ad7949_adc->current_channel = 0;

	cfg = FIELD_PREP(AD7949_CFG_MASK_OVERWRITE, 1) |
		FIELD_PREP(AD7949_CFG_MASK_INCC, AD7949_CFG_VAL_INCC_UNIPOLAR_GND) |
		FIELD_PREP(AD7949_CFG_MASK_INX, ad7949_adc->current_channel) |
		FIELD_PREP(AD7949_CFG_MASK_BW_FULL, 1) |
		FIELD_PREP(AD7949_CFG_MASK_REF, ad7949_adc->refsel) |
		FIELD_PREP(AD7949_CFG_MASK_SEQ, 0x0) |
		FIELD_PREP(AD7949_CFG_MASK_RBN, 1);

	ret = ad7949_spi_write_cfg(ad7949_adc, cfg, AD7949_CFG_MASK_TOTAL);

	/*
	 * Do two dummy conversions to apply the first configuration setting.
	 * Required only after the start up of the device.
	 */
	ad7949_spi_read_channel(ad7949_adc, &val, ad7949_adc->current_channel);
	ad7949_spi_read_channel(ad7949_adc, &val, ad7949_adc->current_channel);

	return ret;
}

static void ad7949_disable_reg(void *reg)
{
	regulator_disable(reg);
}

static int ad7949_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct ad7949_adc_spec *spec;
	struct ad7949_adc_chip *ad7949_adc;
	struct iio_dev *indio_dev;
	u32 tmp;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*ad7949_adc));
	if (!indio_dev)
		return -ENOMEM;

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

	/* Set SPI bits per word */
	if (spi_is_bpw_supported(spi, ad7949_adc->resolution)) {
		spi->bits_per_word = ad7949_adc->resolution;
	} else if (spi_is_bpw_supported(spi, 16)) {
		spi->bits_per_word = 16;
	} else if (spi_is_bpw_supported(spi, 8)) {
		spi->bits_per_word = 8;
	} else {
		dev_err(dev, "unable to find common BPW with spi controller\n");
		return -EINVAL;
	}

	/* Setup internal voltage reference */
	tmp = 4096000;
	device_property_read_u32(dev, "adi,internal-ref-microvolt", &tmp);

	switch (tmp) {
	case 2500000:
		ad7949_adc->refsel = AD7949_CFG_VAL_REF_INT_2500;
		break;
	case 4096000:
		ad7949_adc->refsel = AD7949_CFG_VAL_REF_INT_4096;
		break;
	default:
		dev_err(dev, "unsupported internal voltage reference\n");
		return -EINVAL;
	}

	/* Setup external voltage reference, buffered? */
	ad7949_adc->vref = devm_regulator_get_optional(dev, "vrefin");
	if (IS_ERR(ad7949_adc->vref)) {
		ret = PTR_ERR(ad7949_adc->vref);
		if (ret != -ENODEV)
			return ret;
		/* unbuffered? */
		ad7949_adc->vref = devm_regulator_get_optional(dev, "vref");
		if (IS_ERR(ad7949_adc->vref)) {
			ret = PTR_ERR(ad7949_adc->vref);
			if (ret != -ENODEV)
				return ret;
		} else {
			ad7949_adc->refsel = AD7949_CFG_VAL_REF_EXT_TEMP;
		}
	} else {
		ad7949_adc->refsel = AD7949_CFG_VAL_REF_EXT_TEMP_BUF;
	}

	if (ad7949_adc->refsel & AD7949_CFG_VAL_REF_EXTERNAL) {
		ret = regulator_enable(ad7949_adc->vref);
		if (ret < 0) {
			dev_err(dev, "fail to enable regulator\n");
			return ret;
		}

		ret = devm_add_action_or_reset(dev, ad7949_disable_reg,
					       ad7949_adc->vref);
		if (ret)
			return ret;
	}

	mutex_init(&ad7949_adc->lock);

	ret = ad7949_spi_init(ad7949_adc);
	if (ret) {
		dev_err(dev, "fail to init this device: %d\n", ret);
		return ret;
	}

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		dev_err(dev, "fail to register iio device: %d\n", ret);

	return ret;
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
	.id_table = ad7949_spi_id,
};
module_spi_driver(ad7949_spi_driver);

MODULE_AUTHOR("Charles-Antoine Couret <charles-antoine.couret@essensium.com>");
MODULE_DESCRIPTION("Analog Devices 14/16-bit 8-channel ADC driver");
MODULE_LICENSE("GPL v2");

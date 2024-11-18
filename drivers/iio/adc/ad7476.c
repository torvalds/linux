// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices AD7466/7/8 AD7476/5/7/8 (A) SPI ADC driver
 * TI ADC081S/ADC101S/ADC121S 8/10/12-bit SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

struct ad7476_state;

struct ad7476_chip_info {
	unsigned int			int_vref_uv;
	struct iio_chan_spec		channel[2];
	/* channels used when convst gpio is defined */
	struct iio_chan_spec		convst_channel[2];
	void (*reset)(struct ad7476_state *);
	bool				has_vref;
	bool				has_vdrive;
};

struct ad7476_state {
	struct spi_device		*spi;
	const struct ad7476_chip_info	*chip_info;
	struct regulator		*ref_reg;
	struct gpio_desc		*convst_gpio;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 * Make the buffer large enough for one 16 bit sample and one 64 bit
	 * aligned 64 bit timestamp.
	 */
	unsigned char data[ALIGN(2, sizeof(s64)) + sizeof(s64)] __aligned(IIO_DMA_MINALIGN);
};

enum ad7476_supported_device_ids {
	ID_AD7091,
	ID_AD7091R,
	ID_AD7273,
	ID_AD7274,
	ID_AD7276,
	ID_AD7277,
	ID_AD7278,
	ID_AD7466,
	ID_AD7467,
	ID_AD7468,
	ID_AD7475,
	ID_AD7495,
	ID_AD7940,
	ID_ADC081S,
	ID_ADC101S,
	ID_ADC121S,
	ID_ADS7866,
	ID_ADS7867,
	ID_ADS7868,
	ID_LTC2314_14,
};

static void ad7091_convst(struct ad7476_state *st)
{
	if (!st->convst_gpio)
		return;

	gpiod_set_value(st->convst_gpio, 0);
	udelay(1); /* CONVST pulse width: 10 ns min */
	gpiod_set_value(st->convst_gpio, 1);
	udelay(1); /* Conversion time: 650 ns max */
}

static irqreturn_t ad7476_trigger_handler(int irq, void  *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7476_state *st = iio_priv(indio_dev);
	int b_sent;

	ad7091_convst(st);

	b_sent = spi_sync(st->spi, &st->msg);
	if (b_sent < 0)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, st->data,
		iio_get_time_ns(indio_dev));
done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static void ad7091_reset(struct ad7476_state *st)
{
	/* Any transfers with 8 scl cycles will reset the device */
	spi_read(st->spi, st->data, 1);
}

static int ad7476_scan_direct(struct ad7476_state *st)
{
	int ret;

	ad7091_convst(st);

	ret = spi_sync(st->spi, &st->msg);
	if (ret)
		return ret;

	return be16_to_cpup((__be16 *)st->data);
}

static int ad7476_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad7476_state *st = iio_priv(indio_dev);
	int scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = ad7476_scan_direct(st);
		iio_device_release_direct_mode(indio_dev);

		if (ret < 0)
			return ret;
		*val = (ret >> st->chip_info->channel[0].scan_type.shift) &
			GENMASK(st->chip_info->channel[0].scan_type.realbits - 1, 0);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (st->ref_reg) {
			scale_uv = regulator_get_voltage(st->ref_reg);
			if (scale_uv < 0)
				return scale_uv;
		} else {
			scale_uv = st->chip_info->int_vref_uv;
		}
		*val = scale_uv / 1000;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

#define _AD7476_CHAN(bits, _shift, _info_mask_sep)		\
	{							\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.info_mask_separate = _info_mask_sep,			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = (bits),				\
		.storagebits = 16,				\
		.shift = (_shift),				\
		.endianness = IIO_BE,				\
	},							\
}

#define ADC081S_CHAN(bits) _AD7476_CHAN((bits), 12 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))
#define AD7476_CHAN(bits) _AD7476_CHAN((bits), 13 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))
#define AD7940_CHAN(bits) _AD7476_CHAN((bits), 15 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))
#define AD7091R_CHAN(bits) _AD7476_CHAN((bits), 16 - (bits), 0)
#define AD7091R_CONVST_CHAN(bits) _AD7476_CHAN((bits), 16 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))
#define ADS786X_CHAN(bits) _AD7476_CHAN((bits), 12 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))

static const struct ad7476_chip_info ad7476_chip_info_tbl[] = {
	[ID_AD7091] = {
		.channel[0] = AD7091R_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.convst_channel[0] = AD7091R_CONVST_CHAN(12),
		.convst_channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.reset = ad7091_reset,
	},
	[ID_AD7091R] = {
		.channel[0] = AD7091R_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.convst_channel[0] = AD7091R_CONVST_CHAN(12),
		.convst_channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.int_vref_uv = 2500000,
		.has_vref = true,
		.reset = ad7091_reset,
	},
	[ID_AD7273] = {
		.channel[0] = AD7940_CHAN(10),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.has_vref = true,
	},
	[ID_AD7274] = {
		.channel[0] = AD7940_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.has_vref = true,
	},
	[ID_AD7276] = {
		.channel[0] = AD7940_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7277] = {
		.channel[0] = AD7940_CHAN(10),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7278] = {
		.channel[0] = AD7940_CHAN(8),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7466] = {
		.channel[0] = AD7476_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7467] = {
		.channel[0] = AD7476_CHAN(10),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7468] = {
		.channel[0] = AD7476_CHAN(8),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7475] = {
		.channel[0] = AD7476_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.has_vref = true,
		.has_vdrive = true,
	},
	[ID_AD7495] = {
		.channel[0] = AD7476_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.int_vref_uv = 2500000,
		.has_vdrive = true,
	},
	[ID_AD7940] = {
		.channel[0] = AD7940_CHAN(14),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_ADC081S] = {
		.channel[0] = ADC081S_CHAN(8),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_ADC101S] = {
		.channel[0] = ADC081S_CHAN(10),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_ADC121S] = {
		.channel[0] = ADC081S_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_ADS7866] = {
		.channel[0] = ADS786X_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_ADS7867] = {
		.channel[0] = ADS786X_CHAN(10),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_ADS7868] = {
		.channel[0] = ADS786X_CHAN(8),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_LTC2314_14] = {
		.channel[0] = AD7940_CHAN(14),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.has_vref = true,
	},
};

static const struct iio_info ad7476_info = {
	.read_raw = &ad7476_read_raw,
};

static void ad7476_reg_disable(void *data)
{
	struct regulator *reg = data;

	regulator_disable(reg);
}

static int ad7476_probe(struct spi_device *spi)
{
	struct ad7476_state *st;
	struct iio_dev *indio_dev;
	struct regulator *reg;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->chip_info =
		&ad7476_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	reg = devm_regulator_get(&spi->dev, "vcc");
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	ret = regulator_enable(reg);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, ad7476_reg_disable, reg);
	if (ret)
		return ret;

	/* Either vcc or vref (below) as appropriate */
	if (!st->chip_info->int_vref_uv)
		st->ref_reg = reg;

	if (st->chip_info->has_vref) {

		/* If a device has an internal reference vref is optional */
		if (st->chip_info->int_vref_uv) {
			reg = devm_regulator_get_optional(&spi->dev, "vref");
			if (IS_ERR(reg) && (PTR_ERR(reg) != -ENODEV))
				return PTR_ERR(reg);
		} else {
			reg = devm_regulator_get(&spi->dev, "vref");
			if (IS_ERR(reg))
				return PTR_ERR(reg);
		}

		if (!IS_ERR(reg)) {
			ret = regulator_enable(reg);
			if (ret)
				return ret;

			ret = devm_add_action_or_reset(&spi->dev,
						       ad7476_reg_disable,
						       reg);
			if (ret)
				return ret;
			st->ref_reg = reg;
		} else {
			/*
			 * Can only get here if device supports both internal
			 * and external reference, but the regulator connected
			 * to the external reference is not connected.
			 * Set the reference regulator pointer to NULL to
			 * indicate this.
			 */
			st->ref_reg = NULL;
		}
	}

	if (st->chip_info->has_vdrive) {
		ret = devm_regulator_get_enable(&spi->dev, "vdrive");
		if (ret)
			return ret;
	}

	st->convst_gpio = devm_gpiod_get_optional(&spi->dev,
						  "adi,conversion-start",
						  GPIOD_OUT_LOW);
	if (IS_ERR(st->convst_gpio))
		return PTR_ERR(st->convst_gpio);

	st->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channel;
	indio_dev->num_channels = 2;
	indio_dev->info = &ad7476_info;

	if (st->convst_gpio)
		indio_dev->channels = st->chip_info->convst_channel;
	/* Setup default message */

	st->xfer.rx_buf = &st->data;
	st->xfer.len = st->chip_info->channel[0].scan_type.storagebits / 8;

	spi_message_init(&st->msg);
	spi_message_add_tail(&st->xfer, &st->msg);

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev, NULL,
					      &ad7476_trigger_handler, NULL);
	if (ret)
		return ret;

	if (st->chip_info->reset)
		st->chip_info->reset(st);

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad7476_id[] = {
	{ "ad7091", ID_AD7091 },
	{ "ad7091r", ID_AD7091R },
	{ "ad7273", ID_AD7273 },
	{ "ad7274", ID_AD7274 },
	{ "ad7276", ID_AD7276},
	{ "ad7277", ID_AD7277 },
	{ "ad7278", ID_AD7278 },
	{ "ad7466", ID_AD7466 },
	{ "ad7467", ID_AD7467 },
	{ "ad7468", ID_AD7468 },
	{ "ad7475", ID_AD7475 },
	{ "ad7476", ID_AD7466 },
	{ "ad7476a", ID_AD7466 },
	{ "ad7477", ID_AD7467 },
	{ "ad7477a", ID_AD7467 },
	{ "ad7478", ID_AD7468 },
	{ "ad7478a", ID_AD7468 },
	{ "ad7495", ID_AD7495 },
	{ "ad7910", ID_AD7467 },
	{ "ad7920", ID_AD7466 },
	{ "ad7940", ID_AD7940 },
	{ "adc081s", ID_ADC081S },
	{ "adc101s", ID_ADC101S },
	{ "adc121s", ID_ADC121S },
	{ "ads7866", ID_ADS7866 },
	{ "ads7867", ID_ADS7867 },
	{ "ads7868", ID_ADS7868 },
	{ "ltc2314-14", ID_LTC2314_14 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7476_id);

static struct spi_driver ad7476_driver = {
	.driver = {
		.name	= "ad7476",
	},
	.probe		= ad7476_probe,
	.id_table	= ad7476_id,
};
module_spi_driver(ad7476_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7476 and similar 1-channel ADCs");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices AD7466/7/8 AD7476/5/7/8 (A) SPI ADC driver
 * TI ADC081S/ADC101S/ADC121S 8/10/12-bit SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/bitops.h>
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
	unsigned int			int_vref_mv;
	struct iio_chan_spec		channel[2];
	void (*reset)(struct ad7476_state *);
	void (*conversion_pre_op)(struct ad7476_state *st);
	void (*conversion_post_op)(struct ad7476_state *st);
	bool				has_vref;
	bool				has_vdrive;
	bool				convstart_required;
};

struct ad7476_state {
	struct spi_device		*spi;
	const struct ad7476_chip_info	*chip_info;
	struct gpio_desc		*convst_gpio;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	struct iio_chan_spec		channel[2];
	int				scale_mv;
	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 * Make the buffer large enough for one 16 bit sample and one 64 bit
	 * aligned 64 bit timestamp.
	 */
	unsigned char data[ALIGN(2, sizeof(s64)) + sizeof(s64)] __aligned(IIO_DMA_MINALIGN);
};

static void ad7091_convst(struct ad7476_state *st)
{
	if (!st->convst_gpio)
		return;

	gpiod_set_value_cansleep(st->convst_gpio, 0);
	udelay(1); /* CONVST pulse width: 10 ns min */
	gpiod_set_value_cansleep(st->convst_gpio, 1);
	udelay(1); /* Conversion time: 650 ns max */
}

static void bd79105_convst_disable(struct ad7476_state *st)
{
	gpiod_set_value_cansleep(st->convst_gpio, 0);
}

static void bd79105_convst_enable(struct ad7476_state *st)
{
	gpiod_set_value_cansleep(st->convst_gpio, 1);
	/* Worst case, 2790 ns required for conversion */
	ndelay(2790);
}

static irqreturn_t ad7476_trigger_handler(int irq, void  *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7476_state *st = iio_priv(indio_dev);
	int b_sent;

	if (st->chip_info->conversion_pre_op)
		st->chip_info->conversion_pre_op(st);

	b_sent = spi_sync(st->spi, &st->msg);
	if (b_sent < 0)
		goto done;

	iio_push_to_buffers_with_ts(indio_dev, st->data, sizeof(st->data),
				    iio_get_time_ns(indio_dev));
done:
	if (st->chip_info->conversion_post_op)
		st->chip_info->conversion_post_op(st);
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

	if (st->chip_info->conversion_pre_op)
		st->chip_info->conversion_pre_op(st);

	ret = spi_sync(st->spi, &st->msg);
	if (ret)
		return ret;

	if (st->chip_info->conversion_post_op)
		st->chip_info->conversion_post_op(st);

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

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = ad7476_scan_direct(st);
		iio_device_release_direct(indio_dev);

		if (ret < 0)
			return ret;
		*val = (ret >> chan->scan_type.shift) &
			GENMASK(chan->scan_type.realbits - 1, 0);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->scale_mv;
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
#define ADS786X_CHAN(bits) _AD7476_CHAN((bits), 12 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))

static const struct ad7476_chip_info ad7091_chip_info = {
	.channel[0] = AD7091R_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	.conversion_pre_op = ad7091_convst,
	.reset = ad7091_reset,
};

static const struct ad7476_chip_info ad7091r_chip_info = {
	.channel[0] = AD7091R_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	.conversion_pre_op = ad7091_convst,
	.int_vref_mv = 2500,
	.has_vref = true,
	.reset = ad7091_reset,
};

static const struct ad7476_chip_info ad7273_chip_info = {
	.channel[0] = AD7940_CHAN(10),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	.has_vref = true,
};

static const struct ad7476_chip_info ad7274_chip_info = {
	.channel[0] = AD7940_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	.has_vref = true,
};

static const struct ad7476_chip_info ad7276_chip_info = {
	.channel[0] = AD7940_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ad7277_chip_info = {
	.channel[0] = AD7940_CHAN(10),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ad7278_chip_info = {
	.channel[0] = AD7940_CHAN(8),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ad7466_chip_info = {
	.channel[0] = AD7476_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ad7467_chip_info = {
	.channel[0] = AD7476_CHAN(10),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ad7468_chip_info = {
	.channel[0] = AD7476_CHAN(8),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ad7475_chip_info = {
	.channel[0] = AD7476_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	.has_vref = true,
	.has_vdrive = true,
};

static const struct ad7476_chip_info ad7495_chip_info = {
	.channel[0] = AD7476_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	.int_vref_mv = 2500,
	.has_vdrive = true,
};

static const struct ad7476_chip_info ad7940_chip_info = {
	.channel[0] = AD7940_CHAN(14),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info adc081s_chip_info = {
	.channel[0] = ADC081S_CHAN(8),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info adc101s_chip_info = {
	.channel[0] = ADC081S_CHAN(10),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info adc121s_chip_info = {
	.channel[0] = ADC081S_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ads7866_chip_info = {
	.channel[0] = ADS786X_CHAN(12),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ads7867_chip_info = {
	.channel[0] = ADS786X_CHAN(10),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ads7868_chip_info = {
	.channel[0] = ADS786X_CHAN(8),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct ad7476_chip_info ltc2314_14_chip_info = {
	.channel[0] = AD7940_CHAN(14),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	.has_vref = true,
};

static const struct ad7476_chip_info bd79105_chip_info = {
	.channel[0] = AD7091R_CHAN(16),
	.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	/*
	 * The BD79105 starts ADC data conversion when the CONVSTART line is
	 * set HIGH. The CONVSTART must be kept HIGH until the data has been
	 * read from the ADC.
	 */
	.conversion_pre_op = bd79105_convst_enable,
	.conversion_post_op = bd79105_convst_disable,
	/* BD79105 won't do conversion without convstart */
	.convstart_required = true,
	.has_vref = true,
	.has_vdrive = true,
};

static const struct iio_info ad7476_info = {
	.read_raw = &ad7476_read_raw,
};

static int ad7476_probe(struct spi_device *spi)
{
	struct ad7476_state *st;
	struct iio_dev *indio_dev;
	unsigned int i;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return -ENODEV;

	/* Use VCC for reference voltage if vref / internal vref aren't used */
	if (!st->chip_info->int_vref_mv && !st->chip_info->has_vref) {
		ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vcc");
		if (ret < 0)
			return ret;
		st->scale_mv = ret / 1000;
	} else {
		ret = devm_regulator_get_enable(&spi->dev, "vcc");
		if (ret < 0)
			return ret;
	}

	if (st->chip_info->has_vref) {
		ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vref");
		if (ret < 0) {
			/* Vref is optional if a device has an internal reference */
			if (!st->chip_info->int_vref_mv || ret != -ENODEV)
				return ret;
		} else {
			st->scale_mv = ret / 1000;
		}
	}

	if (!st->scale_mv)
		st->scale_mv = st->chip_info->int_vref_mv;

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

	if (st->chip_info->convstart_required && !st->convst_gpio)
		return dev_err_probe(&spi->dev, -EINVAL, "No convstart GPIO\n");

	/*
	 * This will never happen. Unless someone changes the channel specs
	 * in this driver. And if someone does, without changing the loop
	 * below, then we'd better immediately produce a big fat error, before
	 * the change proceeds from that developer's table.
	 */
	static_assert(ARRAY_SIZE(st->channel) == ARRAY_SIZE(st->chip_info->channel));
	for (i = 0; i < ARRAY_SIZE(st->channel); i++) {
		st->channel[i] = st->chip_info->channel[i];
		if (st->convst_gpio)
			__set_bit(IIO_CHAN_INFO_RAW,
				  &st->channel[i].info_mask_separate);
	}

	st->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->channel;
	indio_dev->num_channels = ARRAY_SIZE(st->channel);
	indio_dev->info = &ad7476_info;

	/* Setup default message */

	st->xfer.rx_buf = &st->data;
	st->xfer.len = indio_dev->channels[0].scan_type.storagebits / 8;

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
	{ "ad7091", (kernel_ulong_t)&ad7091_chip_info },
	{ "ad7091r", (kernel_ulong_t)&ad7091r_chip_info },
	{ "ad7273", (kernel_ulong_t)&ad7273_chip_info },
	{ "ad7274", (kernel_ulong_t)&ad7274_chip_info },
	{ "ad7276", (kernel_ulong_t)&ad7276_chip_info },
	{ "ad7277", (kernel_ulong_t)&ad7277_chip_info },
	{ "ad7278", (kernel_ulong_t)&ad7278_chip_info },
	{ "ad7466", (kernel_ulong_t)&ad7466_chip_info },
	{ "ad7467", (kernel_ulong_t)&ad7467_chip_info },
	{ "ad7468", (kernel_ulong_t)&ad7468_chip_info },
	{ "ad7475", (kernel_ulong_t)&ad7475_chip_info },
	{ "ad7476", (kernel_ulong_t)&ad7466_chip_info },
	{ "ad7476a", (kernel_ulong_t)&ad7466_chip_info },
	{ "ad7477", (kernel_ulong_t)&ad7467_chip_info },
	{ "ad7477a", (kernel_ulong_t)&ad7467_chip_info },
	{ "ad7478", (kernel_ulong_t)&ad7468_chip_info },
	{ "ad7478a", (kernel_ulong_t)&ad7468_chip_info },
	{ "ad7495", (kernel_ulong_t)&ad7495_chip_info },
	{ "ad7910", (kernel_ulong_t)&ad7467_chip_info },
	{ "ad7920", (kernel_ulong_t)&ad7466_chip_info },
	{ "ad7940", (kernel_ulong_t)&ad7940_chip_info },
	{ "adc081s", (kernel_ulong_t)&adc081s_chip_info },
	{ "adc101s", (kernel_ulong_t)&adc101s_chip_info },
	{ "adc121s", (kernel_ulong_t)&adc121s_chip_info },
	{ "ads7866", (kernel_ulong_t)&ads7866_chip_info },
	{ "ads7867", (kernel_ulong_t)&ads7867_chip_info },
	{ "ads7868", (kernel_ulong_t)&ads7868_chip_info },
	{ "bd79105", (kernel_ulong_t)&bd79105_chip_info },
	/*
	 * The ROHM BU79100G is identical to the TI's ADS7866 from the software
	 * point of view. The binding document mandates the ADS7866 to be
	 * marked as a fallback for the BU79100G, but we still need the SPI ID
	 * here to make the module loading work.
	 */
	{ "bu79100g", (kernel_ulong_t)&ads7866_chip_info },
	{ "ltc2314-14", (kernel_ulong_t)&ltc2314_14_chip_info },
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

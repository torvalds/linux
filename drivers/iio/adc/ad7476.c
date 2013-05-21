/*
 * AD7466/7/8 AD7476/5/7/8 (A) SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define RES_MASK(bits)	((1 << (bits)) - 1)

struct ad7476_state;

struct ad7476_chip_info {
	unsigned int			int_vref_uv;
	struct iio_chan_spec		channel[2];
	void (*reset)(struct ad7476_state *);
};

struct ad7476_state {
	struct spi_device		*spi;
	const struct ad7476_chip_info	*chip_info;
	struct regulator		*reg;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 * Make the buffer large enough for one 16 bit sample and one 64 bit
	 * aligned 64 bit timestamp.
	 */
	unsigned char data[ALIGN(2, sizeof(s64)) + sizeof(s64)]
			____cacheline_aligned;
};

enum ad7476_supported_device_ids {
	ID_AD7091R,
	ID_AD7276,
	ID_AD7277,
	ID_AD7278,
	ID_AD7466,
	ID_AD7467,
	ID_AD7468,
	ID_AD7495,
	ID_AD7940,
};

static irqreturn_t ad7476_trigger_handler(int irq, void  *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7476_state *st = iio_priv(indio_dev);
	s64 time_ns;
	int b_sent;

	b_sent = spi_sync(st->spi, &st->msg);
	if (b_sent < 0)
		goto done;

	time_ns = iio_get_time_ns();

	if (indio_dev->scan_timestamp)
		((s64 *)st->data)[1] = time_ns;

	iio_push_to_buffers(indio_dev, st->data);
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
		mutex_lock(&indio_dev->mlock);
		if (iio_buffer_enabled(indio_dev))
			ret = -EBUSY;
		else
			ret = ad7476_scan_direct(st);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;
		*val = (ret >> st->chip_info->channel[0].scan_type.shift) &
			RES_MASK(st->chip_info->channel[0].scan_type.realbits);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (!st->chip_info->int_vref_uv) {
			scale_uv = regulator_get_voltage(st->reg);
			if (scale_uv < 0)
				return scale_uv;
		} else {
			scale_uv = st->chip_info->int_vref_uv;
		}
		scale_uv >>= chan->scan_type.realbits;
		*val =  scale_uv / 1000;
		*val2 = (scale_uv % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
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

#define AD7476_CHAN(bits) _AD7476_CHAN((bits), 13 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))
#define AD7940_CHAN(bits) _AD7476_CHAN((bits), 15 - (bits), \
		BIT(IIO_CHAN_INFO_RAW))
#define AD7091R_CHAN(bits) _AD7476_CHAN((bits), 16 - (bits), 0)

static const struct ad7476_chip_info ad7476_chip_info_tbl[] = {
	[ID_AD7091R] = {
		.channel[0] = AD7091R_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.reset = ad7091_reset,
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
	[ID_AD7495] = {
		.channel[0] = AD7476_CHAN(12),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.int_vref_uv = 2500000,
	},
	[ID_AD7940] = {
		.channel[0] = AD7940_CHAN(14),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
};

static const struct iio_info ad7476_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &ad7476_read_raw,
};

static int ad7476_probe(struct spi_device *spi)
{
	struct ad7476_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	st->chip_info =
		&ad7476_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	st->reg = regulator_get(&spi->dev, "vcc");
	if (IS_ERR(st->reg)) {
		ret = PTR_ERR(st->reg);
		goto error_free_dev;
	}

	ret = regulator_enable(st->reg);
	if (ret)
		goto error_put_reg;

	spi_set_drvdata(spi, indio_dev);

	st->spi = spi;

	/* Establish that the iio_dev is a child of the spi device */
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channel;
	indio_dev->num_channels = 2;
	indio_dev->info = &ad7476_info;
	/* Setup default message */

	st->xfer.rx_buf = &st->data;
	st->xfer.len = st->chip_info->channel[0].scan_type.storagebits / 8;

	spi_message_init(&st->msg);
	spi_message_add_tail(&st->xfer, &st->msg);

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
			&ad7476_trigger_handler, NULL);
	if (ret)
		goto error_disable_reg;

	if (st->chip_info->reset)
		st->chip_info->reset(st);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_ring_unregister;
	return 0;

error_ring_unregister:
	iio_triggered_buffer_cleanup(indio_dev);
error_disable_reg:
	regulator_disable(st->reg);
error_put_reg:
	regulator_put(st->reg);
error_free_dev:
	iio_device_free(indio_dev);

error_ret:
	return ret;
}

static int ad7476_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7476_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	regulator_disable(st->reg);
	regulator_put(st->reg);
	iio_device_free(indio_dev);

	return 0;
}

static const struct spi_device_id ad7476_id[] = {
	{"ad7091r", ID_AD7091R},
	{"ad7273", ID_AD7277},
	{"ad7274", ID_AD7276},
	{"ad7276", ID_AD7276},
	{"ad7277", ID_AD7277},
	{"ad7278", ID_AD7278},
	{"ad7466", ID_AD7466},
	{"ad7467", ID_AD7467},
	{"ad7468", ID_AD7468},
	{"ad7475", ID_AD7466},
	{"ad7476", ID_AD7466},
	{"ad7476a", ID_AD7466},
	{"ad7477", ID_AD7467},
	{"ad7477a", ID_AD7467},
	{"ad7478", ID_AD7468},
	{"ad7478a", ID_AD7468},
	{"ad7495", ID_AD7495},
	{"ad7910", ID_AD7467},
	{"ad7920", ID_AD7466},
	{"ad7940", ID_AD7940},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7476_id);

static struct spi_driver ad7476_driver = {
	.driver = {
		.name	= "ad7476",
		.owner	= THIS_MODULE,
	},
	.probe		= ad7476_probe,
	.remove		= ad7476_remove,
	.id_table	= ad7476_id,
};
module_spi_driver(ad7476_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7476 and similar 1-channel ADCs");
MODULE_LICENSE("GPL v2");

/*
 * AD7887 SPI ADC driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
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
#include <linux/interrupt.h>
#include <linux/bitops.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>

#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/platform_data/ad7887.h>

#define AD7887_REF_DIS		BIT(5)	/* on-chip reference disable */
#define AD7887_DUAL		BIT(4)	/* dual-channel mode */
#define AD7887_CH_AIN1		BIT(3)	/* convert on channel 1, DUAL=1 */
#define AD7887_CH_AIN0		0	/* convert on channel 0, DUAL=0,1 */
#define AD7887_PM_MODE1		0	/* CS based shutdown */
#define AD7887_PM_MODE2		1	/* full on */
#define AD7887_PM_MODE3		2	/* auto shutdown after conversion */
#define AD7887_PM_MODE4		3	/* standby mode */

enum ad7887_channels {
	AD7887_CH0,
	AD7887_CH0_CH1,
	AD7887_CH1,
};

/**
 * struct ad7887_chip_info - chip specifc information
 * @int_vref_mv:	the internal reference voltage
 * @channel:		channel specification
 */
struct ad7887_chip_info {
	u16				int_vref_mv;
	struct iio_chan_spec		channel[3];
};

struct ad7887_state {
	struct spi_device		*spi;
	const struct ad7887_chip_info	*chip_info;
	struct regulator		*reg;
	struct spi_transfer		xfer[4];
	struct spi_message		msg[3];
	struct spi_message		*ring_msg;
	unsigned char			tx_cmd_buf[4];

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 * Buffer needs to be large enough to hold two 16 bit samples and a
	 * 64 bit aligned 64 bit timestamp.
	 */
	unsigned char data[ALIGN(4, sizeof(s64)) + sizeof(s64)]
		____cacheline_aligned;
};

enum ad7887_supported_device_ids {
	ID_AD7887
};

static int ad7887_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7887_state *st = iio_priv(indio_dev);

	/* We know this is a single long so can 'cheat' */
	switch (*indio_dev->active_scan_mask) {
	case (1 << 0):
		st->ring_msg = &st->msg[AD7887_CH0];
		break;
	case (1 << 1):
		st->ring_msg = &st->msg[AD7887_CH1];
		/* Dummy read: push CH1 setting down to hardware */
		spi_sync(st->spi, st->ring_msg);
		break;
	case ((1 << 1) | (1 << 0)):
		st->ring_msg = &st->msg[AD7887_CH0_CH1];
		break;
	}

	return 0;
}

static int ad7887_ring_postdisable(struct iio_dev *indio_dev)
{
	struct ad7887_state *st = iio_priv(indio_dev);

	/* dummy read: restore default CH0 settin */
	return spi_sync(st->spi, &st->msg[AD7887_CH0]);
}

/**
 * ad7887_trigger_handler() bh of trigger launched polling to ring buffer
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 **/
static irqreturn_t ad7887_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7887_state *st = iio_priv(indio_dev);
	int b_sent;

	b_sent = spi_sync(st->spi, st->ring_msg);
	if (b_sent)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, st->data,
		iio_get_time_ns());
done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops ad7887_ring_setup_ops = {
	.preenable = &ad7887_ring_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &ad7887_ring_postdisable,
};

static int ad7887_scan_direct(struct ad7887_state *st, unsigned ch)
{
	int ret = spi_sync(st->spi, &st->msg[ch]);
	if (ret)
		return ret;

	return (st->data[(ch * 2)] << 8) | st->data[(ch * 2) + 1];
}

static int ad7887_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad7887_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = ad7887_scan_direct(st, chan->address);
		iio_device_release_direct_mode(indio_dev);

		if (ret < 0)
			return ret;
		*val = ret >> chan->scan_type.shift;
		*val &= GENMASK(chan->scan_type.realbits - 1, 0);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (st->reg) {
			*val = regulator_get_voltage(st->reg);
			if (*val < 0)
				return *val;
			*val /= 1000;
		} else {
			*val = st->chip_info->int_vref_mv;
		}

		*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}


static const struct ad7887_chip_info ad7887_chip_info_tbl[] = {
	/*
	 * More devices added in future
	 */
	[ID_AD7887] = {
		.channel[0] = {
			.type = IIO_VOLTAGE,
			.indexed = 1,
			.channel = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
			.address = 1,
			.scan_index = 1,
			.scan_type = {
				.sign = 'u',
				.realbits = 12,
				.storagebits = 16,
				.shift = 0,
				.endianness = IIO_BE,
			},
		},
		.channel[1] = {
			.type = IIO_VOLTAGE,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
			.address = 0,
			.scan_index = 0,
			.scan_type = {
				.sign = 'u',
				.realbits = 12,
				.storagebits = 16,
				.shift = 0,
				.endianness = IIO_BE,
			},
		},
		.channel[2] = IIO_CHAN_SOFT_TIMESTAMP(2),
		.int_vref_mv = 2500,
	},
};

static const struct iio_info ad7887_info = {
	.read_raw = &ad7887_read_raw,
	.driver_module = THIS_MODULE,
};

static int ad7887_probe(struct spi_device *spi)
{
	struct ad7887_platform_data *pdata = spi->dev.platform_data;
	struct ad7887_state *st;
	struct iio_dev *indio_dev;
	uint8_t mode;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	if (!pdata || !pdata->use_onchip_ref) {
		st->reg = devm_regulator_get(&spi->dev, "vref");
		if (IS_ERR(st->reg))
			return PTR_ERR(st->reg);

		ret = regulator_enable(st->reg);
		if (ret)
			return ret;
	}

	st->chip_info =
		&ad7887_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;

	/* Estabilish that the iio_dev is a child of the spi device */
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad7887_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* Setup default message */

	mode = AD7887_PM_MODE4;
	if (!pdata || !pdata->use_onchip_ref)
		mode |= AD7887_REF_DIS;
	if (pdata && pdata->en_dual)
		mode |= AD7887_DUAL;

	st->tx_cmd_buf[0] = AD7887_CH_AIN0 | mode;

	st->xfer[0].rx_buf = &st->data[0];
	st->xfer[0].tx_buf = &st->tx_cmd_buf[0];
	st->xfer[0].len = 2;

	spi_message_init(&st->msg[AD7887_CH0]);
	spi_message_add_tail(&st->xfer[0], &st->msg[AD7887_CH0]);

	if (pdata && pdata->en_dual) {
		st->tx_cmd_buf[2] = AD7887_CH_AIN1 | mode;

		st->xfer[1].rx_buf = &st->data[0];
		st->xfer[1].tx_buf = &st->tx_cmd_buf[2];
		st->xfer[1].len = 2;

		st->xfer[2].rx_buf = &st->data[2];
		st->xfer[2].tx_buf = &st->tx_cmd_buf[0];
		st->xfer[2].len = 2;

		spi_message_init(&st->msg[AD7887_CH0_CH1]);
		spi_message_add_tail(&st->xfer[1], &st->msg[AD7887_CH0_CH1]);
		spi_message_add_tail(&st->xfer[2], &st->msg[AD7887_CH0_CH1]);

		st->xfer[3].rx_buf = &st->data[2];
		st->xfer[3].tx_buf = &st->tx_cmd_buf[2];
		st->xfer[3].len = 2;

		spi_message_init(&st->msg[AD7887_CH1]);
		spi_message_add_tail(&st->xfer[3], &st->msg[AD7887_CH1]);

		indio_dev->channels = st->chip_info->channel;
		indio_dev->num_channels = 3;
	} else {
		indio_dev->channels = &st->chip_info->channel[1];
		indio_dev->num_channels = 2;
	}

	ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
			&ad7887_trigger_handler, &ad7887_ring_setup_ops);
	if (ret)
		goto error_disable_reg;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unregister_ring;

	return 0;
error_unregister_ring:
	iio_triggered_buffer_cleanup(indio_dev);
error_disable_reg:
	if (st->reg)
		regulator_disable(st->reg);

	return ret;
}

static int ad7887_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7887_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (st->reg)
		regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad7887_id[] = {
	{"ad7887", ID_AD7887},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7887_id);

static struct spi_driver ad7887_driver = {
	.driver = {
		.name	= "ad7887",
	},
	.probe		= ad7887_probe,
	.remove		= ad7887_remove,
	.id_table	= ad7887_id,
};
module_spi_driver(ad7887_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7887 ADC");
MODULE_LICENSE("GPL v2");

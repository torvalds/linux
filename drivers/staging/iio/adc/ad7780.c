/*
 * AD7780/AD7781 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "ad7780.h"

#define AD7780_RDY	(1 << 7)
#define AD7780_FILTER	(1 << 6)
#define AD7780_ERR	(1 << 5)
#define AD7780_ID1	(1 << 4)
#define AD7780_ID0	(1 << 3)
#define AD7780_GAIN	(1 << 2)
#define AD7780_PAT1	(1 << 1)
#define AD7780_PAT0	(1 << 0)

struct ad7780_chip_info {
	struct iio_chan_spec		channel;
};

struct ad7780_state {
	struct spi_device		*spi;
	const struct ad7780_chip_info	*chip_info;
	struct regulator		*reg;
	struct ad7780_platform_data	*pdata;
	wait_queue_head_t		wq_data_avail;
	bool				done;
	u16				int_vref_mv;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	unsigned int			data ____cacheline_aligned;
};

enum ad7780_supported_device_ids {
	ID_AD7780,
	ID_AD7781,
};

static int ad7780_read(struct ad7780_state *st, int *val)
{
	int ret;

	spi_bus_lock(st->spi->master);

	enable_irq(st->spi->irq);
	st->done = false;
	gpio_set_value(st->pdata->gpio_pdrst, 1);

	ret = wait_event_interruptible(st->wq_data_avail, st->done);
	disable_irq_nosync(st->spi->irq);
	if (ret)
		goto out;

	ret = spi_sync_locked(st->spi, &st->msg);
	*val = be32_to_cpu(st->data);
out:
	gpio_set_value(st->pdata->gpio_pdrst, 0);
	spi_bus_unlock(st->spi->master);

	return ret;
}

static int ad7780_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad7780_state *st = iio_priv(indio_dev);
	struct iio_chan_spec channel = st->chip_info->channel;
	int ret, smpl = 0;
	unsigned long scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = ad7780_read(st, &smpl);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;

		if ((smpl & AD7780_ERR) ||
			!((smpl & AD7780_PAT0) && !(smpl & AD7780_PAT1)))
			return -EIO;

		*val = (smpl >> channel.scan_type.shift) &
			((1 << (channel.scan_type.realbits)) - 1);
		*val -= (1 << (channel.scan_type.realbits - 1));

		if (!(smpl & AD7780_GAIN))
			*val *= 128;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->int_vref_mv * 100000)
			>> (channel.scan_type.realbits - 1);
		*val =  scale_uv / 100000;
		*val2 = (scale_uv % 100000) * 10;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static const struct ad7780_chip_info ad7780_chip_info_tbl[] = {
	[ID_AD7780] = {
		.channel = {
			.type = IIO_VOLTAGE,
			.indexed = 1,
			.channel = 0,
			.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
			IIO_CHAN_INFO_SCALE_SHARED_BIT,
			.scan_type = {
				.sign = 's',
				.realbits = 24,
				.storagebits = 32,
				.shift = 8,
			},
		},
	},
	[ID_AD7781] = {
		.channel = {
			.type = IIO_VOLTAGE,
			.indexed = 1,
			.channel = 0,
			.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
			IIO_CHAN_INFO_SCALE_SHARED_BIT,
			.scan_type = {
				.sign = 's',
				.realbits = 20,
				.storagebits = 32,
				.shift = 12,
			},
		},
	},
};

/**
 *  Interrupt handler
 */
static irqreturn_t ad7780_interrupt(int irq, void *dev_id)
{
	struct ad7780_state *st = dev_id;

	st->done = true;
	wake_up_interruptible(&st->wq_data_avail);

	return IRQ_HANDLED;
};

static const struct iio_info ad7780_info = {
	.read_raw = &ad7780_read_raw,
	.driver_module = THIS_MODULE,
};

static int __devinit ad7780_probe(struct spi_device *spi)
{
	struct ad7780_platform_data *pdata = spi->dev.platform_data;
	struct ad7780_state *st;
	struct iio_dev *indio_dev;
	int ret, voltage_uv = 0;

	if (!pdata) {
		dev_dbg(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(st->reg);
	}

	st->chip_info =
		&ad7780_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	st->pdata = pdata;

	if (pdata && pdata->vref_mv)
		st->int_vref_mv = pdata->vref_mv;
	else if (voltage_uv)
		st->int_vref_mv = voltage_uv / 1000;
	else
		dev_warn(&spi->dev, "reference voltage unspecified\n");

	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;
	indio_dev->info = &ad7780_info;

	init_waitqueue_head(&st->wq_data_avail);

	/* Setup default message */

	st->xfer.rx_buf = &st->data;
	st->xfer.len = st->chip_info->channel.scan_type.storagebits / 8;

	spi_message_init(&st->msg);
	spi_message_add_tail(&st->xfer, &st->msg);

	ret = gpio_request_one(st->pdata->gpio_pdrst, GPIOF_OUT_INIT_LOW,
			       "AD7780 /PDRST");
	if (ret) {
		dev_err(&spi->dev, "failed to request GPIO PDRST\n");
		goto error_disable_reg;
	}

	ret = request_irq(spi->irq, ad7780_interrupt,
		IRQF_TRIGGER_FALLING, spi_get_device_id(spi)->name, st);
	if (ret)
		goto error_free_gpio;

	disable_irq(spi->irq);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(spi->irq, st);
error_free_gpio:
	gpio_free(st->pdata->gpio_pdrst);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);

	iio_device_free(indio_dev);

	return ret;
}

static int ad7780_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7780_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	free_irq(spi->irq, st);
	gpio_free(st->pdata->gpio_pdrst);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_device_free(indio_dev);

	return 0;
}

static const struct spi_device_id ad7780_id[] = {
	{"ad7780", ID_AD7780},
	{"ad7781", ID_AD7781},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7780_id);

static struct spi_driver ad7780_driver = {
	.driver = {
		.name	= "ad7780",
		.owner	= THIS_MODULE,
	},
	.probe		= ad7780_probe,
	.remove		= __devexit_p(ad7780_remove),
	.id_table	= ad7780_id,
};
module_spi_driver(ad7780_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7780/1 ADC");
MODULE_LICENSE("GPL v2");

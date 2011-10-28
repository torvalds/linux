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

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_generic.h"
#include "adc.h"

#include "ad7476.h"

static int ad7476_scan_direct(struct ad7476_state *st)
{
	int ret;

	ret = spi_sync(st->spi, &st->msg);
	if (ret)
		return ret;

	return (st->data[0] << 8) | st->data[1];
}

static int ad7476_read_raw(struct iio_dev *dev_info,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad7476_state *st = dev_info->dev_data;
	unsigned int scale_uv;

	switch (m) {
	case 0:
		mutex_lock(&dev_info->mlock);
		if (iio_ring_enabled(dev_info))
			ret = ad7476_scan_from_ring(st);
		else
			ret = ad7476_scan_direct(st);
		mutex_unlock(&dev_info->mlock);

		if (ret < 0)
			return ret;
		*val = (ret >> st->chip_info->channel[0].scan_type.shift) &
			RES_MASK(st->chip_info->channel[0].scan_type.realbits);
		return IIO_VAL_INT;
	case (1 << IIO_CHAN_INFO_SCALE_SHARED):
		scale_uv = (st->int_vref_mv * 1000)
			>> st->chip_info->channel[0].scan_type.realbits;
		*val =  scale_uv/1000;
		*val2 = (scale_uv%1000)*1000;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static const struct ad7476_chip_info ad7476_chip_info_tbl[] = {
	[ID_AD7466] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 12, 16, 0), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7467] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 10, 16, 2), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7468] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1 , 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 8, 16, 4), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7475] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 12, 16, 0), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7476] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 12, 16, 0), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7477] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 10, 16, 2), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7478] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 8, 16, 4), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
	},
	[ID_AD7495] = {
		.channel[0] = IIO_CHAN(IIO_IN, 0, 1, 0, NULL, 0, 0,
				       (1 << IIO_CHAN_INFO_SCALE_SHARED),
				       0, 0, IIO_ST('u', 12, 16, 0), 0),
		.channel[1] = IIO_CHAN_SOFT_TIMESTAMP(1),
		.int_vref_mv = 2500,
	},
};

static const struct iio_info ad7476_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &ad7476_read_raw,
};

static int __devinit ad7476_probe(struct spi_device *spi)
{
	struct ad7476_platform_data *pdata = spi->dev.platform_data;
	struct ad7476_state *st;
	int ret, voltage_uv = 0;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	st->reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(st->reg);
	}

	st->chip_info =
		&ad7476_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	if (st->chip_info->int_vref_mv)
		st->int_vref_mv = st->chip_info->int_vref_mv;
	else if (pdata && pdata->vref_mv)
		st->int_vref_mv = pdata->vref_mv;
	else if (voltage_uv)
		st->int_vref_mv = voltage_uv / 1000;
	else
		dev_warn(&spi->dev, "reference voltage unspecified\n");

	spi_set_drvdata(spi, st);

	st->spi = spi;

	st->indio_dev = iio_allocate_device(0);
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}

	/* Establish that the iio_dev is a child of the spi device */
	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->name = spi_get_device_id(spi)->name;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->modes = INDIO_DIRECT_MODE;
	st->indio_dev->channels = st->chip_info->channel;
	st->indio_dev->num_channels = 2;
	st->indio_dev->info = &ad7476_info;
	/* Setup default message */

	st->xfer.rx_buf = &st->data;
	st->xfer.len = st->chip_info->channel[0].scan_type.storagebits / 8;

	spi_message_init(&st->msg);
	spi_message_add_tail(&st->xfer, &st->msg);

	ret = ad7476_register_ring_funcs_and_init(st->indio_dev);
	if (ret)
		goto error_free_device;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_device;

	ret = iio_ring_buffer_register_ex(st->indio_dev->ring, 0,
					  st->chip_info->channel,
					  ARRAY_SIZE(st->chip_info->channel));
	if (ret)
		goto error_cleanup_ring;
	return 0;

error_cleanup_ring:
	ad7476_ring_cleanup(st->indio_dev);
	iio_device_unregister(st->indio_dev);
error_free_device:
	iio_free_device(st->indio_dev);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);
	kfree(st);
error_ret:
	return ret;
}

static int ad7476_remove(struct spi_device *spi)
{
	struct ad7476_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;
	iio_ring_buffer_unregister(indio_dev->ring);
	ad7476_ring_cleanup(indio_dev);
	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	kfree(st);
	return 0;
}

static const struct spi_device_id ad7476_id[] = {
	{"ad7466", ID_AD7466},
	{"ad7467", ID_AD7467},
	{"ad7468", ID_AD7468},
	{"ad7475", ID_AD7475},
	{"ad7476", ID_AD7476},
	{"ad7476a", ID_AD7476},
	{"ad7477", ID_AD7477},
	{"ad7477a", ID_AD7477},
	{"ad7478", ID_AD7478},
	{"ad7478a", ID_AD7478},
	{"ad7495", ID_AD7495},
	{}
};

static struct spi_driver ad7476_driver = {
	.driver = {
		.name	= "ad7476",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad7476_probe,
	.remove		= __devexit_p(ad7476_remove),
	.id_table	= ad7476_id,
};

static int __init ad7476_init(void)
{
	return spi_register_driver(&ad7476_driver);
}
module_init(ad7476_init);

static void __exit ad7476_exit(void)
{
	spi_unregister_driver(&ad7476_driver);
}
module_exit(ad7476_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7475/6/7/8(A) AD7466/7/8 ADC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ad7476");

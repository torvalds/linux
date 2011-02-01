/*
 * AD7466/7/8 AD7476/5/7/8 (A) SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
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

static ssize_t ad7476_scan(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7476_state *st = dev_info->dev_data;
	int ret;

	mutex_lock(&dev_info->mlock);
	if (iio_ring_enabled(dev_info))
		ret = ad7476_scan_from_ring(st);
	else
		ret = ad7476_scan_direct(st);
	mutex_unlock(&dev_info->mlock);

	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", (ret >> st->chip_info->res_shift) &
		       RES_MASK(st->chip_info->bits));
}
static IIO_DEV_ATTR_IN_RAW(0, ad7476_scan, 0);

static ssize_t ad7476_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	/* Driver currently only support internal vref */
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7476_state *st = iio_dev_get_devdata(dev_info);
	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (st->int_vref_mv * 1000) >> st->chip_info->bits;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}
static IIO_DEVICE_ATTR(in_scale, S_IRUGO, ad7476_show_scale, NULL, 0);

static ssize_t ad7476_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7476_state *st = iio_dev_get_devdata(dev_info);

	return sprintf(buf, "%s\n", spi_get_device_id(st->spi)->name);
}
static IIO_DEVICE_ATTR(name, S_IRUGO, ad7476_show_name, NULL, 0);

static struct attribute *ad7476_attributes[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7476_attribute_group = {
	.attrs = ad7476_attributes,
};

static const struct ad7476_chip_info ad7476_chip_info_tbl[] = {
	[ID_AD7466] = {
		.bits = 12,
		.storagebits = 16,
		.res_shift = 0,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
	[ID_AD7467] = {
		.bits = 10,
		.storagebits = 16,
		.res_shift = 2,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
	[ID_AD7468] = {
		.bits = 8,
		.storagebits = 16,
		.res_shift = 4,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
	[ID_AD7475] = {
		.bits = 12,
		.storagebits = 16,
		.res_shift = 0,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
	[ID_AD7476] = {
		.bits = 12,
		.storagebits = 16,
		.res_shift = 0,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
	[ID_AD7477] = {
		.bits = 10,
		.storagebits = 16,
		.res_shift = 2,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
	[ID_AD7478] = {
		.bits = 8,
		.storagebits = 16,
		.res_shift = 4,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
	[ID_AD7495] = {
		.bits = 12,
		.storagebits = 16,
		.res_shift = 0,
		.int_vref_mv = 2500,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
	},
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

	atomic_set(&st->protect_ring, 0);
	st->spi = spi;

	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}

	/* Estabilish that the iio_dev is a child of the i2c device */
	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->attrs = &ad7476_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	/* Setup default message */

	st->xfer.rx_buf = &st->data;
	st->xfer.len = st->chip_info->storagebits / 8;

	spi_message_init(&st->msg);
	spi_message_add_tail(&st->xfer, &st->msg);

	ret = ad7476_register_ring_funcs_and_init(st->indio_dev);
	if (ret)
		goto error_free_device;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_device;

	ret = iio_ring_buffer_register(st->indio_dev->ring, 0);
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

/*
 * AD5446 SPI DAC driver
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
#include "dac.h"

#include "ad5446.h"

static void ad5446_store_sample(struct ad5446_state *st, unsigned val)
{
	st->data.d16 = cpu_to_be16(AD5446_LOAD |
					(val << st->chip_info->left_shift));
}

static void ad5542_store_sample(struct ad5446_state *st, unsigned val)
{
	st->data.d16 = cpu_to_be16(val << st->chip_info->left_shift);
}

static void ad5620_store_sample(struct ad5446_state *st, unsigned val)
{
	st->data.d16 = cpu_to_be16(AD5620_LOAD |
					(val << st->chip_info->left_shift));
}

static void ad5660_store_sample(struct ad5446_state *st, unsigned val)
{
	val |= AD5660_LOAD;
	st->data.d24[0] = (val >> 16) & 0xFF;
	st->data.d24[1] = (val >> 8) & 0xFF;
	st->data.d24[2] = val & 0xFF;
}

static ssize_t ad5446_write(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad5446_state *st = dev_info->dev_data;
	int ret;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;

	if (val > RES_MASK(st->chip_info->bits)) {
		ret = -EINVAL;
		goto error_ret;
	}

	mutex_lock(&dev_info->mlock);
	st->chip_info->store_sample(st, val);
	ret = spi_sync(st->spi, &st->msg);
	mutex_unlock(&dev_info->mlock);

error_ret:
	return ret ? ret : len;
}

static IIO_DEV_ATTR_OUT_RAW(0, ad5446_write, 0);

static ssize_t ad5446_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_dev_get_devdata(dev_info);
	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (st->vref_mv * 1000) >> st->chip_info->bits;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}
static IIO_DEVICE_ATTR(out_scale, S_IRUGO, ad5446_show_scale, NULL, 0);

static ssize_t ad5446_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_dev_get_devdata(dev_info);

	return sprintf(buf, "%s\n", spi_get_device_id(st->spi)->name);
}
static IIO_DEVICE_ATTR(name, S_IRUGO, ad5446_show_name, NULL, 0);

static struct attribute *ad5446_attributes[] = {
	&iio_dev_attr_out0_raw.dev_attr.attr,
	&iio_dev_attr_out_scale.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5446_attribute_group = {
	.attrs = ad5446_attributes,
};

static const struct ad5446_chip_info ad5446_chip_info_tbl[] = {
	[ID_AD5444] = {
		.bits = 12,
		.storagebits = 16,
		.left_shift = 2,
		.store_sample = ad5446_store_sample,
	},
	[ID_AD5446] = {
		.bits = 14,
		.storagebits = 16,
		.left_shift = 0,
		.store_sample = ad5446_store_sample,
	},
	[ID_AD5542A] = {
		.bits = 16,
		.storagebits = 16,
		.left_shift = 0,
		.store_sample = ad5542_store_sample,
	},
	[ID_AD5512A] = {
		.bits = 12,
		.storagebits = 16,
		.left_shift = 4,
		.store_sample = ad5542_store_sample,
	},
	[ID_AD5620_2500] = {
		.bits = 12,
		.storagebits = 16,
		.left_shift = 2,
		.int_vref_mv = 2500,
		.store_sample = ad5620_store_sample,
	},
	[ID_AD5620_1250] = {
		.bits = 12,
		.storagebits = 16,
		.left_shift = 2,
		.int_vref_mv = 1250,
		.store_sample = ad5620_store_sample,
	},
	[ID_AD5640_2500] = {
		.bits = 14,
		.storagebits = 16,
		.left_shift = 0,
		.int_vref_mv = 2500,
		.store_sample = ad5620_store_sample,
	},
	[ID_AD5640_1250] = {
		.bits = 14,
		.storagebits = 16,
		.left_shift = 0,
		.int_vref_mv = 1250,
		.store_sample = ad5620_store_sample,
	},
	[ID_AD5660_2500] = {
		.bits = 16,
		.storagebits = 24,
		.left_shift = 0,
		.int_vref_mv = 2500,
		.store_sample = ad5660_store_sample,
	},
	[ID_AD5660_1250] = {
		.bits = 16,
		.storagebits = 24,
		.left_shift = 0,
		.int_vref_mv = 1250,
		.store_sample = ad5660_store_sample,
	},
};

static int __devinit ad5446_probe(struct spi_device *spi)
{
	struct ad5446_state *st;
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
		&ad5446_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	spi_set_drvdata(spi, st);

	st->spi = spi;

	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}

	/* Estabilish that the iio_dev is a child of the spi device */
	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->attrs = &ad5446_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	/* Setup default message */

	st->xfer.tx_buf = &st->data;
	st->xfer.len = st->chip_info->storagebits / 8;

	spi_message_init(&st->msg);
	spi_message_add_tail(&st->xfer, &st->msg);

	switch (spi_get_device_id(spi)->driver_data) {
		case ID_AD5620_2500:
		case ID_AD5620_1250:
		case ID_AD5640_2500:
		case ID_AD5640_1250:
		case ID_AD5660_2500:
		case ID_AD5660_1250:
			st->vref_mv = st->chip_info->int_vref_mv;
			break;
		default:
			if (voltage_uv)
				st->vref_mv = voltage_uv / 1000;
			else
				dev_warn(&spi->dev,
					 "reference voltage unspecified\n");
	}

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_device;

	return 0;

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

static int ad5446_remove(struct spi_device *spi)
{
	struct ad5446_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	kfree(st);
	return 0;
}

static const struct spi_device_id ad5446_id[] = {
	{"ad5444", ID_AD5444},
	{"ad5446", ID_AD5446},
	{"ad5542a", ID_AD5542A},
	{"ad5512a", ID_AD5512A},
	{"ad5620-2500", ID_AD5620_2500}, /* AD5620/40/60: */
	{"ad5620-1250", ID_AD5620_1250}, /* part numbers may look differently */
	{"ad5640-2500", ID_AD5640_2500},
	{"ad5640-1250", ID_AD5640_1250},
	{"ad5660-2500", ID_AD5660_2500},
	{"ad5660-1250", ID_AD5660_1250},
	{}
};

static struct spi_driver ad5446_driver = {
	.driver = {
		.name	= "ad5446",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad5446_probe,
	.remove		= __devexit_p(ad5446_remove),
	.id_table	= ad5446_id,
};

static int __init ad5446_init(void)
{
	return spi_register_driver(&ad5446_driver);
}
module_init(ad5446_init);

static void __exit ad5446_exit(void)
{
	spi_unregister_driver(&ad5446_driver);
}
module_exit(ad5446_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5444/AD5446 DAC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ad5446");

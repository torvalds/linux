/*
 * AD7887 SPI ADC driver
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

#include "ad7887.h"

static int ad7887_scan_direct(struct ad7887_state *st, unsigned ch)
{
	int ret = spi_sync(st->spi, &st->msg[ch]);
	if (ret)
		return ret;

	return (st->data[(ch * 2)] << 8) | st->data[(ch * 2) + 1];
}

static ssize_t ad7887_scan(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7887_state *st = dev_info->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;

	mutex_lock(&dev_info->mlock);
	if (iio_ring_enabled(dev_info))
		ret = ad7887_scan_from_ring(st, 1 << this_attr->address);
	else
		ret = ad7887_scan_direct(st, this_attr->address);
	mutex_unlock(&dev_info->mlock);

	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", (ret >> st->chip_info->left_shift) &
		       RES_MASK(st->chip_info->bits));
}
static IIO_DEV_ATTR_IN_RAW(0, ad7887_scan, 0);
static IIO_DEV_ATTR_IN_RAW(1, ad7887_scan, 1);

static ssize_t ad7887_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	/* Driver currently only support internal vref */
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7887_state *st = iio_dev_get_devdata(dev_info);
	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (st->int_vref_mv * 1000) >> st->chip_info->bits;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}
static IIO_DEVICE_ATTR(in_scale, S_IRUGO, ad7887_show_scale, NULL, 0);

static ssize_t ad7887_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7887_state *st = iio_dev_get_devdata(dev_info);

	return sprintf(buf, "%s\n", spi_get_device_id(st->spi)->name);
}
static IIO_DEVICE_ATTR(name, S_IRUGO, ad7887_show_name, NULL, 0);

static struct attribute *ad7887_attributes[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static mode_t ad7887_attr_is_visible(struct kobject *kobj,
				     struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7887_state *st = iio_dev_get_devdata(dev_info);

	mode_t mode = attr->mode;

	if ((attr == &iio_dev_attr_in1_raw.dev_attr.attr) && !st->en_dual)
			mode = 0;

	return mode;
}

static const struct attribute_group ad7887_attribute_group = {
	.attrs = ad7887_attributes,
	.is_visible = ad7887_attr_is_visible,
};

static const struct ad7887_chip_info ad7887_chip_info_tbl[] = {
	/*
	 * More devices added in future
	 */
	[ID_AD7887] = {
		.bits = 12,
		.storagebits = 16,
		.left_shift = 0,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 2500,
	},
};

static int __devinit ad7887_probe(struct spi_device *spi)
{
	struct ad7887_platform_data *pdata = spi->dev.platform_data;
	struct ad7887_state *st;
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
		&ad7887_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	spi_set_drvdata(spi, st);

	atomic_set(&st->protect_ring, 0);
	st->spi = spi;

	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}

	/* Estabilish that the iio_dev is a child of the spi device */
	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->attrs = &ad7887_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	/* Setup default message */

	st->tx_cmd_buf[0] = AD7887_CH_AIN0 | AD7887_PM_MODE4 |
			    ((pdata && pdata->use_onchip_ref) ?
			    0 : AD7887_REF_DIS);

	st->xfer[0].rx_buf = &st->data[0];
	st->xfer[0].tx_buf = &st->tx_cmd_buf[0];
	st->xfer[0].len = 2;

	spi_message_init(&st->msg[AD7887_CH0]);
	spi_message_add_tail(&st->xfer[0], &st->msg[AD7887_CH0]);

	if (pdata && pdata->en_dual) {
		st->tx_cmd_buf[0] |= AD7887_DUAL | AD7887_REF_DIS;

		st->tx_cmd_buf[2] = AD7887_CH_AIN1 | AD7887_DUAL |
				    AD7887_REF_DIS | AD7887_PM_MODE4;
		st->tx_cmd_buf[4] = AD7887_CH_AIN0 | AD7887_DUAL |
				    AD7887_REF_DIS | AD7887_PM_MODE4;
		st->tx_cmd_buf[6] = AD7887_CH_AIN1 | AD7887_DUAL |
				    AD7887_REF_DIS | AD7887_PM_MODE4;

		st->xfer[1].rx_buf = &st->data[0];
		st->xfer[1].tx_buf = &st->tx_cmd_buf[2];
		st->xfer[1].len = 2;

		st->xfer[2].rx_buf = &st->data[2];
		st->xfer[2].tx_buf = &st->tx_cmd_buf[4];
		st->xfer[2].len = 2;

		spi_message_init(&st->msg[AD7887_CH0_CH1]);
		spi_message_add_tail(&st->xfer[1], &st->msg[AD7887_CH0_CH1]);
		spi_message_add_tail(&st->xfer[2], &st->msg[AD7887_CH0_CH1]);

		st->xfer[3].rx_buf = &st->data[0];
		st->xfer[3].tx_buf = &st->tx_cmd_buf[6];
		st->xfer[3].len = 2;

		spi_message_init(&st->msg[AD7887_CH1]);
		spi_message_add_tail(&st->xfer[3], &st->msg[AD7887_CH1]);

		st->en_dual = true;

		if (pdata && pdata->vref_mv)
			st->int_vref_mv = pdata->vref_mv;
		else if (voltage_uv)
			st->int_vref_mv = voltage_uv / 1000;
		else
			dev_warn(&spi->dev, "reference voltage unspecified\n");

	} else {
		if (pdata && pdata->vref_mv)
			st->int_vref_mv = pdata->vref_mv;
		else if (pdata && pdata->use_onchip_ref)
			st->int_vref_mv = st->chip_info->int_vref_mv;
		else
			dev_warn(&spi->dev, "reference voltage unspecified\n");
	}


	ret = ad7887_register_ring_funcs_and_init(st->indio_dev);
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
	ad7887_ring_cleanup(st->indio_dev);
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

static int ad7887_remove(struct spi_device *spi)
{
	struct ad7887_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;
	iio_ring_buffer_unregister(indio_dev->ring);
	ad7887_ring_cleanup(indio_dev);
	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	kfree(st);
	return 0;
}

static const struct spi_device_id ad7887_id[] = {
	{"ad7887", ID_AD7887},
	{}
};

static struct spi_driver ad7887_driver = {
	.driver = {
		.name	= "ad7887",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad7887_probe,
	.remove		= __devexit_p(ad7887_remove),
	.id_table	= ad7887_id,
};

static int __init ad7887_init(void)
{
	return spi_register_driver(&ad7887_driver);
}
module_init(ad7887_init);

static void __exit ad7887_exit(void)
{
	spi_unregister_driver(&ad7887_driver);
}
module_exit(ad7887_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7887 ADC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ad7887");

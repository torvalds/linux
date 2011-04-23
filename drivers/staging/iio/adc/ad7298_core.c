/*
 * AD7298 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_generic.h"
#include "adc.h"

#include "ad7298.h"

static int ad7298_scan_direct(struct ad7298_state *st, unsigned ch)
{
	int ret;
	st->tx_buf[0] = cpu_to_be16(AD7298_WRITE | st->ext_ref |
				   (AD7298_CH(0) >> ch));

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	return be16_to_cpu(st->rx_buf[0]);
}

static ssize_t ad7298_scan(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_state *st = dev_info->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;

	mutex_lock(&dev_info->mlock);
	if (iio_ring_enabled(dev_info))
		ret = ad7298_scan_from_ring(st, this_attr->address);
	else
		ret = ad7298_scan_direct(st, this_attr->address);
	mutex_unlock(&dev_info->mlock);

	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret & RES_MASK(AD7298_BITS));
}

static IIO_DEV_ATTR_IN_RAW(0, ad7298_scan, 0);
static IIO_DEV_ATTR_IN_RAW(1, ad7298_scan, 1);
static IIO_DEV_ATTR_IN_RAW(2, ad7298_scan, 2);
static IIO_DEV_ATTR_IN_RAW(3, ad7298_scan, 3);
static IIO_DEV_ATTR_IN_RAW(4, ad7298_scan, 4);
static IIO_DEV_ATTR_IN_RAW(5, ad7298_scan, 5);
static IIO_DEV_ATTR_IN_RAW(6, ad7298_scan, 6);
static IIO_DEV_ATTR_IN_RAW(7, ad7298_scan, 7);

static ssize_t ad7298_show_temp(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_state *st = iio_dev_get_devdata(dev_info);
	int tmp;

	tmp = cpu_to_be16(AD7298_WRITE | AD7298_TSENSE |
			  AD7298_TAVG | st->ext_ref);

	mutex_lock(&dev_info->mlock);
	spi_write(st->spi, (u8 *)&tmp, 2);
	tmp = 0;
	spi_write(st->spi, (u8 *)&tmp, 2);
	usleep_range(101, 1000); /* sleep > 100us */
	spi_read(st->spi, (u8 *)&tmp, 2);
	mutex_unlock(&dev_info->mlock);

	tmp = be16_to_cpu(tmp) & RES_MASK(AD7298_BITS);

	/*
	 * One LSB of the ADC corresponds to 0.25 deg C.
	 * The temperature reading is in 12-bit twos complement format
	 */

	if (tmp & (1 << (AD7298_BITS - 1))) {
		tmp = (4096 - tmp) * 250;
		tmp -= (2 * tmp);

	} else {
		tmp *= 250; /* temperature in milli degrees Celsius */
	}

	return sprintf(buf, "%d\n", tmp);
}

static IIO_DEVICE_ATTR(temp0_input, S_IRUGO, ad7298_show_temp, NULL, 0);

static ssize_t ad7298_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_state *st = iio_dev_get_devdata(dev_info);
	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (st->int_vref_mv * 1000) >> AD7298_BITS;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}
static IIO_DEVICE_ATTR(in_scale, S_IRUGO, ad7298_show_scale, NULL, 0);

static ssize_t ad7298_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_state *st = iio_dev_get_devdata(dev_info);

	return sprintf(buf, "%s\n", spi_get_device_id(st->spi)->name);
}
static IIO_DEVICE_ATTR(name, S_IRUGO, ad7298_show_name, NULL, 0);

static struct attribute *ad7298_attributes[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_in2_raw.dev_attr.attr,
	&iio_dev_attr_in3_raw.dev_attr.attr,
	&iio_dev_attr_in4_raw.dev_attr.attr,
	&iio_dev_attr_in5_raw.dev_attr.attr,
	&iio_dev_attr_in6_raw.dev_attr.attr,
	&iio_dev_attr_in7_raw.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	&iio_dev_attr_temp0_input.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7298_attribute_group = {
	.attrs = ad7298_attributes,
};

static int __devinit ad7298_probe(struct spi_device *spi)
{
	struct ad7298_platform_data *pdata = spi->dev.platform_data;
	struct ad7298_state *st;
	int ret;

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
	}

	spi_set_drvdata(spi, st);

	atomic_set(&st->protect_ring, 0);
	st->spi = spi;

	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}

	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->attrs = &ad7298_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	/* Setup default message */

	st->scan_single_xfer[0].tx_buf = &st->tx_buf[0];
	st->scan_single_xfer[0].len = 2;
	st->scan_single_xfer[0].cs_change = 1;
	st->scan_single_xfer[1].tx_buf = &st->tx_buf[1];
	st->scan_single_xfer[1].len = 2;
	st->scan_single_xfer[1].cs_change = 1;
	st->scan_single_xfer[2].rx_buf = &st->rx_buf[0];
	st->scan_single_xfer[2].len = 2;

	spi_message_init(&st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[0], &st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[1], &st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[2], &st->scan_single_msg);

	if (pdata && pdata->vref_mv) {
		st->int_vref_mv = pdata->vref_mv;
		st->ext_ref = AD7298_EXTREF;
	} else {
		st->int_vref_mv = AD7298_INTREF_mV;
	}

	ret = ad7298_register_ring_funcs_and_init(st->indio_dev);
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
	ad7298_ring_cleanup(st->indio_dev);
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

static int __devexit ad7298_remove(struct spi_device *spi)
{
	struct ad7298_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	iio_ring_buffer_unregister(indio_dev->ring);
	ad7298_ring_cleanup(indio_dev);
	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	kfree(st);
	return 0;
}

static const struct spi_device_id ad7298_id[] = {
	{"ad7298", 0},
	{}
};

static struct spi_driver ad7298_driver = {
	.driver = {
		.name	= "ad7298",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad7298_probe,
	.remove		= __devexit_p(ad7298_remove),
	.id_table	= ad7298_id,
};

static int __init ad7298_init(void)
{
	return spi_register_driver(&ad7298_driver);
}
module_init(ad7298_init);

static void __exit ad7298_exit(void)
{
	spi_unregister_driver(&ad7298_driver);
}
module_exit(ad7298_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7298 ADC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ad7298");

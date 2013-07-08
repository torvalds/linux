/*
 * AD7606 SPI ADC driver
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
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>

#include "ad7606.h"

int ad7606_reset(struct ad7606_state *st)
{
	if (gpio_is_valid(st->pdata->gpio_reset)) {
		gpio_set_value(st->pdata->gpio_reset, 1);
		ndelay(100); /* t_reset >= 100ns */
		gpio_set_value(st->pdata->gpio_reset, 0);
		return 0;
	}

	return -ENODEV;
}

static int ad7606_scan_direct(struct iio_dev *indio_dev, unsigned ch)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	int ret;

	st->done = false;
	gpio_set_value(st->pdata->gpio_convst, 1);

	ret = wait_event_interruptible(st->wq_data_avail, st->done);
	if (ret)
		goto error_ret;

	if (gpio_is_valid(st->pdata->gpio_frstdata)) {
		ret = st->bops->read_block(st->dev, 1, st->data);
		if (ret)
			goto error_ret;
		if (!gpio_get_value(st->pdata->gpio_frstdata)) {
			/* This should never happen */
			ad7606_reset(st);
			ret = -EIO;
			goto error_ret;
		}
		ret = st->bops->read_block(st->dev,
			st->chip_info->num_channels - 1, &st->data[1]);
		if (ret)
			goto error_ret;
	} else {
		ret = st->bops->read_block(st->dev,
			st->chip_info->num_channels, st->data);
		if (ret)
			goto error_ret;
	}

	ret = st->data[ch];

error_ret:
	gpio_set_value(st->pdata->gpio_convst, 0);

	return ret;
}

static int ad7606_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad7606_state *st = iio_priv(indio_dev);
	unsigned int scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		if (iio_buffer_enabled(indio_dev))
			ret = -EBUSY;
		else
			ret = ad7606_scan_direct(indio_dev, chan->address);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;
		*val = (short) ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->range * 1000 * 2)
			>> st->chip_info->channels[0].scan_type.realbits;
		*val =  scale_uv / 1000;
		*val2 = (scale_uv % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static ssize_t ad7606_show_range(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7606_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%u\n", st->range);
}

static ssize_t ad7606_store_range(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7606_state *st = iio_priv(indio_dev);
	unsigned long lval;
	int ret;

	ret = kstrtoul(buf, 10, &lval);
	if (ret)
		return ret;

	if (!(lval == 5000 || lval == 10000)) {
		dev_err(dev, "range is not supported\n");
		return -EINVAL;
	}
	mutex_lock(&indio_dev->mlock);
	gpio_set_value(st->pdata->gpio_range, lval == 10000);
	st->range = lval;
	mutex_unlock(&indio_dev->mlock);

	return count;
}

static IIO_DEVICE_ATTR(in_voltage_range, S_IRUGO | S_IWUSR, \
		       ad7606_show_range, ad7606_store_range, 0);
static IIO_CONST_ATTR(in_voltage_range_available, "5000 10000");

static ssize_t ad7606_show_oversampling_ratio(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7606_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%u\n", st->oversampling);
}

static int ad7606_oversampling_get_index(unsigned val)
{
	unsigned char supported[] = {0, 2, 4, 8, 16, 32, 64};
	int i;

	for (i = 0; i < ARRAY_SIZE(supported); i++)
		if (val == supported[i])
			return i;

	return -EINVAL;
}

static ssize_t ad7606_store_oversampling_ratio(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7606_state *st = iio_priv(indio_dev);
	unsigned long lval;
	int ret;

	ret = kstrtoul(buf, 10, &lval);
	if (ret)
		return ret;

	ret = ad7606_oversampling_get_index(lval);
	if (ret < 0) {
		dev_err(dev, "oversampling %lu is not supported\n", lval);
		return ret;
	}

	mutex_lock(&indio_dev->mlock);
	gpio_set_value(st->pdata->gpio_os0, (ret >> 0) & 1);
	gpio_set_value(st->pdata->gpio_os1, (ret >> 1) & 1);
	gpio_set_value(st->pdata->gpio_os1, (ret >> 2) & 1);
	st->oversampling = lval;
	mutex_unlock(&indio_dev->mlock);

	return count;
}

static IIO_DEVICE_ATTR(oversampling_ratio, S_IRUGO | S_IWUSR,
		       ad7606_show_oversampling_ratio,
		       ad7606_store_oversampling_ratio, 0);
static IIO_CONST_ATTR(oversampling_ratio_available, "0 2 4 8 16 32 64");

static struct attribute *ad7606_attributes_os_and_range[] = {
	&iio_dev_attr_in_voltage_range.dev_attr.attr,
	&iio_const_attr_in_voltage_range_available.dev_attr.attr,
	&iio_dev_attr_oversampling_ratio.dev_attr.attr,
	&iio_const_attr_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7606_attribute_group_os_and_range = {
	.attrs = ad7606_attributes_os_and_range,
};

static struct attribute *ad7606_attributes_os[] = {
	&iio_dev_attr_oversampling_ratio.dev_attr.attr,
	&iio_const_attr_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7606_attribute_group_os = {
	.attrs = ad7606_attributes_os,
};

static struct attribute *ad7606_attributes_range[] = {
	&iio_dev_attr_in_voltage_range.dev_attr.attr,
	&iio_const_attr_in_voltage_range_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7606_attribute_group_range = {
	.attrs = ad7606_attributes_range,
};

#define AD7606_CHANNEL(num)					\
	{							\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = num,					\
		.address = num,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),\
		.scan_index = num,				\
		.scan_type = IIO_ST('s', 16, 16, 0),		\
	}

static const struct iio_chan_spec ad7606_8_channels[] = {
	AD7606_CHANNEL(0),
	AD7606_CHANNEL(1),
	AD7606_CHANNEL(2),
	AD7606_CHANNEL(3),
	AD7606_CHANNEL(4),
	AD7606_CHANNEL(5),
	AD7606_CHANNEL(6),
	AD7606_CHANNEL(7),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static const struct iio_chan_spec ad7606_6_channels[] = {
	AD7606_CHANNEL(0),
	AD7606_CHANNEL(1),
	AD7606_CHANNEL(2),
	AD7606_CHANNEL(3),
	AD7606_CHANNEL(4),
	AD7606_CHANNEL(5),
	IIO_CHAN_SOFT_TIMESTAMP(6),
};

static const struct iio_chan_spec ad7606_4_channels[] = {
	AD7606_CHANNEL(0),
	AD7606_CHANNEL(1),
	AD7606_CHANNEL(2),
	AD7606_CHANNEL(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct ad7606_chip_info ad7606_chip_info_tbl[] = {
	/*
	 * More devices added in future
	 */
	[ID_AD7606_8] = {
		.name = "ad7606",
		.int_vref_mv = 2500,
		.channels = ad7606_8_channels,
		.num_channels = 8,
	},
	[ID_AD7606_6] = {
		.name = "ad7606-6",
		.int_vref_mv = 2500,
		.channels = ad7606_6_channels,
		.num_channels = 6,
	},
	[ID_AD7606_4] = {
		.name = "ad7606-4",
		.int_vref_mv = 2500,
		.channels = ad7606_4_channels,
		.num_channels = 4,
	},
};

static int ad7606_request_gpios(struct ad7606_state *st)
{
	struct gpio gpio_array[3] = {
		[0] = {
			.gpio =  st->pdata->gpio_os0,
			.flags = GPIOF_DIR_OUT | ((st->oversampling & 1) ?
				 GPIOF_INIT_HIGH : GPIOF_INIT_LOW),
			.label = "AD7606_OS0",
		},
		[1] = {
			.gpio =  st->pdata->gpio_os1,
			.flags = GPIOF_DIR_OUT | ((st->oversampling & 2) ?
				 GPIOF_INIT_HIGH : GPIOF_INIT_LOW),
			.label = "AD7606_OS1",
		},
		[2] = {
			.gpio =  st->pdata->gpio_os2,
			.flags = GPIOF_DIR_OUT | ((st->oversampling & 4) ?
				 GPIOF_INIT_HIGH : GPIOF_INIT_LOW),
			.label = "AD7606_OS2",
		},
	};
	int ret;

	if (gpio_is_valid(st->pdata->gpio_convst)) {
		ret = gpio_request_one(st->pdata->gpio_convst,
				       GPIOF_OUT_INIT_LOW,
				       "AD7606_CONVST");
		if (ret) {
			dev_err(st->dev, "failed to request GPIO CONVST\n");
			goto error_ret;
		}
	} else {
		ret = -EIO;
		goto error_ret;
	}

	if (gpio_is_valid(st->pdata->gpio_os0) &&
	    gpio_is_valid(st->pdata->gpio_os1) &&
	    gpio_is_valid(st->pdata->gpio_os2)) {
		ret = gpio_request_array(gpio_array, ARRAY_SIZE(gpio_array));
		if (ret < 0)
			goto error_free_convst;
	}

	if (gpio_is_valid(st->pdata->gpio_reset)) {
		ret = gpio_request_one(st->pdata->gpio_reset,
				       GPIOF_OUT_INIT_LOW,
				       "AD7606_RESET");
		if (ret < 0)
			goto error_free_os;
	}

	if (gpio_is_valid(st->pdata->gpio_range)) {
		ret = gpio_request_one(st->pdata->gpio_range, GPIOF_DIR_OUT |
				       ((st->range == 10000) ? GPIOF_INIT_HIGH :
					GPIOF_INIT_LOW), "AD7606_RANGE");
		if (ret < 0)
			goto error_free_reset;
	}
	if (gpio_is_valid(st->pdata->gpio_stby)) {
		ret = gpio_request_one(st->pdata->gpio_stby,
				       GPIOF_OUT_INIT_HIGH,
				       "AD7606_STBY");
		if (ret < 0)
			goto error_free_range;
	}

	if (gpio_is_valid(st->pdata->gpio_frstdata)) {
		ret = gpio_request_one(st->pdata->gpio_frstdata, GPIOF_IN,
				       "AD7606_FRSTDATA");
		if (ret < 0)
			goto error_free_stby;
	}

	return 0;

error_free_stby:
	if (gpio_is_valid(st->pdata->gpio_stby))
		gpio_free(st->pdata->gpio_stby);
error_free_range:
	if (gpio_is_valid(st->pdata->gpio_range))
		gpio_free(st->pdata->gpio_range);
error_free_reset:
	if (gpio_is_valid(st->pdata->gpio_reset))
		gpio_free(st->pdata->gpio_reset);
error_free_os:
	if (gpio_is_valid(st->pdata->gpio_os0) &&
	    gpio_is_valid(st->pdata->gpio_os1) &&
	    gpio_is_valid(st->pdata->gpio_os2))
		gpio_free_array(gpio_array, ARRAY_SIZE(gpio_array));
error_free_convst:
	gpio_free(st->pdata->gpio_convst);
error_ret:
	return ret;
}

static void ad7606_free_gpios(struct ad7606_state *st)
{
	if (gpio_is_valid(st->pdata->gpio_frstdata))
		gpio_free(st->pdata->gpio_frstdata);
	if (gpio_is_valid(st->pdata->gpio_stby))
		gpio_free(st->pdata->gpio_stby);
	if (gpio_is_valid(st->pdata->gpio_range))
		gpio_free(st->pdata->gpio_range);
	if (gpio_is_valid(st->pdata->gpio_reset))
		gpio_free(st->pdata->gpio_reset);
	if (gpio_is_valid(st->pdata->gpio_os0) &&
	    gpio_is_valid(st->pdata->gpio_os1) &&
	    gpio_is_valid(st->pdata->gpio_os2)) {
		gpio_free(st->pdata->gpio_os2);
		gpio_free(st->pdata->gpio_os1);
		gpio_free(st->pdata->gpio_os0);
	}
	gpio_free(st->pdata->gpio_convst);
}

/**
 *  Interrupt handler
 */
static irqreturn_t ad7606_interrupt(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ad7606_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev)) {
		if (!work_pending(&st->poll_work))
			schedule_work(&st->poll_work);
	} else {
		st->done = true;
		wake_up_interruptible(&st->wq_data_avail);
	}

	return IRQ_HANDLED;
};

static const struct iio_info ad7606_info_no_os_or_range = {
	.driver_module = THIS_MODULE,
	.read_raw = &ad7606_read_raw,
};

static const struct iio_info ad7606_info_os_and_range = {
	.driver_module = THIS_MODULE,
	.read_raw = &ad7606_read_raw,
	.attrs = &ad7606_attribute_group_os_and_range,
};

static const struct iio_info ad7606_info_os = {
	.driver_module = THIS_MODULE,
	.read_raw = &ad7606_read_raw,
	.attrs = &ad7606_attribute_group_os,
};

static const struct iio_info ad7606_info_range = {
	.driver_module = THIS_MODULE,
	.read_raw = &ad7606_read_raw,
	.attrs = &ad7606_attribute_group_range,
};

struct iio_dev *ad7606_probe(struct device *dev, int irq,
			      void __iomem *base_address,
			      unsigned id,
			      const struct ad7606_bus_ops *bops)
{
	struct ad7606_platform_data *pdata = dev->platform_data;
	struct ad7606_state *st;
	int ret;
	struct iio_dev *indio_dev = iio_device_alloc(sizeof(*st));

	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	st = iio_priv(indio_dev);

	st->dev = dev;
	st->bops = bops;
	st->base_address = base_address;
	st->range = pdata->default_range == 10000 ? 10000 : 5000;

	ret = ad7606_oversampling_get_index(pdata->default_os);
	if (ret < 0) {
		dev_warn(dev, "oversampling %d is not supported\n",
			 pdata->default_os);
		st->oversampling = 0;
	} else {
		st->oversampling = pdata->default_os;
	}

	st->reg = regulator_get(dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;
	}

	st->pdata = pdata;
	st->chip_info = &ad7606_chip_info_tbl[id];

	indio_dev->dev.parent = dev;
	if (gpio_is_valid(st->pdata->gpio_os0) &&
	    gpio_is_valid(st->pdata->gpio_os1) &&
	    gpio_is_valid(st->pdata->gpio_os2)) {
		if (gpio_is_valid(st->pdata->gpio_range))
			indio_dev->info = &ad7606_info_os_and_range;
		else
			indio_dev->info = &ad7606_info_os;
	} else {
		if (gpio_is_valid(st->pdata->gpio_range))
			indio_dev->info = &ad7606_info_range;
		else
			indio_dev->info = &ad7606_info_no_os_or_range;
	}
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = st->chip_info->name;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;

	init_waitqueue_head(&st->wq_data_avail);

	ret = ad7606_request_gpios(st);
	if (ret)
		goto error_disable_reg;

	ret = ad7606_reset(st);
	if (ret)
		dev_warn(st->dev, "failed to RESET: no RESET GPIO specified\n");

	ret = request_irq(irq, ad7606_interrupt,
		IRQF_TRIGGER_FALLING, st->chip_info->name, indio_dev);
	if (ret)
		goto error_free_gpios;

	ret = ad7606_register_ring_funcs_and_init(indio_dev);
	if (ret)
		goto error_free_irq;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unregister_ring;

	return indio_dev;
error_unregister_ring:
	ad7606_ring_cleanup(indio_dev);

error_free_irq:
	free_irq(irq, indio_dev);

error_free_gpios:
	ad7606_free_gpios(st);

error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);
	iio_device_free(indio_dev);
error_ret:
	return ERR_PTR(ret);
}

int ad7606_remove(struct iio_dev *indio_dev, int irq)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	ad7606_ring_cleanup(indio_dev);

	free_irq(irq, indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}

	ad7606_free_gpios(st);
	iio_device_free(indio_dev);

	return 0;
}

void ad7606_suspend(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	if (gpio_is_valid(st->pdata->gpio_stby)) {
		if (gpio_is_valid(st->pdata->gpio_range))
			gpio_set_value(st->pdata->gpio_range, 1);
		gpio_set_value(st->pdata->gpio_stby, 0);
	}
}

void ad7606_resume(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	if (gpio_is_valid(st->pdata->gpio_stby)) {
		if (gpio_is_valid(st->pdata->gpio_range))
			gpio_set_value(st->pdata->gpio_range,
					st->range == 10000);

		gpio_set_value(st->pdata->gpio_stby, 1);
		ad7606_reset(st);
	}
}

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7606 ADC");
MODULE_LICENSE("GPL v2");

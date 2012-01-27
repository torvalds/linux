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
#include <linux/module.h>

#include "../iio.h"
#include "../sysfs.h"
#include "dac.h"

#include "ad5446.h"

static void ad5446_store_sample(struct ad5446_state *st, unsigned val)
{
	st->data.d16 = cpu_to_be16(AD5446_LOAD | val);
}

static void ad5542_store_sample(struct ad5446_state *st, unsigned val)
{
	st->data.d16 = cpu_to_be16(val);
}

static void ad5620_store_sample(struct ad5446_state *st, unsigned val)
{
	st->data.d16 = cpu_to_be16(AD5620_LOAD | val);
}

static void ad5660_store_sample(struct ad5446_state *st, unsigned val)
{
	val |= AD5660_LOAD;
	st->data.d24[0] = (val >> 16) & 0xFF;
	st->data.d24[1] = (val >> 8) & 0xFF;
	st->data.d24[2] = val & 0xFF;
}

static void ad5620_store_pwr_down(struct ad5446_state *st, unsigned mode)
{
	st->data.d16 = cpu_to_be16(mode << 14);
}

static void ad5660_store_pwr_down(struct ad5446_state *st, unsigned mode)
{
	unsigned val = mode << 16;

	st->data.d24[0] = (val >> 16) & 0xFF;
	st->data.d24[1] = (val >> 8) & 0xFF;
	st->data.d24[2] = val & 0xFF;
}

static ssize_t ad5446_write_powerdown_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_priv(indio_dev);

	if (sysfs_streq(buf, "1kohm_to_gnd"))
		st->pwr_down_mode = MODE_PWRDWN_1k;
	else if (sysfs_streq(buf, "100kohm_to_gnd"))
		st->pwr_down_mode = MODE_PWRDWN_100k;
	else if (sysfs_streq(buf, "three_state"))
		st->pwr_down_mode = MODE_PWRDWN_TRISTATE;
	else
		return -EINVAL;

	return len;
}

static ssize_t ad5446_read_powerdown_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_priv(indio_dev);

	char mode[][15] = {"", "1kohm_to_gnd", "100kohm_to_gnd", "three_state"};

	return sprintf(buf, "%s\n", mode[st->pwr_down_mode]);
}

static ssize_t ad5446_read_dac_powerdown(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5446_write_dac_powerdown(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_priv(indio_dev);
	unsigned long readin;
	int ret;

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	if (readin > 1)
		ret = -EINVAL;

	mutex_lock(&indio_dev->mlock);
	st->pwr_down = readin;

	if (st->pwr_down)
		st->chip_info->store_pwr_down(st, st->pwr_down_mode);
	else
		st->chip_info->store_sample(st, st->cached_val);

	ret = spi_sync(st->spi, &st->msg);
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(out_voltage_powerdown_mode, S_IRUGO | S_IWUSR,
			ad5446_read_powerdown_mode,
			ad5446_write_powerdown_mode, 0);

static IIO_CONST_ATTR(out_voltage_powerdown_mode_available,
			"1kohm_to_gnd 100kohm_to_gnd three_state");

static IIO_DEVICE_ATTR(out_voltage0_powerdown, S_IRUGO | S_IWUSR,
			ad5446_read_dac_powerdown,
			ad5446_write_dac_powerdown, 0);

static struct attribute *ad5446_attributes[] = {
	&iio_dev_attr_out_voltage0_powerdown.dev_attr.attr,
	&iio_dev_attr_out_voltage_powerdown_mode.dev_attr.attr,
	&iio_const_attr_out_voltage_powerdown_mode_available.dev_attr.attr,
	NULL,
};

static umode_t ad5446_attr_is_visible(struct kobject *kobj,
				     struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5446_state *st = iio_priv(indio_dev);

	umode_t mode = attr->mode;

	if (!st->chip_info->store_pwr_down &&
		(attr == &iio_dev_attr_out_voltage0_powerdown.dev_attr.attr ||
		attr == &iio_dev_attr_out_voltage_powerdown_mode.
		 dev_attr.attr ||
		attr ==
		&iio_const_attr_out_voltage_powerdown_mode_available.
		 dev_attr.attr))
		mode = 0;

	return mode;
}

static const struct attribute_group ad5446_attribute_group = {
	.attrs = ad5446_attributes,
	.is_visible = ad5446_attr_is_visible,
};

#define AD5446_CHANNEL(bits, storage, shift) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.output = 1, \
	.channel = 0, \
	.info_mask = IIO_CHAN_INFO_SCALE_SHARED_BIT, \
	.scan_type = IIO_ST('u', (bits), (storage), (shift)) \
}

static const struct ad5446_chip_info ad5446_chip_info_tbl[] = {
	[ID_AD5444] = {
		.channel = AD5446_CHANNEL(12, 16, 2),
		.store_sample = ad5446_store_sample,
	},
	[ID_AD5446] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.store_sample = ad5446_store_sample,
	},
	[ID_AD5541A] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.store_sample = ad5542_store_sample,
	},
	[ID_AD5542A] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.store_sample = ad5542_store_sample,
	},
	[ID_AD5543] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.store_sample = ad5542_store_sample,
	},
	[ID_AD5512A] = {
		.channel = AD5446_CHANNEL(12, 16, 4),
		.store_sample = ad5542_store_sample,
	},
	[ID_AD5553] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.store_sample = ad5542_store_sample,
	},
	[ID_AD5601] = {
		.channel = AD5446_CHANNEL(8, 16, 6),
		.store_sample = ad5542_store_sample,
		.store_pwr_down = ad5620_store_pwr_down,
	},
	[ID_AD5611] = {
		.channel = AD5446_CHANNEL(10, 16, 4),
		.store_sample = ad5542_store_sample,
		.store_pwr_down = ad5620_store_pwr_down,
	},
	[ID_AD5621] = {
		.channel = AD5446_CHANNEL(12, 16, 2),
		.store_sample = ad5542_store_sample,
		.store_pwr_down = ad5620_store_pwr_down,
	},
	[ID_AD5620_2500] = {
		.channel = AD5446_CHANNEL(12, 16, 2),
		.int_vref_mv = 2500,
		.store_sample = ad5620_store_sample,
		.store_pwr_down = ad5620_store_pwr_down,
	},
	[ID_AD5620_1250] = {
		.channel = AD5446_CHANNEL(12, 16, 2),
		.int_vref_mv = 1250,
		.store_sample = ad5620_store_sample,
		.store_pwr_down = ad5620_store_pwr_down,
	},
	[ID_AD5640_2500] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.int_vref_mv = 2500,
		.store_sample = ad5620_store_sample,
		.store_pwr_down = ad5620_store_pwr_down,
	},
	[ID_AD5640_1250] = {
		.channel = AD5446_CHANNEL(14, 16, 0),
		.int_vref_mv = 1250,
		.store_sample = ad5620_store_sample,
		.store_pwr_down = ad5620_store_pwr_down,
	},
	[ID_AD5660_2500] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.int_vref_mv = 2500,
		.store_sample = ad5660_store_sample,
		.store_pwr_down = ad5660_store_pwr_down,
	},
	[ID_AD5660_1250] = {
		.channel = AD5446_CHANNEL(16, 16, 0),
		.int_vref_mv = 1250,
		.store_sample = ad5660_store_sample,
		.store_pwr_down = ad5660_store_pwr_down,
	},
};

static int ad5446_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5446_state *st = iio_priv(indio_dev);
	unsigned long scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->vref_mv * 1000) >> chan->scan_type.realbits;
		*val =  scale_uv / 1000;
		*val2 = (scale_uv % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;

	}
	return -EINVAL;
}

static int ad5446_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad5446_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case 0:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		val <<= chan->scan_type.shift;
		mutex_lock(&indio_dev->mlock);
		st->cached_val = val;
		st->chip_info->store_sample(st, val);
		ret = spi_sync(st->spi, &st->msg);
		mutex_unlock(&indio_dev->mlock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad5446_info = {
	.read_raw = ad5446_read_raw,
	.write_raw = ad5446_write_raw,
	.attrs = &ad5446_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit ad5446_probe(struct spi_device *spi)
{
	struct ad5446_state *st;
	struct iio_dev *indio_dev;
	struct regulator *reg;
	int ret, voltage_uv = 0;

	reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(reg)) {
		ret = regulator_enable(reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(reg);
	}

	indio_dev = iio_allocate_device(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}
	st = iio_priv(indio_dev);
	st->chip_info =
		&ad5446_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	spi_set_drvdata(spi, indio_dev);
	st->reg = reg;
	st->spi = spi;

	/* Estabilish that the iio_dev is a child of the spi device */
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5446_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;

	/* Setup default message */

	st->xfer.tx_buf = &st->data;
	st->xfer.len = st->chip_info->channel.scan_type.storagebits / 8;

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

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_device;

	return 0;

error_free_device:
	iio_free_device(indio_dev);
error_disable_reg:
	if (!IS_ERR(reg))
		regulator_disable(reg);
error_put_reg:
	if (!IS_ERR(reg))
		regulator_put(reg);

	return ret;
}

static int ad5446_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5446_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_free_device(indio_dev);

	return 0;
}

static const struct spi_device_id ad5446_id[] = {
	{"ad5444", ID_AD5444},
	{"ad5446", ID_AD5446},
	{"ad5512a", ID_AD5512A},
	{"ad5541a", ID_AD5541A},
	{"ad5542a", ID_AD5542A},
	{"ad5543", ID_AD5543},
	{"ad5553", ID_AD5553},
	{"ad5601", ID_AD5601},
	{"ad5611", ID_AD5611},
	{"ad5621", ID_AD5621},
	{"ad5620-2500", ID_AD5620_2500}, /* AD5620/40/60: */
	{"ad5620-1250", ID_AD5620_1250}, /* part numbers may look differently */
	{"ad5640-2500", ID_AD5640_2500},
	{"ad5640-1250", ID_AD5640_1250},
	{"ad5660-2500", ID_AD5660_2500},
	{"ad5660-1250", ID_AD5660_1250},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5446_id);

static struct spi_driver ad5446_driver = {
	.driver = {
		.name	= "ad5446",
		.owner	= THIS_MODULE,
	},
	.probe		= ad5446_probe,
	.remove		= __devexit_p(ad5446_remove),
	.id_table	= ad5446_id,
};
module_spi_driver(ad5446_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5444/AD5446 DAC");
MODULE_LICENSE("GPL v2");

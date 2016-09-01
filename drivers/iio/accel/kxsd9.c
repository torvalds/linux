/*
 * kxsd9.c	simple support for the Kionix KXSD9 3D
 *		accelerometer.
 *
 * Copyright (c) 2008-2009 Jonathan Cameron <jic23@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The i2c interface is very similar, so shouldn't be a problem once
 * I have a suitable wire made up.
 *
 * TODO:	Support the motion detector
 *		Uses register address incrementing so could have a
 *		heavily optimized ring buffer access function.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "kxsd9.h"

#define KXSD9_REG_X		0x00
#define KXSD9_REG_Y		0x02
#define KXSD9_REG_Z		0x04
#define KXSD9_REG_AUX		0x06
#define KXSD9_REG_RESET		0x0a
#define KXSD9_REG_CTRL_C	0x0c

#define KXSD9_FS_MASK		0x03

#define KXSD9_REG_CTRL_B	0x0d
#define KXSD9_REG_CTRL_A	0x0e

/**
 * struct kxsd9_state - device related storage
 * @map: regmap to the device
 */
struct kxsd9_state {
	struct regmap *map;
};

#define KXSD9_SCALE_2G "0.011978"
#define KXSD9_SCALE_4G "0.023927"
#define KXSD9_SCALE_6G "0.035934"
#define KXSD9_SCALE_8G "0.047853"

/* reverse order */
static const int kxsd9_micro_scales[4] = { 47853, 35934, 23927, 11978 };

static int kxsd9_write_scale(struct iio_dev *indio_dev, int micro)
{
	int ret, i;
	struct kxsd9_state *st = iio_priv(indio_dev);
	bool foundit = false;
	unsigned int val;

	for (i = 0; i < 4; i++)
		if (micro == kxsd9_micro_scales[i]) {
			foundit = true;
			break;
		}
	if (!foundit)
		return -EINVAL;

	ret = regmap_read(st->map,
			  KXSD9_REG_CTRL_C,
			  &val);
	if (ret < 0)
		goto error_ret;
	ret = regmap_write(st->map,
			   KXSD9_REG_CTRL_C,
			   (val & ~KXSD9_FS_MASK) | i);
error_ret:
	return ret;
}

static int kxsd9_read(struct iio_dev *indio_dev, u8 address)
{
	int ret;
	struct kxsd9_state *st = iio_priv(indio_dev);
	__be16 raw_val;

	ret = regmap_bulk_read(st->map, address, &raw_val, sizeof(raw_val));
	if (ret)
		return ret;
	/* Only 12 bits are valid */
	return be16_to_cpu(raw_val) & 0xfff0;
}

static IIO_CONST_ATTR(accel_scale_available,
		KXSD9_SCALE_2G " "
		KXSD9_SCALE_4G " "
		KXSD9_SCALE_6G " "
		KXSD9_SCALE_8G);

static struct attribute *kxsd9_attributes[] = {
	&iio_const_attr_accel_scale_available.dev_attr.attr,
	NULL,
};

static int kxsd9_write_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int val,
			   int val2,
			   long mask)
{
	int ret = -EINVAL;

	if (mask == IIO_CHAN_INFO_SCALE) {
		/* Check no integer component */
		if (val)
			return -EINVAL;
		ret = kxsd9_write_scale(indio_dev, val2);
	}

	return ret;
}

static int kxsd9_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	int ret = -EINVAL;
	struct kxsd9_state *st = iio_priv(indio_dev);
	unsigned int regval;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = kxsd9_read(indio_dev, chan->address);
		if (ret < 0)
			goto error_ret;
		*val = ret;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(st->map,
				  KXSD9_REG_CTRL_C,
				  &regval);
		if (ret < 0)
			goto error_ret;
		*val = 0;
		*val2 = kxsd9_micro_scales[regval & KXSD9_FS_MASK];
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	}

error_ret:
	return ret;
};
#define KXSD9_ACCEL_CHAN(axis)						\
	{								\
		.type = IIO_ACCEL,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.address = KXSD9_REG_##axis,				\
	}

static const struct iio_chan_spec kxsd9_channels[] = {
	KXSD9_ACCEL_CHAN(X), KXSD9_ACCEL_CHAN(Y), KXSD9_ACCEL_CHAN(Z),
	{
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.indexed = 1,
		.address = KXSD9_REG_AUX,
	}
};

static const struct attribute_group kxsd9_attribute_group = {
	.attrs = kxsd9_attributes,
};

static int kxsd9_power_up(struct kxsd9_state *st)
{
	int ret;

	ret = regmap_write(st->map, KXSD9_REG_CTRL_B, 0x40);
	if (ret)
		return ret;
	return regmap_write(st->map, KXSD9_REG_CTRL_C, 0x9b);
};

static const struct iio_info kxsd9_info = {
	.read_raw = &kxsd9_read_raw,
	.write_raw = &kxsd9_write_raw,
	.attrs = &kxsd9_attribute_group,
	.driver_module = THIS_MODULE,
};

int kxsd9_common_probe(struct device *parent,
		       struct regmap *map,
		       const char *name)
{
	struct iio_dev *indio_dev;
	struct kxsd9_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(parent, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->map = map;

	indio_dev->channels = kxsd9_channels;
	indio_dev->num_channels = ARRAY_SIZE(kxsd9_channels);
	indio_dev->name = name;
	indio_dev->dev.parent = parent;
	indio_dev->info = &kxsd9_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	kxsd9_power_up(st);

	ret = iio_device_register(indio_dev);
	if (ret)
		return ret;

	dev_set_drvdata(parent, indio_dev);

	return 0;
}
EXPORT_SYMBOL(kxsd9_common_probe);

int kxsd9_common_remove(struct device *parent)
{
	struct iio_dev *indio_dev = dev_get_drvdata(parent);

	iio_device_unregister(indio_dev);

	return 0;
}
EXPORT_SYMBOL(kxsd9_common_remove);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Kionix KXSD9 driver");
MODULE_LICENSE("GPL v2");

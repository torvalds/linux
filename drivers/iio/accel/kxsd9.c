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
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include "kxsd9.h"

#define KXSD9_REG_X		0x00
#define KXSD9_REG_Y		0x02
#define KXSD9_REG_Z		0x04
#define KXSD9_REG_AUX		0x06
#define KXSD9_REG_RESET		0x0a
#define KXSD9_REG_CTRL_C	0x0c

#define KXSD9_CTRL_C_FS_MASK	0x03
#define KXSD9_CTRL_C_FS_8G	0x00
#define KXSD9_CTRL_C_FS_6G	0x01
#define KXSD9_CTRL_C_FS_4G	0x02
#define KXSD9_CTRL_C_FS_2G	0x03
#define KXSD9_CTRL_C_MOT_LAT	BIT(3)
#define KXSD9_CTRL_C_MOT_LEV	BIT(4)
#define KXSD9_CTRL_C_LP_MASK	0xe0
#define KXSD9_CTRL_C_LP_NONE	0x00
#define KXSD9_CTRL_C_LP_2000HZC	BIT(5)
#define KXSD9_CTRL_C_LP_2000HZB	BIT(6)
#define KXSD9_CTRL_C_LP_2000HZA	(BIT(5)|BIT(6))
#define KXSD9_CTRL_C_LP_1000HZ	BIT(7)
#define KXSD9_CTRL_C_LP_500HZ	(BIT(7)|BIT(5))
#define KXSD9_CTRL_C_LP_100HZ	(BIT(7)|BIT(6))
#define KXSD9_CTRL_C_LP_50HZ	(BIT(7)|BIT(6)|BIT(5))

#define KXSD9_REG_CTRL_B	0x0d

#define KXSD9_CTRL_B_CLK_HLD	BIT(7)
#define KXSD9_CTRL_B_ENABLE	BIT(6)
#define KXSD9_CTRL_B_ST		BIT(5) /* Self-test */

#define KXSD9_REG_CTRL_A	0x0e

/**
 * struct kxsd9_state - device related storage
 * @dev: pointer to the parent device
 * @map: regmap to the device
 */
struct kxsd9_state {
	struct device *dev;
	struct regmap *map;
};

#define KXSD9_SCALE_2G "0.011978"
#define KXSD9_SCALE_4G "0.023927"
#define KXSD9_SCALE_6G "0.035934"
#define KXSD9_SCALE_8G "0.047853"

/* reverse order */
static const int kxsd9_micro_scales[4] = { 47853, 35934, 23927, 11978 };

#define KXSD9_ZERO_G_OFFSET -2048

static int kxsd9_write_scale(struct iio_dev *indio_dev, int micro)
{
	int ret, i;
	struct kxsd9_state *st = iio_priv(indio_dev);
	bool foundit = false;

	for (i = 0; i < 4; i++)
		if (micro == kxsd9_micro_scales[i]) {
			foundit = true;
			break;
		}
	if (!foundit)
		return -EINVAL;

	ret = regmap_update_bits(st->map,
				 KXSD9_REG_CTRL_C,
				 KXSD9_CTRL_C_FS_MASK,
				 i);
	if (ret < 0)
		goto error_ret;
error_ret:
	return ret;
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
	__be16 raw_val;
	u16 nval;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_bulk_read(st->map, chan->address, &raw_val,
				       sizeof(raw_val));
		if (ret)
			goto error_ret;
		nval = be16_to_cpu(raw_val);
		/* Only 12 bits are valid */
		nval >>= 4;
		*val = nval;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_OFFSET:
		/* This has a bias of -2048 */
		*val = KXSD9_ZERO_G_OFFSET;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(st->map,
				  KXSD9_REG_CTRL_C,
				  &regval);
		if (ret < 0)
			goto error_ret;
		*val = 0;
		*val2 = kxsd9_micro_scales[regval & KXSD9_CTRL_C_FS_MASK];
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	}

error_ret:
	return ret;
};

static irqreturn_t kxsd9_trigger_handler(int irq, void *p)
{
	const struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct kxsd9_state *st = iio_priv(indio_dev);
	int ret;
	/* 4 * 16bit values AND timestamp */
	__be16 hw_values[8];

	ret = regmap_bulk_read(st->map,
			       KXSD9_REG_X,
			       &hw_values,
			       8);
	if (ret) {
		dev_err(st->dev,
			"error reading data\n");
		return ret;
	}

	iio_push_to_buffers_with_timestamp(indio_dev,
					   hw_values,
					   iio_get_time_ns(indio_dev));
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

#define KXSD9_ACCEL_CHAN(axis, index)						\
	{								\
		.type = IIO_ACCEL,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
					BIT(IIO_CHAN_INFO_OFFSET),	\
		.address = KXSD9_REG_##axis,				\
		.scan_index = index,					\
		.scan_type = {                                          \
			.sign = 'u',					\
			.realbits = 12,					\
			.storagebits = 16,				\
			.shift = 4,					\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec kxsd9_channels[] = {
	KXSD9_ACCEL_CHAN(X, 0),
	KXSD9_ACCEL_CHAN(Y, 1),
	KXSD9_ACCEL_CHAN(Z, 2),
	{
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.indexed = 1,
		.address = KXSD9_REG_AUX,
		.scan_index = 3,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
			.shift = 4,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(4),
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

/* Four channels apart from timestamp, scan mask = 0x0f */
static const unsigned long kxsd9_scan_masks[] = { 0xf, 0 };

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
	st->dev = parent;
	st->map = map;

	indio_dev->channels = kxsd9_channels;
	indio_dev->num_channels = ARRAY_SIZE(kxsd9_channels);
	indio_dev->name = name;
	indio_dev->dev.parent = parent;
	indio_dev->info = &kxsd9_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = kxsd9_scan_masks;

	kxsd9_power_up(st);

	ret = iio_triggered_buffer_setup(indio_dev,
					 iio_pollfunc_store_time,
					 kxsd9_trigger_handler,
					 NULL);
	if (ret) {
		dev_err(parent, "triggered buffer setup failed\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_cleanup_buffer;

	dev_set_drvdata(parent, indio_dev);

	return 0;

err_cleanup_buffer:
	iio_triggered_buffer_cleanup(indio_dev);

	return ret;
}
EXPORT_SYMBOL(kxsd9_common_probe);

int kxsd9_common_remove(struct device *parent)
{
	struct iio_dev *indio_dev = dev_get_drvdata(parent);

	iio_triggered_buffer_cleanup(indio_dev);
	iio_device_unregister(indio_dev);

	return 0;
}
EXPORT_SYMBOL(kxsd9_common_remove);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Kionix KXSD9 driver");
MODULE_LICENSE("GPL v2");

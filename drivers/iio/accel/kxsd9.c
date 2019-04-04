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
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
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
 * @orientation: mounting matrix, flipped axis etc
 * @regs: regulators for this device, VDD and IOVDD
 * @scale: the current scaling setting
 */
struct kxsd9_state {
	struct device *dev;
	struct regmap *map;
	struct iio_mount_matrix orientation;
	struct regulator_bulk_data regs[2];
	u8 scale;
};

#define KXSD9_SCALE_2G "0.011978"
#define KXSD9_SCALE_4G "0.023927"
#define KXSD9_SCALE_6G "0.035934"
#define KXSD9_SCALE_8G "0.047853"

/* reverse order */
static const int kxsd9_micro_scales[4] = { 47853, 35934, 23927, 11978 };

#define KXSD9_ZERO_G_OFFSET -2048

/*
 * Regulator names
 */
static const char kxsd9_reg_vdd[] = "vdd";
static const char kxsd9_reg_iovdd[] = "iovdd";

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

	/* Cached scale when the sensor is powered down */
	st->scale = i;

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
	struct kxsd9_state *st = iio_priv(indio_dev);

	pm_runtime_get_sync(st->dev);

	if (mask == IIO_CHAN_INFO_SCALE) {
		/* Check no integer component */
		if (val)
			return -EINVAL;
		ret = kxsd9_write_scale(indio_dev, val2);
	}

	pm_runtime_mark_last_busy(st->dev);
	pm_runtime_put_autosuspend(st->dev);

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

	pm_runtime_get_sync(st->dev);

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
	pm_runtime_mark_last_busy(st->dev);
	pm_runtime_put_autosuspend(st->dev);

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

static int kxsd9_buffer_preenable(struct iio_dev *indio_dev)
{
	struct kxsd9_state *st = iio_priv(indio_dev);

	pm_runtime_get_sync(st->dev);

	return 0;
}

static int kxsd9_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct kxsd9_state *st = iio_priv(indio_dev);

	pm_runtime_mark_last_busy(st->dev);
	pm_runtime_put_autosuspend(st->dev);

	return 0;
}

static const struct iio_buffer_setup_ops kxsd9_buffer_setup_ops = {
	.preenable = kxsd9_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
	.postdisable = kxsd9_buffer_postdisable,
};

static const struct iio_mount_matrix *
kxsd9_get_mount_matrix(const struct iio_dev *indio_dev,
		       const struct iio_chan_spec *chan)
{
	struct kxsd9_state *st = iio_priv(indio_dev);

	return &st->orientation;
}

static const struct iio_chan_spec_ext_info kxsd9_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, kxsd9_get_mount_matrix),
	{ },
};

#define KXSD9_ACCEL_CHAN(axis, index)						\
	{								\
		.type = IIO_ACCEL,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
					BIT(IIO_CHAN_INFO_OFFSET),	\
		.ext_info = kxsd9_ext_info,				\
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

	/* Enable the regulators */
	ret = regulator_bulk_enable(ARRAY_SIZE(st->regs), st->regs);
	if (ret) {
		dev_err(st->dev, "Cannot enable regulators\n");
		return ret;
	}

	/* Power up */
	ret = regmap_write(st->map,
			   KXSD9_REG_CTRL_B,
			   KXSD9_CTRL_B_ENABLE);
	if (ret)
		return ret;

	/*
	 * Set 1000Hz LPF, 2g fullscale, motion wakeup threshold 1g,
	 * latched wakeup
	 */
	ret = regmap_write(st->map,
			   KXSD9_REG_CTRL_C,
			   KXSD9_CTRL_C_LP_1000HZ |
			   KXSD9_CTRL_C_MOT_LEV	|
			   KXSD9_CTRL_C_MOT_LAT |
			   st->scale);
	if (ret)
		return ret;

	/*
	 * Power-up time depends on the LPF setting, but typ 15.9 ms, let's
	 * set 20 ms to allow for some slack.
	 */
	msleep(20);

	return 0;
};

static int kxsd9_power_down(struct kxsd9_state *st)
{
	int ret;

	/*
	 * Set into low power mode - since there may be more users of the
	 * regulators this is the first step of the power saving: it will
	 * make sure we conserve power even if there are others users on the
	 * regulators.
	 */
	ret = regmap_update_bits(st->map,
				 KXSD9_REG_CTRL_B,
				 KXSD9_CTRL_B_ENABLE,
				 0);
	if (ret)
		return ret;

	/* Disable the regulators */
	ret = regulator_bulk_disable(ARRAY_SIZE(st->regs), st->regs);
	if (ret) {
		dev_err(st->dev, "Cannot disable regulators\n");
		return ret;
	}

	return 0;
}

static const struct iio_info kxsd9_info = {
	.read_raw = &kxsd9_read_raw,
	.write_raw = &kxsd9_write_raw,
	.attrs = &kxsd9_attribute_group,
};

/* Four channels apart from timestamp, scan mask = 0x0f */
static const unsigned long kxsd9_scan_masks[] = { 0xf, 0 };

int kxsd9_common_probe(struct device *dev,
		       struct regmap *map,
		       const char *name)
{
	struct iio_dev *indio_dev;
	struct kxsd9_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->dev = dev;
	st->map = map;

	indio_dev->channels = kxsd9_channels;
	indio_dev->num_channels = ARRAY_SIZE(kxsd9_channels);
	indio_dev->name = name;
	indio_dev->dev.parent = dev;
	indio_dev->info = &kxsd9_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = kxsd9_scan_masks;

	/* Read the mounting matrix, if present */
	ret = iio_read_mount_matrix(dev, "mount-matrix", &st->orientation);
	if (ret)
		return ret;

	/* Fetch and turn on regulators */
	st->regs[0].supply = kxsd9_reg_vdd;
	st->regs[1].supply = kxsd9_reg_iovdd;
	ret = devm_regulator_bulk_get(dev,
				      ARRAY_SIZE(st->regs),
				      st->regs);
	if (ret) {
		dev_err(dev, "Cannot get regulators\n");
		return ret;
	}
	/* Default scaling */
	st->scale = KXSD9_CTRL_C_FS_2G;

	kxsd9_power_up(st);

	ret = iio_triggered_buffer_setup(indio_dev,
					 iio_pollfunc_store_time,
					 kxsd9_trigger_handler,
					 &kxsd9_buffer_setup_ops);
	if (ret) {
		dev_err(dev, "triggered buffer setup failed\n");
		goto err_power_down;
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_cleanup_buffer;

	dev_set_drvdata(dev, indio_dev);

	/* Enable runtime PM */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Set autosuspend to two orders of magnitude larger than the
	 * start-up time. 20ms start-up time means 2000ms autosuspend,
	 * i.e. 2 seconds.
	 */
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put(dev);

	return 0;

err_cleanup_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
err_power_down:
	kxsd9_power_down(st);

	return ret;
}
EXPORT_SYMBOL(kxsd9_common_probe);

int kxsd9_common_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct kxsd9_state *st = iio_priv(indio_dev);

	iio_triggered_buffer_cleanup(indio_dev);
	iio_device_unregister(indio_dev);
	pm_runtime_get_sync(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	kxsd9_power_down(st);

	return 0;
}
EXPORT_SYMBOL(kxsd9_common_remove);

#ifdef CONFIG_PM
static int kxsd9_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct kxsd9_state *st = iio_priv(indio_dev);

	return kxsd9_power_down(st);
}

static int kxsd9_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct kxsd9_state *st = iio_priv(indio_dev);

	return kxsd9_power_up(st);
}
#endif /* CONFIG_PM */

const struct dev_pm_ops kxsd9_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(kxsd9_runtime_suspend,
			   kxsd9_runtime_resume, NULL)
};
EXPORT_SYMBOL(kxsd9_dev_pm_ops);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Kionix KXSD9 driver");
MODULE_LICENSE("GPL v2");

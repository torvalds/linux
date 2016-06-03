/*
 * Bosch BMC150 three-axis magnetic field sensor driver
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * This code is based on bmm050_api.c authored by contact@bosch.sensortec.com:
 *
 * (C) Copyright 2011~2014 Bosch Sensortec GmbH All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/regmap.h>

#include "bmc150_magn.h"

#define BMC150_MAGN_DRV_NAME			"bmc150_magn"
#define BMC150_MAGN_IRQ_NAME			"bmc150_magn_event"

#define BMC150_MAGN_REG_CHIP_ID			0x40
#define BMC150_MAGN_CHIP_ID_VAL			0x32

#define BMC150_MAGN_REG_X_L			0x42
#define BMC150_MAGN_REG_X_M			0x43
#define BMC150_MAGN_REG_Y_L			0x44
#define BMC150_MAGN_REG_Y_M			0x45
#define BMC150_MAGN_SHIFT_XY_L			3
#define BMC150_MAGN_REG_Z_L			0x46
#define BMC150_MAGN_REG_Z_M			0x47
#define BMC150_MAGN_SHIFT_Z_L			1
#define BMC150_MAGN_REG_RHALL_L			0x48
#define BMC150_MAGN_REG_RHALL_M			0x49
#define BMC150_MAGN_SHIFT_RHALL_L		2

#define BMC150_MAGN_REG_INT_STATUS		0x4A

#define BMC150_MAGN_REG_POWER			0x4B
#define BMC150_MAGN_MASK_POWER_CTL		BIT(0)

#define BMC150_MAGN_REG_OPMODE_ODR		0x4C
#define BMC150_MAGN_MASK_OPMODE			GENMASK(2, 1)
#define BMC150_MAGN_SHIFT_OPMODE		1
#define BMC150_MAGN_MODE_NORMAL			0x00
#define BMC150_MAGN_MODE_FORCED			0x01
#define BMC150_MAGN_MODE_SLEEP			0x03
#define BMC150_MAGN_MASK_ODR			GENMASK(5, 3)
#define BMC150_MAGN_SHIFT_ODR			3

#define BMC150_MAGN_REG_INT			0x4D

#define BMC150_MAGN_REG_INT_DRDY		0x4E
#define BMC150_MAGN_MASK_DRDY_EN		BIT(7)
#define BMC150_MAGN_SHIFT_DRDY_EN		7
#define BMC150_MAGN_MASK_DRDY_INT3		BIT(6)
#define BMC150_MAGN_MASK_DRDY_Z_EN		BIT(5)
#define BMC150_MAGN_MASK_DRDY_Y_EN		BIT(4)
#define BMC150_MAGN_MASK_DRDY_X_EN		BIT(3)
#define BMC150_MAGN_MASK_DRDY_DR_POLARITY	BIT(2)
#define BMC150_MAGN_MASK_DRDY_LATCHING		BIT(1)
#define BMC150_MAGN_MASK_DRDY_INT3_POLARITY	BIT(0)

#define BMC150_MAGN_REG_LOW_THRESH		0x4F
#define BMC150_MAGN_REG_HIGH_THRESH		0x50
#define BMC150_MAGN_REG_REP_XY			0x51
#define BMC150_MAGN_REG_REP_Z			0x52
#define BMC150_MAGN_REG_REP_DATAMASK		GENMASK(7, 0)

#define BMC150_MAGN_REG_TRIM_START		0x5D
#define BMC150_MAGN_REG_TRIM_END		0x71

#define BMC150_MAGN_XY_OVERFLOW_VAL		-4096
#define BMC150_MAGN_Z_OVERFLOW_VAL		-16384

/* Time from SUSPEND to SLEEP */
#define BMC150_MAGN_START_UP_TIME_MS		3

#define BMC150_MAGN_AUTO_SUSPEND_DELAY_MS	2000

#define BMC150_MAGN_REGVAL_TO_REPXY(regval) (((regval) * 2) + 1)
#define BMC150_MAGN_REGVAL_TO_REPZ(regval) ((regval) + 1)
#define BMC150_MAGN_REPXY_TO_REGVAL(rep) (((rep) - 1) / 2)
#define BMC150_MAGN_REPZ_TO_REGVAL(rep) ((rep) - 1)

enum bmc150_magn_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
	RHALL,
	AXIS_XYZ_MAX = RHALL,
	AXIS_XYZR_MAX,
};

enum bmc150_magn_power_modes {
	BMC150_MAGN_POWER_MODE_SUSPEND,
	BMC150_MAGN_POWER_MODE_SLEEP,
	BMC150_MAGN_POWER_MODE_NORMAL,
};

struct bmc150_magn_trim_regs {
	s8 x1;
	s8 y1;
	__le16 reserved1;
	u8 reserved2;
	__le16 z4;
	s8 x2;
	s8 y2;
	__le16 reserved3;
	__le16 z2;
	__le16 z1;
	__le16 xyz1;
	__le16 z3;
	s8 xy2;
	u8 xy1;
} __packed;

struct bmc150_magn_data {
	struct device *dev;
	/*
	 * 1. Protect this structure.
	 * 2. Serialize sequences that power on/off the device and access HW.
	 */
	struct mutex mutex;
	struct regmap *regmap;
	/* 4 x 32 bits for x, y z, 4 bytes align, 64 bits timestamp */
	s32 buffer[6];
	struct iio_trigger *dready_trig;
	bool dready_trigger_on;
	int max_odr;
	int irq;
};

static const struct {
	int freq;
	u8 reg_val;
} bmc150_magn_samp_freq_table[] = { {2, 0x01},
				    {6, 0x02},
				    {8, 0x03},
				    {10, 0x00},
				    {15, 0x04},
				    {20, 0x05},
				    {25, 0x06},
				    {30, 0x07} };

enum bmc150_magn_presets {
	LOW_POWER_PRESET,
	REGULAR_PRESET,
	ENHANCED_REGULAR_PRESET,
	HIGH_ACCURACY_PRESET
};

static const struct bmc150_magn_preset {
	u8 rep_xy;
	u8 rep_z;
	u8 odr;
} bmc150_magn_presets_table[] = {
	[LOW_POWER_PRESET] = {3, 3, 10},
	[REGULAR_PRESET] =  {9, 15, 10},
	[ENHANCED_REGULAR_PRESET] =  {15, 27, 10},
	[HIGH_ACCURACY_PRESET] =  {47, 83, 20},
};

#define BMC150_MAGN_DEFAULT_PRESET REGULAR_PRESET

static bool bmc150_magn_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMC150_MAGN_REG_POWER:
	case BMC150_MAGN_REG_OPMODE_ODR:
	case BMC150_MAGN_REG_INT:
	case BMC150_MAGN_REG_INT_DRDY:
	case BMC150_MAGN_REG_LOW_THRESH:
	case BMC150_MAGN_REG_HIGH_THRESH:
	case BMC150_MAGN_REG_REP_XY:
	case BMC150_MAGN_REG_REP_Z:
		return true;
	default:
		return false;
	};
}

static bool bmc150_magn_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMC150_MAGN_REG_X_L:
	case BMC150_MAGN_REG_X_M:
	case BMC150_MAGN_REG_Y_L:
	case BMC150_MAGN_REG_Y_M:
	case BMC150_MAGN_REG_Z_L:
	case BMC150_MAGN_REG_Z_M:
	case BMC150_MAGN_REG_RHALL_L:
	case BMC150_MAGN_REG_RHALL_M:
	case BMC150_MAGN_REG_INT_STATUS:
		return true;
	default:
		return false;
	}
}

const struct regmap_config bmc150_magn_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BMC150_MAGN_REG_TRIM_END,
	.cache_type = REGCACHE_RBTREE,

	.writeable_reg = bmc150_magn_is_writeable_reg,
	.volatile_reg = bmc150_magn_is_volatile_reg,
};
EXPORT_SYMBOL(bmc150_magn_regmap_config);

static int bmc150_magn_set_power_mode(struct bmc150_magn_data *data,
				      enum bmc150_magn_power_modes mode,
				      bool state)
{
	int ret;

	switch (mode) {
	case BMC150_MAGN_POWER_MODE_SUSPEND:
		ret = regmap_update_bits(data->regmap, BMC150_MAGN_REG_POWER,
					 BMC150_MAGN_MASK_POWER_CTL, !state);
		if (ret < 0)
			return ret;
		usleep_range(BMC150_MAGN_START_UP_TIME_MS * 1000, 20000);
		return 0;
	case BMC150_MAGN_POWER_MODE_SLEEP:
		return regmap_update_bits(data->regmap,
					  BMC150_MAGN_REG_OPMODE_ODR,
					  BMC150_MAGN_MASK_OPMODE,
					  BMC150_MAGN_MODE_SLEEP <<
					  BMC150_MAGN_SHIFT_OPMODE);
	case BMC150_MAGN_POWER_MODE_NORMAL:
		return regmap_update_bits(data->regmap,
					  BMC150_MAGN_REG_OPMODE_ODR,
					  BMC150_MAGN_MASK_OPMODE,
					  BMC150_MAGN_MODE_NORMAL <<
					  BMC150_MAGN_SHIFT_OPMODE);
	}

	return -EINVAL;
}

static int bmc150_magn_set_power_state(struct bmc150_magn_data *data, bool on)
{
#ifdef CONFIG_PM
	int ret;

	if (on) {
		ret = pm_runtime_get_sync(data->dev);
	} else {
		pm_runtime_mark_last_busy(data->dev);
		ret = pm_runtime_put_autosuspend(data->dev);
	}

	if (ret < 0) {
		dev_err(data->dev,
			"failed to change power state to %d\n", on);
		if (on)
			pm_runtime_put_noidle(data->dev);

		return ret;
	}
#endif

	return 0;
}

static int bmc150_magn_get_odr(struct bmc150_magn_data *data, int *val)
{
	int ret, reg_val;
	u8 i, odr_val;

	ret = regmap_read(data->regmap, BMC150_MAGN_REG_OPMODE_ODR, &reg_val);
	if (ret < 0)
		return ret;
	odr_val = (reg_val & BMC150_MAGN_MASK_ODR) >> BMC150_MAGN_SHIFT_ODR;

	for (i = 0; i < ARRAY_SIZE(bmc150_magn_samp_freq_table); i++)
		if (bmc150_magn_samp_freq_table[i].reg_val == odr_val) {
			*val = bmc150_magn_samp_freq_table[i].freq;
			return 0;
		}

	return -EINVAL;
}

static int bmc150_magn_set_odr(struct bmc150_magn_data *data, int val)
{
	int ret;
	u8 i;

	for (i = 0; i < ARRAY_SIZE(bmc150_magn_samp_freq_table); i++) {
		if (bmc150_magn_samp_freq_table[i].freq == val) {
			ret = regmap_update_bits(data->regmap,
						 BMC150_MAGN_REG_OPMODE_ODR,
						 BMC150_MAGN_MASK_ODR,
						 bmc150_magn_samp_freq_table[i].
						 reg_val <<
						 BMC150_MAGN_SHIFT_ODR);
			if (ret < 0)
				return ret;
			return 0;
		}
	}

	return -EINVAL;
}

static int bmc150_magn_set_max_odr(struct bmc150_magn_data *data, int rep_xy,
				   int rep_z, int odr)
{
	int ret, reg_val, max_odr;

	if (rep_xy <= 0) {
		ret = regmap_read(data->regmap, BMC150_MAGN_REG_REP_XY,
				  &reg_val);
		if (ret < 0)
			return ret;
		rep_xy = BMC150_MAGN_REGVAL_TO_REPXY(reg_val);
	}
	if (rep_z <= 0) {
		ret = regmap_read(data->regmap, BMC150_MAGN_REG_REP_Z,
				  &reg_val);
		if (ret < 0)
			return ret;
		rep_z = BMC150_MAGN_REGVAL_TO_REPZ(reg_val);
	}
	if (odr <= 0) {
		ret = bmc150_magn_get_odr(data, &odr);
		if (ret < 0)
			return ret;
	}
	/* the maximum selectable read-out frequency from datasheet */
	max_odr = 1000000 / (145 * rep_xy + 500 * rep_z + 980);
	if (odr > max_odr) {
		dev_err(data->dev,
			"Can't set oversampling with sampling freq %d\n",
			odr);
		return -EINVAL;
	}
	data->max_odr = max_odr;

	return 0;
}

static s32 bmc150_magn_compensate_x(struct bmc150_magn_trim_regs *tregs, s16 x,
				    u16 rhall)
{
	s16 val;
	u16 xyz1 = le16_to_cpu(tregs->xyz1);

	if (x == BMC150_MAGN_XY_OVERFLOW_VAL)
		return S32_MIN;

	if (!rhall)
		rhall = xyz1;

	val = ((s16)(((u16)((((s32)xyz1) << 14) / rhall)) - ((u16)0x4000)));
	val = ((s16)((((s32)x) * ((((((((s32)tregs->xy2) * ((((s32)val) *
	      ((s32)val)) >> 7)) + (((s32)val) *
	      ((s32)(((s16)tregs->xy1) << 7)))) >> 9) + ((s32)0x100000)) *
	      ((s32)(((s16)tregs->x2) + ((s16)0xA0)))) >> 12)) >> 13)) +
	      (((s16)tregs->x1) << 3);

	return (s32)val;
}

static s32 bmc150_magn_compensate_y(struct bmc150_magn_trim_regs *tregs, s16 y,
				    u16 rhall)
{
	s16 val;
	u16 xyz1 = le16_to_cpu(tregs->xyz1);

	if (y == BMC150_MAGN_XY_OVERFLOW_VAL)
		return S32_MIN;

	if (!rhall)
		rhall = xyz1;

	val = ((s16)(((u16)((((s32)xyz1) << 14) / rhall)) - ((u16)0x4000)));
	val = ((s16)((((s32)y) * ((((((((s32)tregs->xy2) * ((((s32)val) *
	      ((s32)val)) >> 7)) + (((s32)val) *
	      ((s32)(((s16)tregs->xy1) << 7)))) >> 9) + ((s32)0x100000)) *
	      ((s32)(((s16)tregs->y2) + ((s16)0xA0)))) >> 12)) >> 13)) +
	      (((s16)tregs->y1) << 3);

	return (s32)val;
}

static s32 bmc150_magn_compensate_z(struct bmc150_magn_trim_regs *tregs, s16 z,
				    u16 rhall)
{
	s32 val;
	u16 xyz1 = le16_to_cpu(tregs->xyz1);
	u16 z1 = le16_to_cpu(tregs->z1);
	s16 z2 = le16_to_cpu(tregs->z2);
	s16 z3 = le16_to_cpu(tregs->z3);
	s16 z4 = le16_to_cpu(tregs->z4);

	if (z == BMC150_MAGN_Z_OVERFLOW_VAL)
		return S32_MIN;

	val = (((((s32)(z - z4)) << 15) - ((((s32)z3) * ((s32)(((s16)rhall) -
	      ((s16)xyz1)))) >> 2)) / (z2 + ((s16)(((((s32)z1) *
	      ((((s16)rhall) << 1))) + (1 << 15)) >> 16))));

	return val;
}

static int bmc150_magn_read_xyz(struct bmc150_magn_data *data, s32 *buffer)
{
	int ret;
	__le16 values[AXIS_XYZR_MAX];
	s16 raw_x, raw_y, raw_z;
	u16 rhall;
	struct bmc150_magn_trim_regs tregs;

	ret = regmap_bulk_read(data->regmap, BMC150_MAGN_REG_X_L,
			       values, sizeof(values));
	if (ret < 0)
		return ret;

	raw_x = (s16)le16_to_cpu(values[AXIS_X]) >> BMC150_MAGN_SHIFT_XY_L;
	raw_y = (s16)le16_to_cpu(values[AXIS_Y]) >> BMC150_MAGN_SHIFT_XY_L;
	raw_z = (s16)le16_to_cpu(values[AXIS_Z]) >> BMC150_MAGN_SHIFT_Z_L;
	rhall = le16_to_cpu(values[RHALL]) >> BMC150_MAGN_SHIFT_RHALL_L;

	ret = regmap_bulk_read(data->regmap, BMC150_MAGN_REG_TRIM_START,
			       &tregs, sizeof(tregs));
	if (ret < 0)
		return ret;

	buffer[AXIS_X] = bmc150_magn_compensate_x(&tregs, raw_x, rhall);
	buffer[AXIS_Y] = bmc150_magn_compensate_y(&tregs, raw_y, rhall);
	buffer[AXIS_Z] = bmc150_magn_compensate_z(&tregs, raw_z, rhall);

	return 0;
}

static int bmc150_magn_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret, tmp;
	s32 values[AXIS_XYZ_MAX];

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;
		mutex_lock(&data->mutex);

		ret = bmc150_magn_set_power_state(data, true);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return ret;
		}

		ret = bmc150_magn_read_xyz(data, values);
		if (ret < 0) {
			bmc150_magn_set_power_state(data, false);
			mutex_unlock(&data->mutex);
			return ret;
		}
		*val = values[chan->scan_index];

		ret = bmc150_magn_set_power_state(data, false);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return ret;
		}

		mutex_unlock(&data->mutex);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * The API/driver performs an off-chip temperature
		 * compensation and outputs x/y/z magnetic field data in
		 * 16 LSB/uT to the upper application layer.
		 */
		*val = 0;
		*val2 = 625;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = bmc150_magn_get_odr(data, val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->channel2) {
		case IIO_MOD_X:
		case IIO_MOD_Y:
			ret = regmap_read(data->regmap, BMC150_MAGN_REG_REP_XY,
					  &tmp);
			if (ret < 0)
				return ret;
			*val = BMC150_MAGN_REGVAL_TO_REPXY(tmp);
			return IIO_VAL_INT;
		case IIO_MOD_Z:
			ret = regmap_read(data->regmap, BMC150_MAGN_REG_REP_Z,
					  &tmp);
			if (ret < 0)
				return ret;
			*val = BMC150_MAGN_REGVAL_TO_REPZ(tmp);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int bmc150_magn_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val > data->max_odr)
			return -EINVAL;
		mutex_lock(&data->mutex);
		ret = bmc150_magn_set_odr(data, val);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->channel2) {
		case IIO_MOD_X:
		case IIO_MOD_Y:
			if (val < 1 || val > 511)
				return -EINVAL;
			mutex_lock(&data->mutex);
			ret = bmc150_magn_set_max_odr(data, val, 0, 0);
			if (ret < 0) {
				mutex_unlock(&data->mutex);
				return ret;
			}
			ret = regmap_update_bits(data->regmap,
						 BMC150_MAGN_REG_REP_XY,
						 BMC150_MAGN_REG_REP_DATAMASK,
						 BMC150_MAGN_REPXY_TO_REGVAL
						 (val));
			mutex_unlock(&data->mutex);
			return ret;
		case IIO_MOD_Z:
			if (val < 1 || val > 256)
				return -EINVAL;
			mutex_lock(&data->mutex);
			ret = bmc150_magn_set_max_odr(data, 0, val, 0);
			if (ret < 0) {
				mutex_unlock(&data->mutex);
				return ret;
			}
			ret = regmap_update_bits(data->regmap,
						 BMC150_MAGN_REG_REP_Z,
						 BMC150_MAGN_REG_REP_DATAMASK,
						 BMC150_MAGN_REPZ_TO_REGVAL
						 (val));
			mutex_unlock(&data->mutex);
			return ret;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static ssize_t bmc150_magn_show_samp_freq_avail(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	size_t len = 0;
	u8 i;

	for (i = 0; i < ARRAY_SIZE(bmc150_magn_samp_freq_table); i++) {
		if (bmc150_magn_samp_freq_table[i].freq > data->max_odr)
			break;
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 bmc150_magn_samp_freq_table[i].freq);
	}
	/* replace last space with a newline */
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(bmc150_magn_show_samp_freq_avail);

static struct attribute *bmc150_magn_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bmc150_magn_attrs_group = {
	.attrs = bmc150_magn_attributes,
};

#define BMC150_MAGN_CHANNEL(_axis) {					\
	.type = IIO_MAGN,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) |	\
				    BIT(IIO_CHAN_INFO_SCALE),		\
	.scan_index = AXIS_##_axis,					\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 32,						\
		.storagebits = 32,					\
		.endianness = IIO_LE					\
	},								\
}

static const struct iio_chan_spec bmc150_magn_channels[] = {
	BMC150_MAGN_CHANNEL(X),
	BMC150_MAGN_CHANNEL(Y),
	BMC150_MAGN_CHANNEL(Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_info bmc150_magn_info = {
	.attrs = &bmc150_magn_attrs_group,
	.read_raw = bmc150_magn_read_raw,
	.write_raw = bmc150_magn_write_raw,
	.driver_module = THIS_MODULE,
};

static const unsigned long bmc150_magn_scan_masks[] = {
					BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z),
					0};

static irqreturn_t bmc150_magn_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = bmc150_magn_read_xyz(data, data->buffer);
	if (ret < 0)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   pf->timestamp);

err:
	mutex_unlock(&data->mutex);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int bmc150_magn_init(struct bmc150_magn_data *data)
{
	int ret, chip_id;
	struct bmc150_magn_preset preset;

	ret = bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_SUSPEND,
					 false);
	if (ret < 0) {
		dev_err(data->dev,
			"Failed to bring up device from suspend mode\n");
		return ret;
	}

	ret = regmap_read(data->regmap, BMC150_MAGN_REG_CHIP_ID, &chip_id);
	if (ret < 0) {
		dev_err(data->dev, "Failed reading chip id\n");
		goto err_poweroff;
	}
	if (chip_id != BMC150_MAGN_CHIP_ID_VAL) {
		dev_err(data->dev, "Invalid chip id 0x%x\n", chip_id);
		ret = -ENODEV;
		goto err_poweroff;
	}
	dev_dbg(data->dev, "Chip id %x\n", chip_id);

	preset = bmc150_magn_presets_table[BMC150_MAGN_DEFAULT_PRESET];
	ret = bmc150_magn_set_odr(data, preset.odr);
	if (ret < 0) {
		dev_err(data->dev, "Failed to set ODR to %d\n",
			preset.odr);
		goto err_poweroff;
	}

	ret = regmap_write(data->regmap, BMC150_MAGN_REG_REP_XY,
			   BMC150_MAGN_REPXY_TO_REGVAL(preset.rep_xy));
	if (ret < 0) {
		dev_err(data->dev, "Failed to set REP XY to %d\n",
			preset.rep_xy);
		goto err_poweroff;
	}

	ret = regmap_write(data->regmap, BMC150_MAGN_REG_REP_Z,
			   BMC150_MAGN_REPZ_TO_REGVAL(preset.rep_z));
	if (ret < 0) {
		dev_err(data->dev, "Failed to set REP Z to %d\n",
			preset.rep_z);
		goto err_poweroff;
	}

	ret = bmc150_magn_set_max_odr(data, preset.rep_xy, preset.rep_z,
				      preset.odr);
	if (ret < 0)
		goto err_poweroff;

	ret = bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_NORMAL,
					 true);
	if (ret < 0) {
		dev_err(data->dev, "Failed to power on device\n");
		goto err_poweroff;
	}

	return 0;

err_poweroff:
	bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_SUSPEND, true);
	return ret;
}

static int bmc150_magn_reset_intr(struct bmc150_magn_data *data)
{
	int tmp;

	/*
	 * Data Ready (DRDY) is always cleared after
	 * readout of data registers ends.
	 */
	return regmap_read(data->regmap, BMC150_MAGN_REG_X_L, &tmp);
}

static int bmc150_magn_trig_try_reen(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret;

	if (!data->dready_trigger_on)
		return 0;

	mutex_lock(&data->mutex);
	ret = bmc150_magn_reset_intr(data);
	mutex_unlock(&data->mutex);

	return ret;
}

static int bmc150_magn_data_rdy_trigger_set_state(struct iio_trigger *trig,
						  bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&data->mutex);
	if (state == data->dready_trigger_on)
		goto err_unlock;

	ret = regmap_update_bits(data->regmap, BMC150_MAGN_REG_INT_DRDY,
				 BMC150_MAGN_MASK_DRDY_EN,
				 state << BMC150_MAGN_SHIFT_DRDY_EN);
	if (ret < 0)
		goto err_unlock;

	data->dready_trigger_on = state;

	if (state) {
		ret = bmc150_magn_reset_intr(data);
		if (ret < 0)
			goto err_unlock;
	}
	mutex_unlock(&data->mutex);

	return 0;

err_unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static const struct iio_trigger_ops bmc150_magn_trigger_ops = {
	.set_trigger_state = bmc150_magn_data_rdy_trigger_set_state,
	.try_reenable = bmc150_magn_trig_try_reen,
	.owner = THIS_MODULE,
};

static int bmc150_magn_buffer_preenable(struct iio_dev *indio_dev)
{
	struct bmc150_magn_data *data = iio_priv(indio_dev);

	return bmc150_magn_set_power_state(data, true);
}

static int bmc150_magn_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct bmc150_magn_data *data = iio_priv(indio_dev);

	return bmc150_magn_set_power_state(data, false);
}

static const struct iio_buffer_setup_ops bmc150_magn_buffer_setup_ops = {
	.preenable = bmc150_magn_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
	.postdisable = bmc150_magn_buffer_postdisable,
};

static const char *bmc150_magn_match_acpi_device(struct device *dev)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;

	return dev_name(dev);
}

int bmc150_magn_probe(struct device *dev, struct regmap *regmap,
		      int irq, const char *name)
{
	struct bmc150_magn_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);
	data->regmap = regmap;
	data->irq = irq;
	data->dev = dev;

	if (!name && ACPI_HANDLE(dev))
		name = bmc150_magn_match_acpi_device(dev);

	mutex_init(&data->mutex);

	ret = bmc150_magn_init(data);
	if (ret < 0)
		return ret;

	indio_dev->dev.parent = dev;
	indio_dev->channels = bmc150_magn_channels;
	indio_dev->num_channels = ARRAY_SIZE(bmc150_magn_channels);
	indio_dev->available_scan_masks = bmc150_magn_scan_masks;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bmc150_magn_info;

	if (irq > 0) {
		data->dready_trig = devm_iio_trigger_alloc(dev,
							   "%s-dev%d",
							   indio_dev->name,
							   indio_dev->id);
		if (!data->dready_trig) {
			ret = -ENOMEM;
			dev_err(dev, "iio trigger alloc failed\n");
			goto err_poweroff;
		}

		data->dready_trig->dev.parent = dev;
		data->dready_trig->ops = &bmc150_magn_trigger_ops;
		iio_trigger_set_drvdata(data->dready_trig, indio_dev);
		ret = iio_trigger_register(data->dready_trig);
		if (ret) {
			dev_err(dev, "iio trigger register failed\n");
			goto err_poweroff;
		}

		ret = request_threaded_irq(irq,
					   iio_trigger_generic_data_rdy_poll,
					   NULL,
					   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					   BMC150_MAGN_IRQ_NAME,
					   data->dready_trig);
		if (ret < 0) {
			dev_err(dev, "request irq %d failed\n", irq);
			goto err_trigger_unregister;
		}
	}

	ret = iio_triggered_buffer_setup(indio_dev,
					 iio_pollfunc_store_time,
					 bmc150_magn_trigger_handler,
					 &bmc150_magn_buffer_setup_ops);
	if (ret < 0) {
		dev_err(dev, "iio triggered buffer setup failed\n");
		goto err_free_irq;
	}

	ret = pm_runtime_set_active(dev);
	if (ret)
		goto err_buffer_cleanup;

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev,
					 BMC150_MAGN_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(dev, "unable to register iio device\n");
		goto err_buffer_cleanup;
	}

	dev_dbg(dev, "Registered device %s\n", name);
	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
err_free_irq:
	if (irq > 0)
		free_irq(irq, data->dready_trig);
err_trigger_unregister:
	if (data->dready_trig)
		iio_trigger_unregister(data->dready_trig);
err_poweroff:
	bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_SUSPEND, true);
	return ret;
}
EXPORT_SYMBOL(bmc150_magn_probe);

int bmc150_magn_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_magn_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	iio_triggered_buffer_cleanup(indio_dev);

	if (data->irq > 0)
		free_irq(data->irq, data->dready_trig);

	if (data->dready_trig)
		iio_trigger_unregister(data->dready_trig);

	mutex_lock(&data->mutex);
	bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_SUSPEND, true);
	mutex_unlock(&data->mutex);

	return 0;
}
EXPORT_SYMBOL(bmc150_magn_remove);

#ifdef CONFIG_PM
static int bmc150_magn_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_SLEEP,
					 true);
	mutex_unlock(&data->mutex);
	if (ret < 0) {
		dev_err(dev, "powering off device failed\n");
		return ret;
	}
	return 0;
}

/*
 * Should be called with data->mutex held.
 */
static int bmc150_magn_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_magn_data *data = iio_priv(indio_dev);

	return bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_NORMAL,
					  true);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int bmc150_magn_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_SLEEP,
					 true);
	mutex_unlock(&data->mutex);

	return ret;
}

static int bmc150_magn_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmc150_magn_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = bmc150_magn_set_power_mode(data, BMC150_MAGN_POWER_MODE_NORMAL,
					 true);
	mutex_unlock(&data->mutex);

	return ret;
}
#endif

const struct dev_pm_ops bmc150_magn_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bmc150_magn_suspend, bmc150_magn_resume)
	SET_RUNTIME_PM_OPS(bmc150_magn_runtime_suspend,
			   bmc150_magn_runtime_resume, NULL)
};
EXPORT_SYMBOL(bmc150_magn_pm_ops);

MODULE_AUTHOR("Irina Tirdea <irina.tirdea@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 magnetometer core driver");

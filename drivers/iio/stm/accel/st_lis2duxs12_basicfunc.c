// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 basic function sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>
#include <linux/module.h>
#include <linux/version.h>

#include "st_lis2duxs12.h"

static const unsigned long st_lis2duxs12_event_available_scan_masks[] = {
	BIT(0), 0x0
};

static const struct iio_chan_spec st_lis2duxs12_wk_channels[] = {
	{
		.type = STM_IIO_GESTURE,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 8,
			.storagebits = 8,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec st_lis2duxs12_ff_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_GESTURE, thr),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec st_lis2duxs12_tap_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_TAP, thr),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec st_lis2duxs12_dtap_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_TAP_TAP, thr),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec st_lis2duxs12_ttap_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_GESTURE, thr),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec st_lis2duxs12_6D_channels[] = {
	{
		.type = STM_IIO_GESTURE,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 8,
			.storagebits = 8,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec st_lis2duxs12_sleepchange_channels[] = {
	ST_LIS2DUXS12_EVENT_CHANNEL(STM_IIO_GESTURE, thr),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct st_lis2duxs12_ff_th st_lis2duxs12_free_fall_threshold[] = {
	[0] = {
		.val = 0x00,
		.mg = 156,
	},
	[1] = {
		.val = 0x01,
		.mg = 219,
	},
	[2] = {
		.val = 0x02,
		.mg = 250,
	},
	[3] = {
		.val = 0x03,
		.mg = 312,
	},
	[4] = {
		.val = 0x04,
		.mg = 344,
	},
	[5] = {
		.val = 0x05,
		.mg = 406,
	},
	[6] = {
		.val = 0x06,
		.mg = 469,
	},
	[7] = {
		.val = 0x07,
		.mg = 500,
	},
};

static const struct st_lis2duxs12_6D_th st_lis2duxs12_6D_threshold[] = {
	[0] = {
		.val = 0x00,
		.deg = 80,
	},
	[1] = {
		.val = 0x01,
		.deg = 70,
	},
	[2] = {
		.val = 0x02,
		.deg = 60,
	},
	[3] = {
		.val = 0x03,
		.deg = 50,
	},
};

/*
 * st_lis2duxs12_set_wake_up_thershold - set wake-up threshold in ug
 * @hw - ST ACC MEMS hw instance
 * @th_ug - wake-up threshold in ug (micro g)
 *
 * wake-up thershold (th_umss) is expressed in micro m/s^2, register
 * val is (th_umss * 2^6) / (1000000 * FS_XL(m/s^2))
 */
static int
st_lis2duxs12_set_wake_up_thershold(struct st_lis2duxs12_hw *hw,
				    int th_umss)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_dev *iio_dev;
	u8 val, max_th, fs_xl;
	int tmp, err, i;

	err = st_lis2duxs12_read_with_mask_locked(hw,
		hw->fs_table_entry[ST_LIS2DUXS12_ID_ACC].reg.addr,
		hw->fs_table_entry[ST_LIS2DUXS12_ID_ACC].reg.mask,
		&fs_xl);
	if (err < 0)
		return err;

	for (i = 0; i < hw->fs_table_entry->size; i++) {
		if (hw->fs_table_entry->fs_avl[i].val == fs_xl)
			break;
	}

	if (i == hw->fs_table_entry->size)
		return -EINVAL;

	tmp = (th_umss * 64) /
	      (hw->fs_table_entry->fs_avl[i].gain * 16384);
	val = (u8)tmp;
	max_th = ST_LIS2DUXS12_WK_THS_MASK >> ffs(ST_LIS2DUXS12_WK_THS_MASK);
	if (val > max_th)
		val = max_th;

	err = st_lis2duxs12_update_bits_locked(hw,
				ST_LIS2DUXS12_WAKE_UP_THS_ADDR,
				ST_LIS2DUXS12_WK_THS_MASK, val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[0] = th_umss;

	return 0;
}

/*
 * st_lis2duxs12_set_wake_up_duration - set wake-up duration in ms
 * @hw - ST ACC MEMS hw instance
 * @dur_ms - wake-up duration in ms
 *
 * wake-up duration register val is related to XL ODR
 */
static int
st_lis2duxs12_set_wake_up_duration(struct st_lis2duxs12_hw *hw,
				   int dur_ms)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, tmp, sensor_odr, err;
	u8 val, max_dur, odr_xl;

	err = st_lis2duxs12_read_with_mask_locked(hw,
		hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].reg.addr,
		hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].reg.mask,
		&odr_xl);
	if (err < 0)
		return err;

	if (odr_xl == 0) {
		dev_info(hw->dev, "use default ODR\n");
		odr_xl = hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[4].val;
	}

	for (i = 0; i < hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].size; i++) {
		if (odr_xl ==
		     hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].val)
			break;
	}

	if (i == hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].size)
		return -EINVAL;

	sensor_odr = ST_LIS2DUXS12_ODR_EXPAND(
		hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].hz,
		hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].uhz);

	tmp = dur_ms / (1000000 / (sensor_odr / 1000));
	val = (u8)tmp;
	max_dur = ST_LIS2DUXS12_WAKE_DUR_MASK >> ffs(ST_LIS2DUXS12_WAKE_DUR_MASK);
	if (val > max_dur)
		val = max_dur;

	err = st_lis2duxs12_update_bits_locked(hw,
				ST_LIS2DUXS12_WAKE_UP_DUR_ADDR,
				ST_LIS2DUXS12_WAKE_DUR_MASK, val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[1] = dur_ms;

	return 0;
}

/*
 * st_lis2duxs12_set_freefall_threshold - set free fall threshold detection mg
 * @hw - ST ACC MEMS hw instance
 * @th_mg - free fall threshold in mg
 */
static int
st_lis2duxs12_set_freefall_threshold(struct st_lis2duxs12_hw *hw,
				     int th_mg)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_free_fall_threshold); i++) {
		if (th_mg >= st_lis2duxs12_free_fall_threshold[i].mg)
			break;
	}

	if (i == ARRAY_SIZE(st_lis2duxs12_free_fall_threshold))
		return -EINVAL;

	err = st_lis2duxs12_update_bits_locked(hw,
			ST_LIS2DUXS12_FREE_FALL_ADDR,
			ST_LIS2DUXS12_FF_THS_MASK,
			st_lis2duxs12_free_fall_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_FF];
	sensor = iio_priv(iio_dev);
	sensor->conf[2] = th_mg;

	return 0;
}

/*
 * st_lis2duxs12_set_6D_threshold - set 6D threshold detection in degrees
 * @hw - ST ACC MEMS hw instance
 * @deg - 6D threshold in degrees
 */
static int st_lis2duxs12_set_6D_threshold(struct st_lis2duxs12_hw *hw,
					  int deg)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_6D_threshold); i++) {
		if (deg >= st_lis2duxs12_6D_threshold[i].deg)
			break;
	}

	if (i == ARRAY_SIZE(st_lis2duxs12_6D_threshold))
		return -EINVAL;

	err = st_lis2duxs12_update_bits_locked(hw,
				ST_LIS2DUXS12_SIXD_ADDR,
				ST_LIS2DUXS12_D6D_THS_MASK,
				st_lis2duxs12_6D_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_6D];
	sensor = iio_priv(iio_dev);
	sensor->conf[3] = deg;

	return 0;
}

static int
st_lis2duxs12_event_sensor_enable(struct st_lis2duxs12_sensor *sensor,
				  bool enable)
{
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int err, eint = !!enable;

	err = st_lis2duxs12_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	switch (sensor->id) {
	case ST_LIS2DUXS12_ID_WK:
		err = st_lis2duxs12_update_bits_locked(hw,
					hw->md_int_reg,
					ST_LIS2DUXS12_INT_WU_MASK,
					eint);
		if (err < 0)
			return err;

		err = st_lis2duxs12_update_bits_locked(hw,
					ST_LIS2DUXS12_CTRL1_ADDR,
					ST_LIS2DUXS12_WU_EN_MASK,
					eint ? 0x07 : 0);
		if (err < 0)
			return err;
		break;
	case ST_LIS2DUXS12_ID_FF:
		err = st_lis2duxs12_update_bits_locked(hw,
					hw->md_int_reg,
					ST_LIS2DUXS12_INT_FF_MASK,
					eint);
		if (err < 0)
			return err;
		break;
	case ST_LIS2DUXS12_ID_SC:
		err = st_lis2duxs12_update_bits_locked(hw,
					hw->md_int_reg,
					ST_LIS2DUXS12_INT_SLEEP_CHANGE_MASK,
					eint);
		if (err < 0)
			return err;
		break;
	case ST_LIS2DUXS12_ID_6D:
		err = st_lis2duxs12_update_bits_locked(hw,
					hw->md_int_reg,
					ST_LIS2DUXS12_INT_6D_MASK,
					eint);
		if (err < 0)
			return err;
		break;
	case ST_LIS2DUXS12_ID_TAP:
		err = st_lis2duxs12_update_bits_locked(hw,
					hw->md_int_reg,
					ST_LIS2DUXS12_INT_TAP_MASK,
					eint);
		if (err < 0)
			return err;

		err = st_lis2duxs12_update_bits_locked(hw,
					ST_LIS2DUXS12_TAP_CFG5_ADDR,
					ST_LIS2DUXS12_SINGLE_TAP_EN_MASK,
					eint);
		if (err < 0)
			return err;
		break;
	case ST_LIS2DUXS12_ID_DTAP:
		err = st_lis2duxs12_update_bits_locked(hw,
					hw->md_int_reg,
					ST_LIS2DUXS12_INT_TAP_MASK,
					eint);
		if (err < 0)
			return err;

		err = st_lis2duxs12_update_bits_locked(hw,
					ST_LIS2DUXS12_TAP_CFG5_ADDR,
					ST_LIS2DUXS12_DOUBLE_TAP_EN_MASK,
					eint);
		if (err < 0)
			return err;
		break;
	case ST_LIS2DUXS12_ID_TTAP:
		err = st_lis2duxs12_update_bits_locked(hw,
					hw->md_int_reg,
					ST_LIS2DUXS12_INT_TAP_MASK,
					eint);
		if (err < 0)
			return err;

		err = st_lis2duxs12_update_bits_locked(hw,
					ST_LIS2DUXS12_TAP_CFG5_ADDR,
					ST_LIS2DUXS12_TRIPLE_TAP_EN_MASK,
					eint);
		if (err < 0)
			return err;
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err >= 0) {
		err = st_lis2duxs12_update_bits_locked(hw,
				ST_LIS2DUXS12_INTERRUPT_CFG_ADDR,
				ST_LIS2DUXS12_INTERRUPTS_ENABLE_MASK,
				eint);
		if (eint == 0)
			hw->enable_mask &= ~BIT(sensor->id);
		else
			hw->enable_mask |= BIT(sensor->id);
	}

	return err;
}

static int
st_lis2duxs12_read_event_config(struct iio_dev *iio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

static int
st_lis2duxs12_write_event_config(struct iio_dev *iio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 int state)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);
	err = st_lis2duxs12_event_sensor_enable(sensor, state);
	mutex_unlock(&iio_dev->mlock);

	return err;
}

ssize_t st_lis2duxs12_wakeup_threshold_get(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[0]);
}

ssize_t st_lis2duxs12_wakeup_threshold_set(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lis2duxs12_set_wake_up_thershold(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[0] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

ssize_t st_lis2duxs12_wakeup_duration_get(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[1]);
}

ssize_t
st_lis2duxs12_wakeup_duration_set(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lis2duxs12_set_wake_up_duration(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[1] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

ssize_t st_lis2duxs12_freefall_threshold_get(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[2]);
}

ssize_t st_lis2duxs12_freefall_threshold_set(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lis2duxs12_set_freefall_threshold(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[2] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

ssize_t st_lis2duxs12_6D_threshold_get(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[3]);
}

ssize_t st_lis2duxs12_6D_threshold_set(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lis2duxs12_set_6D_threshold(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[3] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEVICE_ATTR(wakeup_threshold, 0644,
		       st_lis2duxs12_wakeup_threshold_get,
		       st_lis2duxs12_wakeup_threshold_set, 0);

static IIO_DEVICE_ATTR(wakeup_duration, 0644,
		       st_lis2duxs12_wakeup_duration_get,
		       st_lis2duxs12_wakeup_duration_set, 0);

static IIO_DEVICE_ATTR(freefall_threshold, 0644,
		       st_lis2duxs12_freefall_threshold_get,
		       st_lis2duxs12_freefall_threshold_set, 0);

static IIO_DEVICE_ATTR(sixd_threshold, 0644,
		       st_lis2duxs12_6D_threshold_get,
		       st_lis2duxs12_6D_threshold_set, 0);

static struct attribute *st_lis2duxs12_wk_attributes[] = {
	&iio_dev_attr_wakeup_threshold.dev_attr.attr,
	&iio_dev_attr_wakeup_duration.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2duxs12_wk_attribute_group = {
	.attrs = st_lis2duxs12_wk_attributes,
};

static const struct iio_info st_lis2duxs12_wk_info = {
	.attrs = &st_lis2duxs12_wk_attribute_group,
};

static struct attribute *st_lis2duxs12_ff_attributes[] = {
	&iio_dev_attr_freefall_threshold.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2duxs12_ff_attribute_group = {
	.attrs = st_lis2duxs12_ff_attributes,
};

static const struct iio_info st_lis2duxs12_ff_info = {
	.attrs = &st_lis2duxs12_ff_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

static struct attribute *st_lis2duxs12_sc_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2duxs12_sc_attribute_group = {
	.attrs = st_lis2duxs12_sc_attributes,
};

static const struct iio_info st_lis2duxs12_sc_info = {
	.attrs = &st_lis2duxs12_sc_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

static struct attribute *st_lis2duxs12_6D_attributes[] = {
	&iio_dev_attr_sixd_threshold.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2duxs12_6D_attribute_group = {
	.attrs = st_lis2duxs12_6D_attributes,
};

static const struct iio_info st_lis2duxs12_6D_info = {
	.attrs = &st_lis2duxs12_6D_attribute_group,
};

static struct attribute *st_lis2duxs12_tap_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2duxs12_tap_attribute_group = {
	.attrs = st_lis2duxs12_tap_attributes,
};

static const struct iio_info st_lis2duxs12_tap_info = {
	.attrs = &st_lis2duxs12_tap_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

static struct attribute *st_lis2duxs12_dtap_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2duxs12_dtap_attribute_group = {
	.attrs = st_lis2duxs12_dtap_attributes,
};

static const struct iio_info st_lis2duxs12_dtap_info = {
	.attrs = &st_lis2duxs12_dtap_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

static struct attribute *st_lis2duxs12_ttap_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2duxs12_ttap_attribute_group = {
	.attrs = st_lis2duxs12_ttap_attributes,
};

static const struct iio_info st_lis2duxs12_ttap_info = {
	.attrs = &st_lis2duxs12_ttap_attribute_group,
	.read_event_config = st_lis2duxs12_read_event_config,
	.write_event_config = st_lis2duxs12_write_event_config,
};

static struct iio_dev *
st_lis2duxs12_alloc_event_iiodev(struct st_lis2duxs12_hw *hw,
				 enum st_lis2duxs12_sensor_id id)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;
	sensor->watermark = 1;

	iio_dev->available_scan_masks = st_lis2duxs12_event_available_scan_masks;

	switch (id) {
	case ST_LIS2DUXS12_ID_WK:
		iio_dev->channels = st_lis2duxs12_wk_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_wk_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_wk", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_wk_info;
		/* request ODR @50 Hz to works properly */
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_FF:
		iio_dev->channels = st_lis2duxs12_ff_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_ff_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_ff", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_ff_info;
		/* request ODR @50 Hz to works properly */
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_SC:
		iio_dev->channels = st_lis2duxs12_sleepchange_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_sleepchange_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_sc", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_sc_info;
		/* request ODR @50 Hz to works properly */
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_6D:
		iio_dev->channels = st_lis2duxs12_6D_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_6D_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_6d", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_6D_info;
		/* request ODR @50 Hz to works properly */
		sensor->odr = 50;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_TAP:
		iio_dev->channels = st_lis2duxs12_tap_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_tap_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_tap", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_tap_info;
		/* request ODR @400 Hz to works properly */
		sensor->odr = 400;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_DTAP:
		iio_dev->channels = st_lis2duxs12_dtap_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_dtap_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_dtap", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_dtap_info;
		/* request ODR @400 Hz to works properly */
		sensor->odr = 400;
		sensor->uodr = 0;
		break;
	case ST_LIS2DUXS12_ID_TTAP:
		iio_dev->channels = st_lis2duxs12_ttap_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_ttap_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_ttap", hw->settings->id.name);
		iio_dev->info = &st_lis2duxs12_ttap_info;
		/* request ODR @400 Hz to works properly */
		sensor->odr = 400;
		sensor->uodr = 0;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

int st_lis2duxs12_event_handler(struct st_lis2duxs12_hw *hw)
{
	struct iio_dev *iio_dev;
	int status;
	s64 event;
	int err;

	if (hw->enable_mask & ST_LIS2DUXS12_BASIC_FUNC_ENABLED) {
		err = st_lis2duxs12_read_locked(hw,
				  ST_LIS2DUXS12_ALL_INT_SRC_ADDR,
				  &status, sizeof(status));
		if (err < 0)
			return IRQ_HANDLED;

		if (status & ST_LIS2DUXS12_FF_IA_ALL_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_FF];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}
		if (status & ST_LIS2DUXS12_WU_IA_ALL_MASK) {
			struct st_lis2duxs12_sensor *sensor;

			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_WK];
			sensor = iio_priv(iio_dev);
			iio_trigger_poll_chained(sensor->trig);
		}
		if (status & ST_LIS2DUXS12_SLEEP_CHANGE_ALL_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_SC];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}
		if (status & ST_LIS2DUXS12_D6D_IA_ALL_MASK) {
			struct st_lis2duxs12_sensor *sensor;

			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_6D];
			sensor = iio_priv(iio_dev);
			iio_trigger_poll_chained(sensor->trig);
		}
		if (status & ST_LIS2DUXS12_SINGLE_TAP_ALL_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_TAP];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_TAP, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}
		if (status & ST_LIS2DUXS12_DOUBLE_TAP_ALL_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_DTAP];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_TAP_TAP, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}
		if (status & ST_LIS2DUXS12_TRIPLE_TAP_ALL_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_TTAP];
			/* triple tap is not available as IIO type */
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}
	}

	return IRQ_HANDLED;
}

static inline int st_lis2duxs12_get_6D(struct st_lis2duxs12_hw *hw,
				       u8 *out)
{
	return st_lis2duxs12_read_with_mask_locked(hw,
					    ST_LIS2DUXS12_SIXD_SRC_ADDR,
					    ST_LIS2DUXS12_X_Y_Z_MASK,
					    out);
}

static inline int st_lis2duxs12_get_wk(struct st_lis2duxs12_hw *hw,
				       u8 *out)
{
	return st_lis2duxs12_read_with_mask_locked(hw,
					ST_LIS2DUXS12_WAKE_UP_SRC_ADDR,
					ST_LIS2DUXS12_WK_MASK, out);
}

static irqreturn_t st_lis2duxs12_6D_handler_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);

	st_lis2duxs12_get_6D(sensor->hw, &sensor->scan.event);
	iio_push_to_buffers_with_timestamp(iio_dev, &sensor->scan.event,
					   iio_get_time_ns(iio_dev));
	iio_trigger_notify_done(sensor->trig);

	return IRQ_HANDLED;
}

static irqreturn_t st_lis2duxs12_wk_handler_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);

	st_lis2duxs12_get_wk(sensor->hw, &sensor->scan.event);
	iio_push_to_buffers_with_timestamp(iio_dev, &sensor->scan.event,
					   iio_get_time_ns(iio_dev));
	iio_trigger_notify_done(sensor->trig);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops st_lis2duxs12_trigger_ops = {
	NULL,
};

static int st_lis2duxs12_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_lis2duxs12_event_sensor_enable(iio_priv(iio_dev),
						 true);
}

static int st_lis2duxs12_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_lis2duxs12_event_sensor_enable(iio_priv(iio_dev),
						 false);
}

static const struct iio_buffer_setup_ops st_lis2duxs12_buffer_ops = {
	.preenable = st_lis2duxs12_buffer_preenable,
#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
#endif /* LINUX_VERSION_CODE */
	.postdisable = st_lis2duxs12_buffer_postdisable,
};

/*
 * st_lis2duxs12_init_tap - initialize tap configuration
 *
 * This section can be customized
 */
static int st_lis2duxs12_init_tap(struct st_lis2duxs12_hw *hw)
{
	int err;

	err = regmap_write(hw->regmap,
			   ST_LIS2DUXS12_TAP_CFG0_ADDR,
			   0xc8);
	if (err)
		return err;

	err = regmap_write(hw->regmap,
			   ST_LIS2DUXS12_TAP_CFG1_ADDR,
			   0x28);
	if (err)
		return err;

	err = regmap_write(hw->regmap,
			   ST_LIS2DUXS12_TAP_CFG2_ADDR,
			   0x03);
	if (err)
		return err;

	err = regmap_write(hw->regmap,
			   ST_LIS2DUXS12_TAP_CFG3_ADDR,
			   0x84);
	if (err)
		return err;

	err = regmap_write(hw->regmap,
			   ST_LIS2DUXS12_TAP_CFG4_ADDR,
			   0x08);
	if (err)
		return err;

	err = regmap_write(hw->regmap,
			   ST_LIS2DUXS12_TAP_CFG6_ADDR,
			   0x0a);
	if (err)
		return err;

	return 0;
}

int st_lis2duxs12_probe_basicfunc(struct st_lis2duxs12_hw *hw)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_dev *iio_dev;
	irqreturn_t (*pthread[2])(int irq, void *p) = {
		[0] = st_lis2duxs12_wk_handler_thread,
		[1] = st_lis2duxs12_6D_handler_thread,
	};
	int i, err;

	for (i = ST_LIS2DUXS12_ID_FF;
	     i <= ST_LIS2DUXS12_ID_TTAP; i++) {
		hw->iio_devs[i] = st_lis2duxs12_alloc_event_iiodev(hw,
								   i);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	/* configure trigger sensors */
	for (i = ST_LIS2DUXS12_ID_WK; i <= ST_LIS2DUXS12_ID_6D; i++) {
		iio_dev = hw->iio_devs[i];

		err = devm_iio_triggered_buffer_setup(hw->dev, iio_dev,
				NULL, pthread[i - ST_LIS2DUXS12_ID_WK],
				&st_lis2duxs12_buffer_ops);
		if (err < 0)
			return err;

		sensor = iio_priv(iio_dev);
		sensor->trig = devm_iio_trigger_alloc(hw->dev,
						      "%s-trigger",
						      iio_dev->name);
		if (!sensor->trig)
			return -ENOMEM;

		iio_trigger_set_drvdata(sensor->trig, iio_dev);
		sensor->trig->ops = &st_lis2duxs12_trigger_ops;
		sensor->trig->dev.parent = hw->dev;

		err = devm_iio_trigger_register(hw->dev, sensor->trig);
		if (err)
			return err;

		iio_dev->trig = iio_trigger_get(sensor->trig);
	}

	err = st_lis2duxs12_init_tap(hw);
	if (err)
		return err;

	/* set default settings threshold */
	return st_lis2duxs12_set_wake_up_thershold(hw,
					   ST_LIS2DUXS12_DEFAULT_WK_TH);
}

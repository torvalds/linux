// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsvx events function sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/version.h>

#include "st_lsm6dsvx.h"

static const struct st_lsm6dsvx_ff_th st_lsm6dsvx_free_fall_threshold[] = {
	[0] = { .val = 0x00, .mg = 156 },
	[1] = { .val = 0x01, .mg = 219 },
	[2] = { .val = 0x02, .mg = 250 },
	[3] = { .val = 0x03, .mg = 312 },
	[4] = { .val = 0x04, .mg = 344 },
	[5] = { .val = 0x05, .mg = 406 },
	[6] = { .val = 0x06, .mg = 469 },
	[7] = { .val = 0x07, .mg = 500 },
};

static const struct st_lsm6dsvx_6D_th st_lsm6dsvx_6D_threshold[] = {
	[0] = { .val = 0x00, .deg = 80 },
	[1] = { .val = 0x01, .deg = 70 },
	[2] = { .val = 0x02, .deg = 60 },
	[3] = { .val = 0x03, .deg = 50 },
};
static const unsigned long st_lsm6dsvx_event_available_scan_masks[] = {
	BIT(0), 0x0
};

static const struct iio_chan_spec st_lsm6dsvx_wk_channels[] = {
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

static const struct iio_chan_spec st_lsm6dsvx_ff_channels[] = {
	ST_LSM6DSVX_EVENT_CHANNEL(STM_IIO_GESTURE, thr),
};

static const struct iio_chan_spec st_lsm6dsvx_sc_channels[] = {
	ST_LSM6DSVX_EVENT_CHANNEL(STM_IIO_GESTURE, thr),
};

static const struct iio_chan_spec st_lsm6dsvx_6D_channels[] = {
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

static const struct iio_chan_spec st_lsm6dsvx_tap_channels[] = {
	ST_LSM6DSVX_EVENT_CHANNEL(STM_IIO_TAP, thr),
};

static const struct iio_chan_spec st_lsm6dsvx_dtap_channels[] = {
	ST_LSM6DSVX_EVENT_CHANNEL(STM_IIO_TAP_TAP, thr),
};

/*
 * st_lsm6dsvx_set_wake_up_thershold - set wake-up threshold in ug
 *
 * @hw - ST IMU MEMS hw instance
 * @th_ug - wake-up threshold in ug (micro g)
 *
 * wake-up threshold register val = (th_ug * 2 ^ 6) / (1000000 * FS_XL)
 */
static int st_lsm6dsvx_set_wake_up_thershold(struct st_lsm6dsvx_hw *hw,
					     int th_ug)
{
	struct st_lsm6dsvx_sensor *sensor;
	u8 fs_xl_g[] = { 2, 16, 4, 8 };
	struct iio_dev *iio_dev;
	u8 val, fs_xl, max_th;
	int tmp, err;

	err = st_lsm6dsvx_read_with_mask(hw,
				hw->fs_table[ST_LSM6DSVX_ID_ACC].reg.addr,
				hw->fs_table[ST_LSM6DSVX_ID_ACC].reg.mask,
				&fs_xl);
	if (err < 0)
		return err;

	if (fs_xl >= ARRAY_SIZE(fs_xl_g))
		return -EINVAL;

	tmp = (th_ug * 64) / (fs_xl_g[fs_xl] * 1000000);

	val = (u8)tmp;
	max_th = ST_LSM6DSVX_WK_THS_MASK >> __ffs(ST_LSM6DSVX_WK_THS_MASK);
	if (val > max_th)
		val = max_th;

	err = st_lsm6dsvx_write_with_mask(hw, ST_LSM6DSVX_REG_WAKE_UP_THS_ADDR,
					  ST_LSM6DSVX_WK_THS_MASK, val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[0] = th_ug;

	return 0;
}

/*
 * st_lsm6dsvx_set_wake_up_duration - set wake-up duration in ms
 *
 * @hw - ST IMU MEMS hw instance
 * @dur_ms - wake-up duration in ms
 *
 * wake-up duration register val is related to XL ODR
 */
static int st_lsm6dsvx_set_wake_up_duration(struct st_lsm6dsvx_hw *hw,
					    int dur_ms)
{
	struct st_lsm6dsvx_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, tmp, sensor_odr, err;
	u8 val, odr_xl, max_dur;

	err = st_lsm6dsvx_read_with_mask(hw,
		hw->odr_table[ST_LSM6DSVX_ID_ACC].reg.addr,
		hw->odr_table[ST_LSM6DSVX_ID_ACC].reg.mask,
		&odr_xl);
	if (err < 0)
		return err;

	if (odr_xl == 0) {
		dev_info(hw->dev, "use default ODR (26 Hz)\n");
		odr_xl = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[2].val;
	}

	for (i = 0; i < hw->odr_table[ST_LSM6DSVX_ID_ACC].size; i++) {
		if (odr_xl ==
		     hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[i].val)
			break;
	}

	if (i == hw->odr_table[ST_LSM6DSVX_ID_ACC].size)
		return -EINVAL;


	sensor_odr = ST_LSM6DSVX_ODR_EXPAND(
		hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[i].hz,
		hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[i].uhz);

	tmp = dur_ms / (1000000 / (sensor_odr / 1000));
	val = (u8)tmp;
	max_dur = ST_LSM6DSVX_WAKE_DUR_MASK >>
		  __ffs(ST_LSM6DSVX_WAKE_DUR_MASK);
	if (val > max_dur)
		val = max_dur;

	err = st_lsm6dsvx_write_with_mask(hw, ST_LSM6DSVX_REG_WAKE_UP_DUR_ADDR,
					  ST_LSM6DSVX_WAKE_DUR_MASK, val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_WK];
	sensor = iio_priv(iio_dev);
	sensor->conf[1] = dur_ms;
	sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[i].hz;

	return 0;
}

/*
 * st_lsm6dsvx_set_freefall_threshold - set free fall threshold
 * detection mg
 *
 * @hw - ST IMU MEMS hw instance
 * @th_mg - free fall threshold in mg
 */
static int st_lsm6dsvx_set_freefall_threshold(struct st_lsm6dsvx_hw *hw,
					      int th_mg)
{
	struct st_lsm6dsvx_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsvx_free_fall_threshold); i++) {
		if (th_mg >= st_lsm6dsvx_free_fall_threshold[i].mg)
			break;
	}

	if (i == ARRAY_SIZE(st_lsm6dsvx_free_fall_threshold))
		return -EINVAL;

	err = st_lsm6dsvx_write_with_mask(hw, ST_LSM6DSVX_REG_FREE_FALL_ADDR,
					  ST_LSM6DSVX_FF_THS_MASK,
					  st_lsm6dsvx_free_fall_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_FF];
	sensor = iio_priv(iio_dev);
	sensor->conf[2] = th_mg;

	return 0;
}

/*
 * st_lsm6dsvx_set_6D_threshold - set 6D threshold detection in degrees
 *
 * @hw - ST IMU MEMS hw instance
 * @deg - 6D threshold in degrees
 */
static int st_lsm6dsvx_set_6D_threshold(struct st_lsm6dsvx_hw *hw,
					int deg)
{
	struct st_lsm6dsvx_sensor *sensor;
	struct iio_dev *iio_dev;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsvx_6D_threshold); i++) {
		if (deg >= st_lsm6dsvx_6D_threshold[i].deg)
			break;
	}

	if (i == ARRAY_SIZE(st_lsm6dsvx_6D_threshold))
		return -EINVAL;

	err = st_lsm6dsvx_write_with_mask(hw, ST_LSM6DSVX_REG_TAP_THS_6D_ADDR,
					  ST_LSM6DSVX_SIXD_THS_MASK,
					  st_lsm6dsvx_6D_threshold[i].val);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_6D];
	sensor = iio_priv(iio_dev);
	sensor->conf[3] = deg;

	return 0;
}

/*
 * st_lsm6dsvx_init_tap - initialize tap detection to default value
 *
 * @hw - ST IMU MEMS hw instance
 */
static int st_lsm6dsvx_init_tap(struct st_lsm6dsvx_hw *hw)
{
	int err;

	err = regmap_update_bits(hw->regmap,
				 ST_LSM6DSVX_REG_TAP_CFG0_ADDR,
				 ST_LSM6DSVX_REG_TAP_EN_MASK,
				 FIELD_PREP(ST_LSM6DSVX_REG_TAP_EN_MASK, 0x07));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LSM6DSVX_REG_TAP_CFG1_ADDR,
				 ST_LSM6DSVX_TAP_THS_X_MASK,
				 FIELD_PREP(ST_LSM6DSVX_TAP_THS_X_MASK, 0x09));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LSM6DSVX_REG_TAP_CFG2_ADDR,
				 ST_LSM6DSVX_TAP_THS_Y_MASK,
				 FIELD_PREP(ST_LSM6DSVX_TAP_THS_Y_MASK, 0x09));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LSM6DSVX_REG_TAP_THS_6D_ADDR,
				 ST_LSM6DSVX_TAP_THS_Z_MASK,
				 FIELD_PREP(ST_LSM6DSVX_TAP_THS_Z_MASK, 0x09));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LSM6DSVX_REG_TAP_DUR_ADDR,
				 ST_LSM6DSVX_SHOCK_MASK,
				 FIELD_PREP(ST_LSM6DSVX_SHOCK_MASK, 0x02));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LSM6DSVX_REG_TAP_DUR_ADDR,
				 ST_LSM6DSVX_QUIET_MASK,
				 FIELD_PREP(ST_LSM6DSVX_QUIET_MASK, 0x01));

	return err < 0 ? err : 0;
}

static int
st_lsm6dsvx_event_sensor_set_enable(struct st_lsm6dsvx_sensor *sensor,
				    bool enable)
{
	int err, eint = !!enable;
	struct st_lsm6dsvx_hw *hw = sensor->hw;
	u8 int_reg = hw->int_pin == 1 ? ST_LSM6DSVX_REG_MD1_CFG_ADDR :
					ST_LSM6DSVX_REG_MD2_CFG_ADDR;

	err = st_lsm6dsvx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	switch (sensor->id) {
	case ST_LSM6DSVX_ID_WK:
		err = st_lsm6dsvx_write_with_mask(hw, int_reg,
						  ST_LSM6DSVX_INT_WU_MASK,
						  eint);
		if (err < 0)
			return err;
		break;
	case ST_LSM6DSVX_ID_FF:
		err = st_lsm6dsvx_write_with_mask(hw, int_reg,
						  ST_LSM6DSVX_INT_FF_MASK,
						  eint);
		if (err < 0)
			return err;
		break;
	case ST_LSM6DSVX_ID_SLPCHG:
		err = st_lsm6dsvx_write_with_mask(hw, int_reg,
					      ST_LSM6DSVX_INT_SLEEP_CHANGE_MASK,
					      eint);
		if (err < 0)
			return err;
		break;
	case ST_LSM6DSVX_ID_6D:
		err = st_lsm6dsvx_write_with_mask(hw, int_reg,
						  ST_LSM6DSVX_INT_6D_MASK,
						  eint);
		if (err < 0)
			return err;
		break;
	case ST_LSM6DSVX_ID_TAP:
		err = st_lsm6dsvx_write_with_mask(hw, int_reg,
						ST_LSM6DSVX_TAP_IA_MASK,
						eint);
		if (err < 0)
			return err;

		err = st_lsm6dsvx_write_with_mask(hw,
					     ST_LSM6DSVX_REG_WAKE_UP_THS_ADDR,
					     ST_LSM6DSVX_SINGLE_DOUBLE_TAP_MASK,
					     0);
		if (err < 0)
			return err;
		break;
	case ST_LSM6DSVX_ID_DTAP:
		err = st_lsm6dsvx_write_with_mask(hw, int_reg,
						ST_LSM6DSVX_INT_DOUBLE_TAP_MASK,
						eint);
		if (err < 0)
			return err;

		err = st_lsm6dsvx_write_with_mask(hw,
					     ST_LSM6DSVX_REG_WAKE_UP_THS_ADDR,
					     ST_LSM6DSVX_SINGLE_DOUBLE_TAP_MASK,
					     1);
		if (err < 0)
			return err;
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err >= 0) {
		err = st_lsm6dsvx_write_with_mask(hw,
					  ST_LSM6DSVX_REG_FUNCTIONS_ENABLE_ADDR,
					  ST_LSM6DSVX_INTERRUPTS_ENABLE_MASK,
					  eint);
		if (eint == 0)
			hw->enable_mask &= ~BIT(sensor->id);
		else
			hw->enable_mask |= BIT(sensor->id);
	}

	return err;
}

static int
st_lsm6dsvx_read_event_config(struct iio_dev *iio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsvx_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

static int
st_lsm6dsvx_write_event_config(struct iio_dev *iio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       int state)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);
	err = st_lsm6dsvx_event_sensor_set_enable(sensor, state);
	mutex_unlock(&iio_dev->mlock);

	return err;
}

static ssize_t
st_lsm6dsvx_wakeup_threshold_get(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[0]);
}

static ssize_t
st_lsm6dsvx_wakeup_threshold_set(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lsm6dsvx_set_wake_up_thershold(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[0] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static ssize_t
st_lsm6dsvx_wakeup_duration_get(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[1]);
}

static ssize_t
st_lsm6dsvx_wakeup_duration_set(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lsm6dsvx_set_wake_up_duration(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[1] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static ssize_t
st_lsm6dsvx_freefall_threshold_get(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[2]);
}

static ssize_t
st_lsm6dsvx_freefall_threshold_set(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lsm6dsvx_set_freefall_threshold(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[2] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static ssize_t
st_lsm6dsvx_6D_threshold_get(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->conf[3]);
}

static ssize_t
st_lsm6dsvx_6D_threshold_set(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lsm6dsvx_set_6D_threshold(sensor->hw, val);
	if (err < 0)
		goto out;

	sensor->conf[3] = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEVICE_ATTR(wakeup_threshold, 0644,
		       st_lsm6dsvx_wakeup_threshold_get,
		       st_lsm6dsvx_wakeup_threshold_set, 0);

static IIO_DEVICE_ATTR(wakeup_duration, 0644,
		       st_lsm6dsvx_wakeup_duration_get,
		       st_lsm6dsvx_wakeup_duration_set, 0);

static IIO_DEVICE_ATTR(freefall_threshold, 0644,
		       st_lsm6dsvx_freefall_threshold_get,
		       st_lsm6dsvx_freefall_threshold_set, 0);

static IIO_DEVICE_ATTR(sixd_threshold, 0644,
		       st_lsm6dsvx_6D_threshold_get,
		       st_lsm6dsvx_6D_threshold_set, 0);

static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsvx_get_module_id, NULL, 0);

static struct attribute *st_lsm6dsvx_wk_attributes[] = {
	&iio_dev_attr_wakeup_threshold.dev_attr.attr,
	&iio_dev_attr_wakeup_duration.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_wk_attribute_group = {
	.attrs = st_lsm6dsvx_wk_attributes,
};

static const struct iio_info st_lsm6dsvx_wk_info = {
	.attrs = &st_lsm6dsvx_wk_attribute_group,
};

static struct attribute *st_lsm6dsvx_ff_attributes[] = {
	&iio_dev_attr_freefall_threshold.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_ff_attribute_group = {
	.attrs = st_lsm6dsvx_ff_attributes,
};

static const struct iio_info st_lsm6dsvx_ff_info = {
	.attrs = &st_lsm6dsvx_ff_attribute_group,
	.read_event_config = st_lsm6dsvx_read_event_config,
	.write_event_config = st_lsm6dsvx_write_event_config,
};

static struct attribute *st_lsm6dsvx_sc_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_sc_attribute_group = {
	.attrs = st_lsm6dsvx_sc_attributes,
};

static const struct iio_info st_lsm6dsvx_sc_info = {
	.attrs = &st_lsm6dsvx_sc_attribute_group,
	.read_event_config = st_lsm6dsvx_read_event_config,
	.write_event_config = st_lsm6dsvx_write_event_config,
};

static struct attribute *st_lsm6dsvx_6D_attributes[] = {
	&iio_dev_attr_sixd_threshold.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_6D_attribute_group = {
	.attrs = st_lsm6dsvx_6D_attributes,
};

static const struct iio_info st_lsm6dsvx_6D_info = {
	.attrs = &st_lsm6dsvx_6D_attribute_group,
};

static struct attribute *st_lsm6dsvx_tap_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_tap_attribute_group = {
	.attrs = st_lsm6dsvx_tap_attributes,
};

static const struct iio_info st_lsm6dsvx_tap_info = {
	.attrs = &st_lsm6dsvx_tap_attribute_group,
	.read_event_config = st_lsm6dsvx_read_event_config,
	.write_event_config = st_lsm6dsvx_write_event_config,
};

static struct attribute *st_lsm6dsvx_dtap_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_dtap_attribute_group = {
	.attrs = st_lsm6dsvx_dtap_attributes,
};

static const struct iio_info st_lsm6dsvx_dtap_info = {
	.attrs = &st_lsm6dsvx_dtap_attribute_group,
	.read_event_config = st_lsm6dsvx_read_event_config,
	.write_event_config = st_lsm6dsvx_write_event_config,
};

static struct iio_dev *
st_lsm6dsvx_alloc_event_iiodev(struct st_lsm6dsvx_hw *hw,
			       enum st_lsm6dsvx_sensor_id id)
{
	struct st_lsm6dsvx_sensor *sensor;
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
	iio_dev->available_scan_masks = st_lsm6dsvx_event_available_scan_masks;

	switch (id) {
	case ST_LSM6DSVX_ID_WK:
		iio_dev->channels = st_lsm6dsvx_wk_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_wk_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_wk", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_wk_info;
		sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[3].hz;
		break;
	case ST_LSM6DSVX_ID_FF:
		iio_dev->channels = st_lsm6dsvx_ff_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_ff_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_ff", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_ff_info;
		sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[3].hz;
		break;
	case ST_LSM6DSVX_ID_SLPCHG:
		iio_dev->channels = st_lsm6dsvx_sc_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_sc_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_sc", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_sc_info;
		sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[3].hz;
		break;
	case ST_LSM6DSVX_ID_6D:
		iio_dev->channels = st_lsm6dsvx_6D_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_6D_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_6d", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_6D_info;
		sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[3].hz;
		break;
	case ST_LSM6DSVX_ID_TAP:
		iio_dev->channels = st_lsm6dsvx_tap_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_tap_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_tap", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_tap_info;

		/* require main sensor odr > 400 Hz */
		sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[7].hz;
		break;
	case ST_LSM6DSVX_ID_DTAP:
		iio_dev->channels = st_lsm6dsvx_dtap_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_dtap_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_dtap", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_dtap_info;

		/* require main sensor odr > 400 Hz */
		sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[7].hz;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

/**
 * st_lsm6dsvx_event_handler() - Detect embedded low power event
 *
 * @hw: ST IMU MEMS hw instance.
 *
 * return IRQ_HANDLED.
 *
 * NOTE: Uses page_lock through the st_lsm6dsvx_read_locked.
 */
int st_lsm6dsvx_event_handler(struct st_lsm6dsvx_hw *hw)
{
	struct iio_dev *iio_dev;
	u8 status;
	s64 event;
	int err;

	if (hw->enable_mask &
	    (BIT(ST_LSM6DSVX_ID_WK) | BIT(ST_LSM6DSVX_ID_FF) |
	     BIT(ST_LSM6DSVX_ID_SLPCHG) |
	     BIT(ST_LSM6DSVX_ID_6D) | BIT(ST_LSM6DSVX_ID_TAP) |
	     BIT(ST_LSM6DSVX_ID_DTAP))) {
		err = st_lsm6dsvx_read_locked(hw,
				     ST_LSM6DSVX_REG_ALL_INT_SRC_ADDR,
				     &status, sizeof(status));
		if (err < 0)
			return IRQ_HANDLED;

		/* base function sensors */
		if (status & ST_LSM6DSVX_TAP_IA_MASK) {
			if (BIT(ST_LSM6DSVX_ID_TAP)) {
				iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_TAP];
				event = IIO_UNMOD_EVENT_CODE(STM_IIO_TAP, -1,
							     IIO_EV_TYPE_THRESH,
							     IIO_EV_DIR_RISING);
				iio_push_event(iio_dev, event,
					       iio_get_time_ns(iio_dev));
			} else {
				iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_DTAP];
				event = IIO_UNMOD_EVENT_CODE(STM_IIO_TAP_TAP, -1,
							     IIO_EV_TYPE_THRESH,
							     IIO_EV_DIR_RISING);
				iio_push_event(iio_dev, event,
					       iio_get_time_ns(iio_dev));
			}
		}

		if (status & ST_LSM6DSVX_FF_IA_MASK) {
			iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_FF];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}

		if (status & ST_LSM6DSVX_WU_IA_MASK) {
			struct st_lsm6dsvx_sensor *sensor;

			iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_WK];
			sensor = iio_priv(iio_dev);
			iio_trigger_poll_chained(sensor->trig);
		}

		if (status & ST_LSM6DSVX_SLEEP_CHANGE_MASK) {
			iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_SLPCHG];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}

		if (status & ST_LSM6DSVX_D6D_IA_MASK) {
			struct st_lsm6dsvx_sensor *sensor;

			iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_6D];
			sensor = iio_priv(iio_dev);
			iio_trigger_poll_chained(sensor->trig);
		}
	}

	return IRQ_HANDLED;
}

static inline int st_lsm6dsvx_get_6D(struct st_lsm6dsvx_hw *hw, u8 *out)
{
	return st_lsm6dsvx_read_with_mask(hw, ST_LSM6DSVX_REG_D6D_SRC_ADDR,
					  ST_LSM6DSVX_D6D_EVENT_MASK, out);
}

static inline int st_lsm6dsvx_get_wk(struct st_lsm6dsvx_hw *hw, u8 *out)
{
	return st_lsm6dsvx_read_with_mask(hw,
					ST_LSM6DSVX_REG_WAKE_UP_SRC_ADDR,
					ST_LSM6DSVX_WAKE_UP_EVENT_MASK, out);
}

static irqreturn_t st_lsm6dsvx_6D_handler_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);

	st_lsm6dsvx_get_6D(sensor->hw, &sensor->scan.event);
	iio_push_to_buffers_with_timestamp(iio_dev, &sensor->scan.event,
					   iio_get_time_ns(iio_dev));

	iio_trigger_notify_done(sensor->trig);

	return IRQ_HANDLED;
}

static irqreturn_t st_lsm6dsvx_wk_handler_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);

	st_lsm6dsvx_get_wk(sensor->hw, &sensor->scan.event);
	iio_push_to_buffers_with_timestamp(iio_dev, &sensor->scan.event,
					   iio_get_time_ns(iio_dev));

	iio_trigger_notify_done(sensor->trig);

	return IRQ_HANDLED;
}

int st_lsm6dsvx_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio_dev = iio_trigger_get_drvdata(trig);
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);

	dev_info(sensor->hw->dev, "trigger set %d\n", state);

	return 0;
}

static const struct iio_trigger_ops st_lsm6dsvx_trigger_ops = {
	.set_trigger_state = &st_lsm6dsvx_trig_set_state,
};

static int st_lsm6dsvx_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_lsm6dsvx_event_sensor_set_enable(iio_priv(iio_dev),
						   true);
}

static int st_lsm6dsvx_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_lsm6dsvx_event_sensor_set_enable(iio_priv(iio_dev),
						   false);
}

static const struct iio_buffer_setup_ops st_lsm6dsvx_buffer_ops = {
	.preenable = st_lsm6dsvx_buffer_preenable,
#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
#endif /* LINUX_VERSION_CODE */
	.postdisable = st_lsm6dsvx_buffer_postdisable,
};

static int
st_lsm6dsvx_config_default_events(struct st_lsm6dsvx_hw *hw)
{
	int err;

	/* set default wake-up thershold to 93750 ug */
	err = st_lsm6dsvx_set_wake_up_thershold(hw, 93750);
	if (err < 0)
		return err;

	/* set default wake-up duration to 0 */
	err = st_lsm6dsvx_set_wake_up_duration(hw, 0);
	if (err < 0)
		return err;

	/* set default FF threshold to 312 mg */
	err = st_lsm6dsvx_set_freefall_threshold(hw, 312);
	if (err < 0)
		return err;

	/* set default 6D threshold to 60 degrees */
	err = st_lsm6dsvx_set_6D_threshold(hw, 60);
	if (err < 0)
		return err;

	return st_lsm6dsvx_init_tap(hw);
}

int st_lsm6dsvx_probe_event(struct st_lsm6dsvx_hw *hw)
{
	struct st_lsm6dsvx_sensor *sensor;
	struct iio_dev *iio_dev;
	irqreturn_t (*pthread[ST_LSM6DSVX_ID_MAX - ST_LSM6DSVX_ID_WK])(int irq, void *p) = {
		[0] = st_lsm6dsvx_wk_handler_thread,
		[1] = st_lsm6dsvx_6D_handler_thread,
		/* add here all other trigger handler funcions */
	};
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsvx_event_sensor_list);
	     i++) {
		enum st_lsm6dsvx_sensor_id id =
				       st_lsm6dsvx_event_sensor_list[i];

		hw->iio_devs[id] = st_lsm6dsvx_alloc_event_iiodev(hw,
								  id);
		if (!hw->iio_devs[id])
			return -ENOMEM;
	}

	/* configure trigger sensors */
	for (i = 0;
	     i < ARRAY_SIZE(st_lsm6dsvx_event_trigger_sensor_list);
	     i++) {
		enum st_lsm6dsvx_sensor_id id =
				       st_lsm6dsvx_event_trigger_sensor_list[i];
		iio_dev = hw->iio_devs[id];
		sensor = iio_priv(iio_dev);

		err = devm_iio_triggered_buffer_setup(hw->dev, iio_dev,
				NULL, pthread[id - ST_LSM6DSVX_ID_WK],
				&st_lsm6dsvx_buffer_ops);
		if (err < 0)
			return err;

		sensor->trig = devm_iio_trigger_alloc(hw->dev,
						      "%s-trigger",
						      iio_dev->name);
		if (!sensor->trig) {
			dev_err(hw->dev,
				"failed to allocate iio trigger.\n");

			return -ENOMEM;
		}

		iio_trigger_set_drvdata(sensor->trig, iio_dev);
		sensor->trig->ops = &st_lsm6dsvx_trigger_ops;
		sensor->trig->dev.parent = hw->dev;

		err = devm_iio_trigger_register(hw->dev, sensor->trig);
		if (err < 0) {
			dev_err(hw->dev,
				"failed to register iio trigger.\n");

			return err;
		}

		iio_dev->trig = iio_trigger_get(sensor->trig);
	}

	return st_lsm6dsvx_config_default_events(hw);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2du12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <asm/unaligned.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lis2du12.h"

static const struct st_lis2du12_std_entry {
	u16 odr;
	u8 val;
} st_lis2du12_std_table[] = {
	{    6,  3 },
	{   12,  3 },
	{   25,  3 },
	{   50,  3 },
	{  100,  3 },
	{  200,  3 },
	{  400,  3 },
	{  800,  3 },
};

static const struct st_lis2du12_odr {
	u16 hz;
	u32 uhz;
	u8 val;
} st_lis2du12_odr_table[] = {
	{    0,      0, 0x00 },
	{    1, 500000, 0x01 },
	{    3,      0, 0x02 },
	{    6,      0, 0x04 },
	{   12, 500000, 0x05 },
	{   25,      0, 0x06 },
	{   50,      0, 0x07 },
	{  100,      0, 0x08 },
	{  200,      0, 0x09 },
	{  400,      0, 0x0a },
	{  800,      0, 0x0b },
};

static const struct st_lis2du12_fs {
	u32 gain;
	u8 val;
} st_lis2du12_fs_table[] = {
	{ IIO_G_TO_M_S_2(976),  0x00 },
	{ IIO_G_TO_M_S_2(1952), 0x01 },
	{ IIO_G_TO_M_S_2(3904), 0x02 },
	{ IIO_G_TO_M_S_2(7808), 0x03 },
};

static const struct st_lis2du12_selftest_req {
	char *mode;
	u8 val;
} st_lis2du12_selftest_table[] = {
	{ "disabled",      0x0 },
	{ "positive-sign", 0x6 },
	{ "negative-sign", 0x1 },
};

static const struct iio_event_spec st_lis2du12_fifo_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_event_spec st_lis2du12_rthr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

static const struct iio_chan_spec st_lis2du12_acc_channels[] = {
	ST_LIS2DU12_ACC_CHAN(ST_LIS2DU12_OUT_X_L_ADDR, IIO_MOD_X, 0),
	ST_LIS2DU12_ACC_CHAN(ST_LIS2DU12_OUT_Y_L_ADDR, IIO_MOD_Y, 1),
	ST_LIS2DU12_ACC_CHAN(ST_LIS2DU12_OUT_Z_L_ADDR, IIO_MOD_Z, 2),
	ST_LIS2DU12_EVENT_CHANNEL(IIO_ACCEL,
				  &st_lis2du12_fifo_flush_event),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec st_lis2du12_temp_channels[] = {
	ST_LIS2DU12_TEMP_CHAN(ST_LIS2DU12_TEMP_L_ADDR, IIO_NO_MOD),
	ST_LIS2DU12_EVENT_CHANNEL(IIO_TEMP,
				  &st_lis2du12_fifo_flush_event),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec st_lis2du12_tap_tap_channels[] = {
	ST_LIS2DU12_EVENT_CHANNEL(STM_IIO_TAP_TAP,
				  &st_lis2du12_rthr_event),
};

static const struct iio_chan_spec st_lis2du12_tap_channels[] = {
	ST_LIS2DU12_EVENT_CHANNEL(STM_IIO_TAP,
				  &st_lis2du12_rthr_event),
};

static const struct iio_chan_spec st_lis2du12_wu_channels[] = {
	ST_LIS2DU12_EVENT_CHANNEL(STM_IIO_GESTURE,
				  &st_lis2du12_rthr_event),
};

static const struct iio_chan_spec st_lis2du12_ff_channels[] = {
	ST_LIS2DU12_EVENT_CHANNEL(STM_IIO_GESTURE,
				  &st_lis2du12_rthr_event),
};

static const struct iio_chan_spec st_lis2du12_6d_channels[] = {
	ST_LIS2DU12_EVENT_CHANNEL(STM_IIO_GESTURE,
				  &st_lis2du12_rthr_event),
};

static const struct iio_chan_spec st_lis2du12_act_channels[] = {
	ST_LIS2DU12_EVENT_CHANNEL(STM_IIO_GESTURE,
				  &st_lis2du12_rthr_event),
};

static int st_lis2du12_set_fs(struct st_lis2du12_sensor *sensor,
			      u16 gain)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_lis2du12_fs_table); i++)
		if (st_lis2du12_fs_table[i].gain == gain)
			break;

	if (i == ARRAY_SIZE(st_lis2du12_fs_table))
		return -EINVAL;

	err = regmap_update_bits(sensor->hw->regmap,
				 ST_LIS2DU12_CTRL5_ADDR,
				 ST_LIS2DU12_FS_MASK,
				 FIELD_PREP(ST_LIS2DU12_FS_MASK,
					    st_lis2du12_fs_table[i].val));
	if (err < 0)
		return err;

	sensor->gain = gain;

	return 0;
}

static inline int st_lis2du12_get_odr_idx(u16 odr, u32 uodr)
{
	int uhz = ST_LIS2DU12_ODR_EXPAND(odr, uodr);
	int ref_uhz;
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lis2du12_odr_table); i++) {
		ref_uhz = ST_LIS2DU12_ODR_EXPAND(st_lis2du12_odr_table[i].hz,
						 st_lis2du12_odr_table[i].uhz);
		if (ref_uhz >= uhz)
			break;
	}

	if (i == ARRAY_SIZE(st_lis2du12_odr_table))
		return -EINVAL;

	return i;
}

static int st_lis2du12_check_odr_dependency(struct st_lis2du12_hw *hw,
					    u16 odr, u32 uodr,
					    enum st_lis2du12_sensor_id ref_id)
{
	struct st_lis2du12_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	int ref_uhz = ST_LIS2DU12_ODR_EXPAND(ref->odr, ref->uodr);
	int uhz = ST_LIS2DU12_ODR_EXPAND(odr, uodr);
	int ret;

	if (ref_uhz > 0) {
		if (hw->enable_mask & BIT(ref_id))
			ret = max_t(int, ref_uhz, uhz);
		else
			ret = uhz;
	} else {
		ret = (hw->enable_mask & BIT(ref_id)) ? ref_uhz : 0;
	}

	return ret;
}

static int st_lis2du12_set_odr(struct st_lis2du12_sensor *sensor,
			       u16 req_odr, u32 req_uodr)
{
	int uhz = ST_LIS2DU12_ODR_EXPAND(req_odr, req_uodr);
	struct st_lis2du12_hw *hw = sensor->hw;
	int err, odr, ret;
	u8 i;

	for (i = 0; i < ST_LIS2DU12_ID_MAX; i++) {
		if (i == sensor->id)
			continue;

		odr = st_lis2du12_check_odr_dependency(hw, req_odr,
						       req_uodr, i);
		/* check if device is already configured */
		if (odr != uhz)
			return 0;
	}

	ret = st_lis2du12_get_odr_idx(req_odr, req_uodr);
	if (ret < 0)
		return ret;

	err = regmap_update_bits(sensor->hw->regmap,
				 ST_LIS2DU12_CTRL5_ADDR,
				 ST_LIS2DU12_ODR_MASK,
				 FIELD_PREP(ST_LIS2DU12_ODR_MASK,
					    st_lis2du12_odr_table[ret].val));

	return err < 0 ? err : 0;
}

static int st_lis2du12_check_whoami(struct st_lis2du12_hw *hw)
{
	int err, data;

	err = regmap_read(hw->regmap, ST_LIS2DU12_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");

		return err;
	}

	if (data != ST_LIS2DU12_WHOAMI_VAL) {
		dev_err(hw->dev, "wrong whoami %02x vs %02x\n",
			data, ST_LIS2DU12_WHOAMI_VAL);
		return -ENODEV;
	}

	return 0;
}

static int st_lis2du12_of_get_int_pin(struct st_lis2du12_hw *hw,
				       int *int_pin)
{
	struct device_node *np = hw->dev->of_node;

	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "st,int-pin", int_pin);
}

static int st_lis2du12_get_int_pin(struct st_lis2du12_hw *hw)
{
	int err = 0, drdy_pin;

	if (st_lis2du12_of_get_int_pin(hw, &drdy_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		drdy_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (drdy_pin) {
	case 1:
		hw->drdy_reg = ST_LIS2DU12_CTRL2_ADDR;
		hw->md_reg = ST_LIS2DU12_MD1_CFG_ADDR;
		break;
	case 2:
		hw->drdy_reg = ST_LIS2DU12_CTRL3_ADDR;
		hw->md_reg = ST_LIS2DU12_MD2_CFG_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	return err;
}

static int st_lis2du12_init_hw(struct st_lis2du12_hw *hw)
{
	int err;

	/* read interrupt pin configuration */
	err = st_lis2du12_get_int_pin(hw);
	if (err < 0)
		return err;

	/* soft reset procedure */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_CTRL1_ADDR,
				 ST_LIS2DU12_SW_RESET_MASK,
				 FIELD_PREP(ST_LIS2DU12_SW_RESET_MASK, 1));
	if (err < 0)
		return err;

	/* wait 50 μs */
	usleep_range(55, 60);

	/* boot procedure */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_CTRL4_ADDR,
				 ST_LIS2DU12_BOOT_MASK,
				 FIELD_PREP(ST_LIS2DU12_BOOT_MASK, 1));
	if (err < 0)
		return err;

	usleep_range(20000, 20100);

	/* enable BDU */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_CTRL4_ADDR,
				 ST_LIS2DU12_BDU_MASK,
				 FIELD_PREP(ST_LIS2DU12_BDU_MASK, 1));
	if (err < 0)
		return err;

	/*
	 * register address is automatically incremented during a
	 * multiple-byte access with a serial interface
	 */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_CTRL1_ADDR,
				 ST_LIS2DU12_IF_ADD_INC_MASK,
				 FIELD_PREP(ST_LIS2DU12_IF_ADD_INC_MASK, 1));
	if (err < 0)
		return err;


	/* set interrupt latched */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_INTERRUPT_CFG_ADDR,
				 ST_LIS2DU12_INT_SHORT_EN_MASK,
				 FIELD_PREP(ST_LIS2DU12_INT_SHORT_EN_MASK, 0x1));
	if (err < 0)
		return err;

	return regmap_update_bits(hw->regmap,
				  ST_LIS2DU12_INTERRUPT_CFG_ADDR,
				  ST_LIS2DU12_LIR_MASK,
				  FIELD_PREP(ST_LIS2DU12_LIR_MASK, 1));
}

static int st_lis2du12_embedded_config(struct st_lis2du12_hw *hw)
{
	struct st_lis2du12_sensor *sensor;
	int err;

	/* configure default free fall event threshold */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_FREE_FALL_ADDR,
				 ST_LIS2DU12_FF_THS_MASK,
				 FIELD_PREP(ST_LIS2DU12_FF_THS_MASK, 1));
	if (err < 0)
		return err;

	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_FF);
	sensor->ff_ths = 1;

	/* configure default free fall event duration */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_FREE_FALL_ADDR,
				 ST_LIS2DU12_FF_DUR_MASK,
				 FIELD_PREP(ST_LIS2DU12_FF_DUR_MASK, 1));
	if (err < 0)
		return err;

	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_FF);
	sensor->ff_dur = 1;

	/* enable tap event on all axes */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_TAP_THS_Z_ADDR,
				 ST_LIS2DU12_TAP_EN_MASK,
				 FIELD_PREP(ST_LIS2DU12_TAP_EN_MASK, 0x7));
	if (err < 0)
		return err;

	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_TAP);
	sensor->tap_en = 0x7;
	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_TAP_TAP);
	sensor->tap_en = 0x7;

	/* enable wake-up event on all axes */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_CTRL1_ADDR,
				 ST_LIS2DU12_WU_EN_MASK,
				 FIELD_PREP(ST_LIS2DU12_WU_EN_MASK, 0x7));
	if (err < 0)
		return err;

	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_WU);
	sensor->wk_en = 0x7;

	/* double tap event detection configuration */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_INT_DUR_ADDR,
				 ST_LIS2DU12_SHOCK_MASK,
				 FIELD_PREP(ST_LIS2DU12_SHOCK_MASK, 0x3));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_INT_DUR_ADDR,
				 ST_LIS2DU12_QUIET_MASK,
				 FIELD_PREP(ST_LIS2DU12_QUIET_MASK, 0x3));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_INT_DUR_ADDR,
				 ST_LIS2DU12_LATENCY_MASK,
				 FIELD_PREP(ST_LIS2DU12_LATENCY_MASK, 0x7));
	if (err < 0)
		return err;

	/* configure default threshold for tap event recognition */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_TAP_THS_X_ADDR,
				 ST_LIS2DU12_TAP_THS_X_MASK,
				 FIELD_PREP(ST_LIS2DU12_TAP_THS_X_MASK, 0x9));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_TAP_THS_Y_ADDR,
				 ST_LIS2DU12_TAP_THS_Y_MASK,
				 FIELD_PREP(ST_LIS2DU12_TAP_THS_Y_MASK, 0x9));
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_TAP_THS_Z_ADDR,
				 ST_LIS2DU12_TAP_THS_Z_MASK,
				 FIELD_PREP(ST_LIS2DU12_TAP_THS_Z_MASK, 0x9));

	if (err < 0)
		return err;

	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_TAP_TAP);
	sensor->shock = 0x3;
	sensor->quiet = 0x3;
	sensor->latency = 0x7;
	sensor->tap_ths_x = 0x9;
	sensor->tap_ths_y = 0x9;
	sensor->tap_ths_z = 0x9;

	/* setup wake-up threshold */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_WAKE_UP_THS_ADDR,
				 ST_LIS2DU12_WK_THS_MASK,
				 FIELD_PREP(ST_LIS2DU12_WK_THS_MASK, 0x2));
	if (err < 0)
		return err;

	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_WU);
	sensor->wh_ths = 0x2;

	/* set 6D threshold to 60 degree */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_TAP_THS_X_ADDR,
				 ST_LIS2DU12_D6D_THS_MASK,
				 FIELD_PREP(ST_LIS2DU12_D6D_THS_MASK, 0x2));
	if (err < 0)
		return err;

	sensor = st_lis2du12_get_sensor_from_id(hw, ST_LIS2DU12_ID_6D);
	sensor->d6d_ths = 0x2;

	return err;
}

static ssize_t
st_lis2du12_sysfs_sampling_frequency_avl(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i, len = 0;

	for (i = 1; i < ARRAY_SIZE(st_lis2du12_odr_table); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_lis2du12_odr_table[i].hz,
				 st_lis2du12_odr_table[i].uhz);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_lis2du12_sysfs_scale_avail(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st_lis2du12_fs_table); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
				 st_lis2du12_fs_table[i].gain);
	buf[len - 1] = '\n';

	return len;
}

int st_lis2du12_sensor_set_enable(struct st_lis2du12_sensor *sensor,
				  bool enable)
{
	struct st_lis2du12_hw *hw = sensor->hw;
	int err;

	err = st_lis2du12_set_odr(sensor, enable ? sensor->odr : 0,
				  enable ? sensor->uodr : 0);
	if (err < 0)
		return err;

	if (enable)
		hw->enable_mask |= BIT(sensor->id);
	else
		hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

static int st_lis2du12_read_oneshot(struct st_lis2du12_sensor *sensor,
				    u8 addr, int *val)
{
	struct st_lis2du12_hw *hw = sensor->hw;
	int err, delay;
	u8 data[2];

	err = st_lis2du12_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	/* sample to discard, 3 * odr us */
	delay = 3000000 / sensor->odr;
	usleep_range(delay, delay + 1);

	err = st_lis2du12_read_locked(hw, addr, &data, sizeof(data));
	if (err < 0)
		return err;

	st_lis2du12_sensor_set_enable(sensor, false);

	*val = (s16)get_unaligned_le16(data);

	return IIO_VAL_INT;
}

static int st_lis2du12_read_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *ch,
				int *val, int *val2, long mask)
{
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&iio_dev->mlock);
		if (iio_buffer_enabled(iio_dev)) {
			ret = -EBUSY;
			mutex_unlock(&iio_dev->mlock);
			break;
		}
		ret = st_lis2du12_read_oneshot(sensor, ch->address,
					       val);
		mutex_unlock(&iio_dev->mlock);
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (ch->type) {
		case IIO_TEMP:
			*val = sensor->offset;
			ret = IIO_VAL_INT;
			break;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sensor->gain;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = (int)sensor->odr;
		*val2 = (int)sensor->uodr;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_lis2du12_write_raw(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_lis2du12_set_fs(sensor, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		int ret;

		ret = st_lis2du12_get_odr_idx(val, val2);
		if (ret < 0)
			break;

		sensor->hw->std_level = st_lis2du12_std_table[ret].val;
		sensor->odr = val;
		sensor->uodr = val2;
		err = 0;
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	mutex_unlock(&iio_dev->mlock);

	return err;
}

static int st_lis2du12_read_event_config(struct iio_dev *iio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir)
{
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2du12_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

static int st_lis2du12_write_event_config(struct iio_dev *iio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  int enable)
{
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2du12_hw *hw = sensor->hw;
	bool enable_int;
	u8 md_mask;
	int err;

	switch (sensor->id) {
	case ST_LIS2DU12_ID_WU:
		md_mask = ST_LIS2DU12_INT_WU_MASK;
		break;
	case ST_LIS2DU12_ID_TAP:
		md_mask = ST_LIS2DU12_INT_SINGLE_TAP_MASK;
		err = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_WAKE_UP_THS_ADDR,
					 ST_LIS2DU12_SINGLE_DOUBLE_TAP_MASK,
					 FIELD_PREP(ST_LIS2DU12_SINGLE_DOUBLE_TAP_MASK, 0));
		if (err < 0)
			return err;
		break;
	case ST_LIS2DU12_ID_TAP_TAP:
		md_mask = ST_LIS2DU12_INT_DOUBLE_TAP_MASK;
		err = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_WAKE_UP_THS_ADDR,
					 ST_LIS2DU12_SINGLE_DOUBLE_TAP_MASK,
					 FIELD_PREP(ST_LIS2DU12_SINGLE_DOUBLE_TAP_MASK, enable));
		if (err < 0)
			return err;
		break;
	case ST_LIS2DU12_ID_FF:
		md_mask = ST_LIS2DU12_INT_FF_MASK;
		break;
	case ST_LIS2DU12_ID_6D:
		md_mask = ST_LIS2DU12_INT_6D_MASK;
		break;
	case ST_LIS2DU12_ID_ACT:
		md_mask = ST_LIS2DU12_INT_SLEEP_CHANGE_MASK;
		err = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_WAKE_UP_THS_ADDR,
					 ST_LIS2DU12_SLEEP_ON_MASK,
					 FIELD_PREP(ST_LIS2DU12_SLEEP_ON_MASK, enable));
		if (err < 0)
			return err;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_update_bits(hw->regmap, hw->md_reg, md_mask,
				 ST_LIS2DU12_SHIFT_VAL(enable, md_mask));
	if (err < 0)
		return err;

	err = st_lis2du12_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	/* enable global interrupt pin */
	enable_int = st_lis2du12_interrupts_enabled(hw);
	if ((enable_int && enable == 1) || (!enable_int && enable == 0))
		err = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_INTERRUPT_CFG_ADDR,
					 ST_LIS2DU12_INTERRUPTS_ENABLE_MASK,
					 FIELD_PREP(ST_LIS2DU12_INTERRUPTS_ENABLE_MASK, enable));

	return err;
}

static ssize_t st_lis2du12_get_hwfifo_watermark(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->watermark);
}

static ssize_t
st_lis2du12_get_max_hwfifo_watermark(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return sprintf(buf, "%d\n", ST_LIS2DU12_MAX_WATERMARK);
}

static ssize_t st_lis2du12_get_selftest_avail(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return sprintf(buf, "%s, %s\n", st_lis2du12_selftest_table[1].mode,
		       st_lis2du12_selftest_table[2].mode);
}

static ssize_t st_lis2du12_get_selftest_status(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2du12_hw *hw = sensor->hw;
	char *ret;

	switch (hw->st_status) {
	case ST_LIS2DU12_ST_PASS:
		ret = "pass";
		break;
	case ST_LIS2DU12_ST_FAIL:
		ret = "fail";
		break;
	default:
	case ST_LIS2DU12_ST_RESET:
		ret = "na";
		break;
	}

	return sprintf(buf, "%s\n", ret);
}

static ssize_t st_lis2du12_enable_selftest(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct iio_chan_spec const *ch = iio_dev->channels;
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2du12_hw *hw = sensor->hw;
	u8 data[ST_LIS2DU12_ACC_DATA_SIZE], val;
	s16 out1[3], out2[3];
	int i, err, gain;

	mutex_lock(&iio_dev->mlock);

	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;
		goto unlock;
	}

	for (i = 0; i < ARRAY_SIZE(st_lis2du12_selftest_table); i++)
		if (!strncmp(buf, st_lis2du12_selftest_table[i].mode,
			     size - 2))
			break;

	if (i == ARRAY_SIZE(st_lis2du12_selftest_table)) {
		err = -EINVAL;
		goto unlock;
	}

	hw->st_status = ST_LIS2DU12_ST_RESET;
	val = st_lis2du12_selftest_table[i].val;
	gain = sensor->gain;

	/* set self test mode */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_ST_SIGN_ADDR,
				 ST_LIS2DU12_STSIGN_MASK,
				 FIELD_PREP(ST_LIS2DU12_STSIGN_MASK, val));
	if (err < 0)
		goto unlock;

	err = st_lis2du12_set_odr(sensor, 0, 0);
	if (err < 0)
		goto unlock;

	/* start test mode */
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_CTRL3_ADDR,
				 ST_LIS2DU12_ST_MASK,
				 FIELD_PREP(ST_LIS2DU12_ST_MASK, 0x02));
	if (err < 0)
		goto unlock;

	/* fs = 8g, odr = 200Hz */
	err = st_lis2du12_set_fs(sensor, IIO_G_TO_M_S_2(3904));
	if (err < 0)
		goto unlock;

	err = st_lis2du12_set_odr(sensor, 200, 0);
	if (err < 0)
		goto unlock;

	err = st_lis2du12_sensor_set_enable(sensor, true);
	if (err < 0)
		goto unlock;

	msleep(30);

	err = regmap_bulk_read(hw->regmap, ch[0].address,
			       data, sizeof(data));
	if (err < 0)
		goto unlock;

	err = st_lis2du12_set_odr(sensor, 0, 0);
	if (err < 0)
		goto unlock;

	out1[0] = ((s16)get_unaligned_le16(&data[0]) >> 4);
	out1[1] = ((s16)get_unaligned_le16(&data[2]) >> 4);
	out1[2] = ((s16)get_unaligned_le16(&data[4]) >> 4);

	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_CTRL3_ADDR,
				 ST_LIS2DU12_ST_MASK,
				 FIELD_PREP(ST_LIS2DU12_ST_MASK, 0x01));
	if (err < 0)
		goto unlock;

	/* fs = 8g, odr = 200Hz */
	err = st_lis2du12_set_odr(sensor, 200, 0);
	if (err < 0)
		goto unlock;

	msleep(30);

	err = regmap_bulk_read(hw->regmap, ch[0].address,
			       data, sizeof(data));
	if (err < 0)
		goto unlock;

	err = st_lis2du12_set_odr(sensor, 0, 0);
	if (err < 0)
		goto unlock;

	out2[0] = ((s16)get_unaligned_le16(&data[0]) >> 4);
	out2[1] = ((s16)get_unaligned_le16(&data[2]) >> 4);
	out2[2] = ((s16)get_unaligned_le16(&data[4]) >> 4);

	/* disable self test */
	err = regmap_update_bits(hw->regmap, ST_LIS2DU12_CTRL3_ADDR,
				 ST_LIS2DU12_ST_MASK,
				 FIELD_PREP(ST_LIS2DU12_ST_MASK, 0));
	if (err < 0)
		goto unlock;

	if ((abs(out2[0] - out1[0]) >= 180) ||
	    (abs(out2[0] - out1[0]) < 12)) {
		hw->st_status = ST_LIS2DU12_ST_FAIL;
		goto selftest_restore;
	}

	if ((abs(out2[1] - out1[1]) >= 180) ||
	    (abs(out2[1] - out1[1]) < 12)) {
		hw->st_status = ST_LIS2DU12_ST_FAIL;
		goto selftest_restore;
	}

	if ((abs(out2[2] - out1[2]) >= 308) ||
	    (abs(out2[2] - out1[2]) < 51)) {
		hw->st_status = ST_LIS2DU12_ST_FAIL;
		goto selftest_restore;
	}

	hw->st_status = ST_LIS2DU12_ST_PASS;

selftest_restore:
	err = st_lis2du12_set_fs(sensor, gain);
	if (err < 0)
		goto unlock;

	err = st_lis2du12_sensor_set_enable(sensor, false);

unlock:
	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : size;
}

static ssize_t st_lis2du12_get_4d(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", !!sensor->hw->fourd_enabled);
}

static ssize_t st_lis2du12_set_4d(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	mutex_lock(&iio_dev->mlock);
	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;

		goto unlock;
	}

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto unlock;

	val = val >= 1 ? 1 : 0;

	/* set Enable */
	err = regmap_update_bits(sensor->hw->regmap, ST_LIS2DU12_TAP_THS_X_ADDR,
				 ST_LIS2DU12_D4D_EN_MASK,
				 FIELD_PREP(ST_LIS2DU12_D4D_EN_MASK, val));
	if (err < 0)
		goto unlock;

	sensor->hw->fourd_enabled = val;

unlock:
	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : size;
}

static int st_lis2du12_embebbed_config(struct st_lis2du12_sensor *sensor,
				       enum st_lis2du12_attr_id id,
				       u8 *reg, u8 *mask, u8 **attr)
{
	int err = 0;

	switch (id) {
	case ST_LIS2DU12_WK_THS_ATTR_ID:
		*attr = &sensor->wh_ths;
		*reg = ST_LIS2DU12_WAKE_UP_THS_ADDR;
		*mask = ST_LIS2DU12_WK_THS_MASK;
		break;
	case ST_LIS2DU12_WK_DUR_ATTR_ID:
		*attr = &sensor->wh_dur;
		*reg = ST_LIS2DU12_WAKE_UP_DUR_ADDR;
		*mask = ST_LIS2DU12_WAKE_DUR_MASK;
		break;
	case ST_LIS2DU12_FF_THS_ATTR_ID:
		*attr = &sensor->ff_ths;
		*reg = ST_LIS2DU12_FREE_FALL_ADDR;
		*mask = ST_LIS2DU12_FF_THS_MASK;
		break;
	case ST_LIS2DU12_FF_DUR_ATTR_ID:
		*attr = &sensor->ff_dur;
		*reg = ST_LIS2DU12_FREE_FALL_ADDR;
		*mask = ST_LIS2DU12_FF_DUR_MASK;
		break;
	case ST_LIS2DU12_6D_THS_ATTR_ID:
		*attr = &sensor->d6d_ths;
		*reg = ST_LIS2DU12_TAP_THS_X_ADDR;
		*mask = ST_LIS2DU12_D6D_THS_MASK;
		break;
	case ST_LIS2DU12_LATENCY_ATTR_ID:
		*attr = &sensor->latency;
		*reg = ST_LIS2DU12_INT_DUR_ADDR;
		*mask = ST_LIS2DU12_LATENCY_MASK;
		break;
	case ST_LIS2DU12_QUIET_ATTR_ID:
		*attr = &sensor->quiet;
		*reg = ST_LIS2DU12_INT_DUR_ADDR;
		*mask = ST_LIS2DU12_QUIET_MASK;
		break;
	case ST_LIS2DU12_SHOCK_ATTR_ID:
		*attr = &sensor->shock;
		*reg = ST_LIS2DU12_INT_DUR_ADDR;
		*mask = ST_LIS2DU12_SHOCK_MASK;
		break;
	case ST_LIS2DU12_TAP_PRIORITY_ATTR_ID:
		*attr = &sensor->tap_priority;
		*reg = ST_LIS2DU12_TAP_THS_Y_ADDR;
		*mask = ST_LIS2DU12_TAP_PRIORITY_MASK;
		break;
	case ST_LIS2DU12_SLEEP_DUR_ATTR_ID:
		*attr = &sensor->sleep_dur;
		*reg = ST_LIS2DU12_WAKE_UP_DUR_ADDR;
		*mask = ST_LIS2DU12_SLEEP_DUR_MASK;
		break;
	case ST_LIS2DU12_TAP_THRESHOLD_X_ATTR_ID:
		*attr = &sensor->tap_ths_x;
		*reg = ST_LIS2DU12_TAP_THS_X_ADDR;
		*mask = ST_LIS2DU12_TAP_THS_X_MASK;
		break;
	case ST_LIS2DU12_TAP_THRESHOLD_Y_ATTR_ID:
		*attr = &sensor->tap_ths_y;
		*reg = ST_LIS2DU12_TAP_THS_Y_ADDR;
		*mask = ST_LIS2DU12_TAP_THS_Y_MASK;
		break;
	case ST_LIS2DU12_TAP_THRESHOLD_Z_ATTR_ID:
		*attr = &sensor->tap_ths_z;
		*reg = ST_LIS2DU12_TAP_THS_Z_ADDR;
		*mask = ST_LIS2DU12_TAP_THS_Z_MASK;
		break;
	case ST_LIS2DU12_TAP_ENABLE_ATTR_ID:
		*attr = &sensor->tap_en;
		*reg = ST_LIS2DU12_TAP_THS_Z_ADDR;
		*mask = ST_LIS2DU12_TAP_EN_MASK;
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

ssize_t st_lis2du12_embebbed_threshold_get(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct st_lis2du12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	enum st_lis2du12_attr_id id;
	u8 reg, mask, *val;
	int err;

	id = (enum st_lis2du12_attr_id)this_attr->address;
	err = st_lis2du12_embebbed_config(sensor, id, &reg, &mask, &val);
	if (err < 0)
		return err;

	return sprintf(buf, "%d\n", *val);
}

ssize_t st_lis2du12_embebbed_threshold_set(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	enum st_lis2du12_attr_id id;
	u8 reg, mask, *val;
	int err, data;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &data);
	if (err < 0)
		goto out;

	id = (enum st_lis2du12_attr_id)this_attr->address;
	err = st_lis2du12_embebbed_config(sensor, id, &reg, &mask, &val);
	if (err < 0)
		goto out;

	err = regmap_update_bits(sensor->hw->regmap, reg, mask,
				 ST_LIS2DU12_SHIFT_VAL(data, mask));
	if (err < 0)
		goto out;

	if (id == ST_LIS2DU12_FF_DUR_ATTR_ID) {
		/* free fall duration split on two registers */
		err = regmap_update_bits(sensor->hw->regmap,
					 ST_LIS2DU12_WAKE_UP_DUR_ADDR,
					 ST_LIS2DU12_FF_DUR5_MASK,
					 ST_LIS2DU12_SHIFT_VAL((data >> 5 & 0x01),
						     ST_LIS2DU12_FF_DUR5_MASK));
		if (err < 0)
			goto out;
	}

	*val = (u8)data;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lis2du12_sysfs_sampling_frequency_avl);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_lis2du12_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644,
		       st_lis2du12_get_hwfifo_watermark,
		       st_lis2du12_set_hwfifo_watermark, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_lis2du12_get_max_hwfifo_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, 0200, NULL,
		       st_lis2du12_flush_fifo, 0);
static IIO_DEVICE_ATTR(selftest_available, 0444,
		       st_lis2du12_get_selftest_avail, NULL, 0);
static IIO_DEVICE_ATTR(selftest, 0644, st_lis2du12_get_selftest_status,
		       st_lis2du12_enable_selftest, 0);
static IIO_DEVICE_ATTR(enable_4d, 0644, st_lis2du12_get_4d,
		       st_lis2du12_set_4d, 0);
static IIO_DEVICE_ATTR(wakeup_threshold, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_WK_THS_ATTR_ID);
static IIO_DEVICE_ATTR(wakeup_duration, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_WK_DUR_ATTR_ID);
static IIO_DEVICE_ATTR(freefall_threshold, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_FF_THS_ATTR_ID);
static IIO_DEVICE_ATTR(freefall_duration, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_FF_DUR_ATTR_ID);
static IIO_DEVICE_ATTR(sixd_threshold, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_6D_THS_ATTR_ID);
static IIO_DEVICE_ATTR(latency_threshold, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_LATENCY_ATTR_ID);
static IIO_DEVICE_ATTR(quiet_threshold, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_QUIET_ATTR_ID);
static IIO_DEVICE_ATTR(shock_threshold, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_SHOCK_ATTR_ID);
static IIO_DEVICE_ATTR(tap_priority, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_TAP_PRIORITY_ATTR_ID);
static IIO_DEVICE_ATTR(tap_thr_x, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_TAP_THRESHOLD_X_ATTR_ID);
static IIO_DEVICE_ATTR(tap_thr_y, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_TAP_THRESHOLD_Y_ATTR_ID);
static IIO_DEVICE_ATTR(tap_thr_z, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_TAP_THRESHOLD_Z_ATTR_ID);
static IIO_DEVICE_ATTR(tap_enable, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_TAP_ENABLE_ATTR_ID);
static IIO_DEVICE_ATTR(sleep_dur, 0644,
		       st_lis2du12_embebbed_threshold_get,
		       st_lis2du12_embebbed_threshold_set,
		       ST_LIS2DU12_SLEEP_DUR_ATTR_ID);

static struct attribute *st_lis2du12_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_acc_attribute_group = {
	.attrs = st_lis2du12_acc_attributes,
};

static const struct iio_info st_lis2du12_acc_info = {
	.attrs = &st_lis2du12_acc_attribute_group,
	.read_raw = st_lis2du12_read_raw,
	.write_raw = st_lis2du12_write_raw,
};

static struct attribute *st_lis2du12_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_temp_attribute_group = {
	.attrs = st_lis2du12_temp_attributes,
};

static const struct iio_info st_lis2du12_temp_info = {
	.attrs = &st_lis2du12_temp_attribute_group,
	.read_raw = st_lis2du12_read_raw,
	.write_raw = st_lis2du12_write_raw,
};

static struct attribute *st_lis2du12_wu_attributes[] = {
	&iio_dev_attr_wakeup_threshold.dev_attr.attr,
	&iio_dev_attr_wakeup_duration.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_wu_attribute_group = {
	.attrs = st_lis2du12_wu_attributes,
};

static const struct iio_info st_lis2du12_wu_info = {
	.attrs = &st_lis2du12_wu_attribute_group,
	.read_event_config = st_lis2du12_read_event_config,
	.write_event_config = st_lis2du12_write_event_config,
};

static struct attribute *st_lis2du12_tap_tap_attributes[] = {
	&iio_dev_attr_latency_threshold.dev_attr.attr,
	&iio_dev_attr_quiet_threshold.dev_attr.attr,
	&iio_dev_attr_shock_threshold.dev_attr.attr,
	&iio_dev_attr_tap_priority.dev_attr.attr,
	&iio_dev_attr_tap_thr_x.dev_attr.attr,
	&iio_dev_attr_tap_thr_y.dev_attr.attr,
	&iio_dev_attr_tap_thr_z.dev_attr.attr,
	&iio_dev_attr_tap_enable.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_tap_tap_attribute_group = {
	.attrs = st_lis2du12_tap_tap_attributes,
};

static const struct iio_info st_lis2du12_tap_tap_info = {
	.attrs = &st_lis2du12_tap_tap_attribute_group,
	.read_event_config = st_lis2du12_read_event_config,
	.write_event_config = st_lis2du12_write_event_config,
};

static struct attribute *st_lis2du12_tap_attributes[] = {
	&iio_dev_attr_quiet_threshold.dev_attr.attr,
	&iio_dev_attr_shock_threshold.dev_attr.attr,
	&iio_dev_attr_tap_priority.dev_attr.attr,
	&iio_dev_attr_tap_thr_x.dev_attr.attr,
	&iio_dev_attr_tap_thr_y.dev_attr.attr,
	&iio_dev_attr_tap_thr_z.dev_attr.attr,
	&iio_dev_attr_tap_enable.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_tap_attribute_group = {
	.attrs = st_lis2du12_tap_attributes,
};

static const struct iio_info st_lis2du12_tap_info = {
	.attrs = &st_lis2du12_tap_attribute_group,
	.read_event_config = st_lis2du12_read_event_config,
	.write_event_config = st_lis2du12_write_event_config,
};

static struct attribute *st_lis2du12_ff_attributes[] = {
	&iio_dev_attr_freefall_threshold.dev_attr.attr,
	&iio_dev_attr_freefall_duration.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_ff_attribute_group = {
	.attrs = st_lis2du12_ff_attributes,
};

static const struct iio_info st_lis2du12_ff_info = {
	.attrs = &st_lis2du12_ff_attribute_group,
	.read_event_config = st_lis2du12_read_event_config,
	.write_event_config = st_lis2du12_write_event_config,
};

static struct attribute *st_lis2du12_6d_attributes[] = {
	&iio_dev_attr_sixd_threshold.dev_attr.attr,
	&iio_dev_attr_enable_4d.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_6d_attribute_group = {
	.attrs = st_lis2du12_6d_attributes,
};

static const struct iio_info st_lis2du12_6d_info = {
	.attrs = &st_lis2du12_6d_attribute_group,
	.read_event_config = st_lis2du12_read_event_config,
	.write_event_config = st_lis2du12_write_event_config,
};

static struct attribute *st_lis2du12_act_attributes[] = {
	&iio_dev_attr_sleep_dur.dev_attr.attr,
	&iio_dev_attr_wakeup_threshold.dev_attr.attr,
	&iio_dev_attr_wakeup_duration.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2du12_act_attribute_group = {
	.attrs = st_lis2du12_act_attributes,
};

static const struct iio_info st_lis2du12_act_info = {
	.attrs = &st_lis2du12_act_attribute_group,
	.read_event_config = st_lis2du12_read_event_config,
	.write_event_config = st_lis2du12_write_event_config,
};

static const unsigned long st_lis2du12_avail_acc_scan_masks[] = {
	BIT(2) | BIT(1) | BIT(0), 0x0
};
static const unsigned long st_lis2du12_avail_temp_scan_masks[] = {
	BIT(0), 0x0
};
static const unsigned long st_lis2du12_event_avail_scan_masks[] = {
	BIT(0), 0x0
};

static struct iio_dev *st_lis2du12_alloc_iiodev(struct st_lis2du12_hw *hw,
						enum st_lis2du12_sensor_id id)
{
	struct st_lis2du12_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;

	switch (id) {
	case ST_LIS2DU12_ID_ACC:
		iio_dev->channels = st_lis2du12_acc_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2du12_acc_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_accel";
		iio_dev->info = &st_lis2du12_acc_info;
		iio_dev->available_scan_masks =
				st_lis2du12_avail_acc_scan_masks;

		sensor->odr = st_lis2du12_odr_table[4].hz;
		sensor->uodr = st_lis2du12_odr_table[4].uhz;
		st_lis2du12_set_fs(sensor, st_lis2du12_fs_table[0].gain);
		sensor->offset = 0;
		sensor->watermark = 1;
		break;
	case ST_LIS2DU12_ID_TEMP:
		iio_dev->channels = st_lis2du12_temp_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2du12_temp_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_temp";
		iio_dev->info = &st_lis2du12_temp_info;
		iio_dev->available_scan_masks =
				st_lis2du12_avail_temp_scan_masks;

		sensor->odr = st_lis2du12_odr_table[4].hz;
		sensor->uodr = st_lis2du12_odr_table[4].uhz;
		/* temperature has fixed gain and offset */
		sensor->gain = 45000;
		sensor->offset = 555;
		sensor->watermark = 1;
		break;
	case ST_LIS2DU12_ID_WU:
		iio_dev->channels = st_lis2du12_wu_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2du12_wu_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_wk";
		iio_dev->info = &st_lis2du12_wu_info;
		iio_dev->available_scan_masks =
				st_lis2du12_event_avail_scan_masks;

		sensor->odr = st_lis2du12_odr_table[9].hz;
		break;
	case ST_LIS2DU12_ID_TAP_TAP:
		iio_dev->channels = st_lis2du12_tap_tap_channels;
		iio_dev->num_channels =
			ARRAY_SIZE(st_lis2du12_tap_tap_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_tap_tap";
		iio_dev->info = &st_lis2du12_tap_tap_info;
		iio_dev->available_scan_masks =
				st_lis2du12_event_avail_scan_masks;

		sensor->odr = st_lis2du12_odr_table[9].hz;
		break;
	case ST_LIS2DU12_ID_TAP:
		iio_dev->channels = st_lis2du12_tap_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2du12_tap_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_tap";
		iio_dev->info = &st_lis2du12_tap_info;
		iio_dev->available_scan_masks =
				st_lis2du12_event_avail_scan_masks;

		sensor->odr = st_lis2du12_odr_table[9].hz;
		break;
	case ST_LIS2DU12_ID_FF:
		iio_dev->channels = st_lis2du12_ff_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2du12_ff_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_ff";
		iio_dev->info = &st_lis2du12_ff_info;
		iio_dev->available_scan_masks =
				st_lis2du12_event_avail_scan_masks;

		sensor->odr = st_lis2du12_odr_table[8].hz;
		break;
	case ST_LIS2DU12_ID_6D:
		iio_dev->channels = st_lis2du12_6d_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2du12_6d_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_6d";
		iio_dev->info = &st_lis2du12_6d_info;
		iio_dev->available_scan_masks =
				st_lis2du12_event_avail_scan_masks;

		sensor->odr = st_lis2du12_odr_table[8].hz;
		break;
	case ST_LIS2DU12_ID_ACT:
		iio_dev->channels = st_lis2du12_act_channels;
		iio_dev->num_channels =
				ARRAY_SIZE(st_lis2du12_act_channels);
		iio_dev->name = ST_LIS2DU12_DEV_NAME "_act";
		iio_dev->info = &st_lis2du12_act_info;
		iio_dev->available_scan_masks =
				st_lis2du12_event_avail_scan_masks;

		sensor->odr = st_lis2du12_odr_table[8].hz;
		break;
	default:
		return NULL;
	}

	return iio_dev;
}

int st_lis2du12_probe(struct device *dev, int irq,
		      struct regmap *regmap)
{
	struct st_lis2du12_hw *hw;
	int i, err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->fifo_lock);
	mutex_init(&hw->lock);

	hw->dev = dev;
	hw->irq = irq;
	hw->regmap = regmap;

	err = st_lis2du12_check_whoami(hw);
	if (err < 0)
		return err;

	err = st_lis2du12_init_hw(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_LIS2DU12_ID_MAX; i++) {
		hw->iio_devs[i] = st_lis2du12_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	if (hw->irq > 0) {
		err = st_lis2du12_buffer_setup(hw);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ST_LIS2DU12_ID_MAX; i++) {
		err = devm_iio_device_register(hw->dev,
					       hw->iio_devs[i]);
		if (err)
			return err;
	}

	err = st_lis2du12_embedded_config(hw);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(st_lis2du12_probe);

static int __maybe_unused st_lis2du12_resume(struct device *dev)
{
	struct st_lis2du12_hw *hw = dev_get_drvdata(dev);

	dev_info(dev, "Resuming device\n");

	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);

	return 0;
}

static int __maybe_unused st_lis2du12_suspend(struct device *dev)
{
	struct st_lis2du12_hw *hw = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(hw->irq);

	dev_info(dev, "Suspending device\n");

	return 0;
}

const struct dev_pm_ops st_lis2du12_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_lis2du12_suspend,
				st_lis2du12_resume)
};
EXPORT_SYMBOL(st_lis2du12_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2du12 driver");
MODULE_LICENSE("GPL v2");

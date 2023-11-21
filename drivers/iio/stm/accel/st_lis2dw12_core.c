// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2dw12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <asm/unaligned.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lis2dw12.h"

struct st_lis2dw12_std_entry {
	u16 odr;
	u8 val;
};

struct st_lis2dw12_std_entry st_lis2dw12_std_table[] = {
	{   12, 12 },
	{   25, 18 },
	{   50, 24 },
	{  100, 24 },
	{  200, 32 },
	{  400, 48 },
	{  800, 64 },
	{ 1600, 64 },
};

struct st_lis2dw12_odr {
	u16 hz;
	u8 val;
};

static const struct st_lis2dw12_odr st_lis2dw12_odr_table[] = {
	{    0, 0x0 }, /* power-down */
	{   12, 0x2 }, /* LP 12.5Hz */
	{   25, 0x3 }, /* LP 25Hz*/
	{   50, 0x4 }, /* LP 50Hz*/
	{  100, 0x5 }, /* LP 100Hz*/
	{  200, 0x6 }, /* LP 200Hz*/
	{  400, 0x7 }, /* HP 400Hz*/
	{  800, 0x8 }, /* HP 800Hz*/
	{ 1600, 0x9 }, /* HP 1600Hz*/
};

struct st_lis2dw12_fs {
	u32 gain;
	u8 val;
};

static const struct st_lis2dw12_fs st_lis2dw12_fs_table[] = {
	{  ST_LIS2DW12_FS_2G_GAIN, 0x0 },
	{  ST_LIS2DW12_FS_4G_GAIN, 0x1 },
	{  ST_LIS2DW12_FS_8G_GAIN, 0x2 },
	{ ST_LIS2DW12_FS_16G_GAIN, 0x3 },
};

struct st_lis2dw12_selftest_req {
	char *mode;
	u8 val;
};

struct st_lis2dw12_selftest_req st_lis2dw12_selftest_table[] = {
	{ "disabled", 0x0 },
	{ "positive-sign", 0x1 },
	{ "negative-sign", 0x2 },
};

#define ST_LIS2DW12_ACC_CHAN(addr, ch2, idx)				\
{									\
	.type = IIO_ACCEL,						\
	.address = addr,						\
	.modified = 1,							\
	.channel2 = ch2,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_SCALE),			\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = idx,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 14,						\
		.storagebits = 16,					\
		.shift = 2,						\
		.endianness = IIO_LE,					\
	},								\
}

const struct iio_event_spec st_lis2dw12_fifo_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

const struct iio_event_spec st_lis2dw12_rthr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

#define ST_LIS2DW12_EVENT_CHANNEL(chan_type, evt_spec)	\
{							\
	.type = chan_type,				\
	.modified = 0,					\
	.scan_index = -1,				\
	.indexed = -1,					\
	.event_spec = evt_spec,				\
	.num_event_specs = 1,				\
}

static const struct iio_chan_spec st_lis2dw12_acc_channels[] = {
	ST_LIS2DW12_ACC_CHAN(ST_LIS2DW12_OUT_X_L_ADDR, IIO_MOD_X, 0),
	ST_LIS2DW12_ACC_CHAN(ST_LIS2DW12_OUT_Y_L_ADDR, IIO_MOD_Y, 1),
	ST_LIS2DW12_ACC_CHAN(ST_LIS2DW12_OUT_Z_L_ADDR, IIO_MOD_Z, 2),
	ST_LIS2DW12_EVENT_CHANNEL(IIO_ACCEL, &st_lis2dw12_fifo_flush_event),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec st_lis2dw12_tap_tap_channels[] = {
	ST_LIS2DW12_EVENT_CHANNEL(STM_IIO_TAP_TAP, &st_lis2dw12_rthr_event),
};

static const struct iio_chan_spec st_lis2dw12_tap_channels[] = {
	ST_LIS2DW12_EVENT_CHANNEL(STM_IIO_TAP, &st_lis2dw12_rthr_event),
};

static const struct iio_chan_spec st_lis2dw12_wu_channels[] = {
	ST_LIS2DW12_EVENT_CHANNEL(STM_IIO_GESTURE, &st_lis2dw12_rthr_event),
};

static int st_lis2dw12_set_fs(struct st_lis2dw12_sensor *sensor, u16 gain)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(st_lis2dw12_fs_table); i++)
		if (st_lis2dw12_fs_table[i].gain == gain)
			break;

	if (i == ARRAY_SIZE(st_lis2dw12_fs_table))
		return -EINVAL;

	err = st_lis2dw12_write_with_mask_locked(sensor->hw,
						 ST_LIS2DW12_CTRL6_ADDR,
						 ST_LIS2DW12_FS_MASK,
						 st_lis2dw12_fs_table[i].val);
	if (err < 0)
		return err;

	sensor->gain = gain;

	return 0;
}

static inline int st_lis2dw12_get_odr_idx(u16 odr, u8 *idx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lis2dw12_odr_table); i++)
		if (st_lis2dw12_odr_table[i].hz == odr)
			break;

	if (i == ARRAY_SIZE(st_lis2dw12_odr_table))
		return -EINVAL;

	*idx = i;

	return 0;
}

static int st_lis2dw12_set_std_level(struct st_lis2dw12_hw *hw, u16 odr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lis2dw12_std_table); i++)
		if (st_lis2dw12_std_table[i].odr == odr)
			break;

	if (i == ARRAY_SIZE(st_lis2dw12_std_table))
		return -EINVAL;

	hw->std_level = st_lis2dw12_std_table[i].val;

	return 0;
}

static u16 st_lis2dw12_check_odr_dependency(struct st_lis2dw12_hw *hw, u16 odr,
					    enum st_lis2dw12_sensor_id ref_id)
{
	struct st_lis2dw12_sensor *ref = iio_priv(hw->iio_devs[ref_id]);
	bool enable = odr > 0;
	u16 ret;

	if (enable) {
		if (hw->enable_mask & BIT(ref_id))
			ret = max_t(u32, ref->odr, odr);
		else
			ret = odr;
	} else {
		ret = (hw->enable_mask & BIT(ref_id)) ? ref->odr : 0;
	}

	return ret;
}

static int st_lis2dw12_set_odr(struct st_lis2dw12_sensor *sensor, u16 req_odr)
{
	struct st_lis2dw12_hw *hw = sensor->hw;
	u16 upd_odr = req_odr;
	u8 mode, val, i;
	int err, odr;

	for (i = 0; i < ST_LIS2DW12_ID_MAX; i++) {
		if (i == sensor->id)
			continue;

		odr = st_lis2dw12_check_odr_dependency(hw, upd_odr, i);
		if (odr > upd_odr)
			upd_odr = odr;
	}

	err = st_lis2dw12_get_odr_idx(upd_odr, &i);
	if (err < 0)
		return err;

	mode = req_odr > 200 ? 0x1 : 0x0;
	val = (st_lis2dw12_odr_table[i].val << __ffs(ST_LIS2DW12_ODR_MASK)) |
	      (mode << __ffs(ST_LIS2DW12_MODE_MASK)) | 0x01;

	/*
	 * disable cache support when setting odr register, use the
	 * driver api to restore it
	 */
	regcache_cache_bypass(hw->regmap, true);
	err = st_lis2dw12_write_locked(hw, ST_LIS2DW12_CTRL1_ADDR,
				       &val, sizeof(val));

	regcache_cache_bypass(hw->regmap, false);

	return err < 0 ? err : 0;
}

static int st_lis2dw12_check_whoami(struct st_lis2dw12_hw *hw)
{
	int data, err;

	err = regmap_read(hw->regmap, ST_LIS2DW12_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != ST_LIS2DW12_WHOAMI_VAL) {
		dev_err(hw->dev, "wrong whoami %02x vs %02x\n",
			data, ST_LIS2DW12_WHOAMI_VAL);
		return -ENODEV;
	}

	return 0;
}

static int st_lis2dw12_of_get_drdy_pin(struct st_lis2dw12_hw *hw)
{
	struct device_node *np = hw->dev->of_node;
	int err;

	if (!np)
		return -EINVAL;

	err = of_property_read_u32(np, "st,drdy-int-pin", &hw->irq_pin);
	if (err != 0)
		return -EINVAL;

	hw->irq = of_irq_get(np, 0);
	if (hw->irq < 0)
		return hw->irq;

	if (hw->irq_pin == 1) {
		/* in case same pin only one irq is requested */
		hw->irq_emb = -1;

		return 0;
	}

	/*
	 * embedded feature when irq is on int 2 require a new dedicated
	 * irq line
	 */
	hw->irq_emb = of_irq_get(np, 1);
	if (hw->irq_emb < 0) {
		dev_err(hw->dev,
			"embedded feature require a irq line\n");

		return -EINVAL;
	}

	return 0;
}

static int st_lis2dw12_get_drdy_pin(struct st_lis2dw12_hw *hw)
{
	int err = 0;

	if (st_lis2dw12_of_get_drdy_pin(hw) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		hw->irq_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (hw->irq_pin) {
	case 1:
		hw->irq_reg = ST_LIS2DW12_CTRL4_INT1_CTRL_ADDR;
		break;
	case 2:
		hw->irq_reg = ST_LIS2DW12_CTRL5_INT2_CTRL_ADDR;
		break;
	default:
		dev_err(hw->dev, "unsupported interrupt pin\n");
		err = -EINVAL;
		break;
	}

	return err;
}

static int st_lis2dw12_init_hw(struct st_lis2dw12_hw *hw)
{
	int err;

	/* soft reset the device */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_CTRL2_ADDR,
						 ST_LIS2DW12_RESET_MASK, 1);
	if (err < 0)
		return err;

	/* enable BDU */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_CTRL2_ADDR,
						 ST_LIS2DW12_BDU_MASK, 1);
	if (err < 0)
		return err;

	/* enable all interrupts */
	err = st_lis2dw12_write_with_mask_locked(hw,
						 ST_LIS2DW12_ABS_INT_CFG_ADDR,
						 ST_LIS2DW12_ALL_INT_MASK, 1);
	if (err < 0)
		return err;

	/* configure fifo watermark */
	err = st_lis2dw12_update_fifo_watermark(hw, hw->watermark);
	if (err < 0)
		return err;

	/* configure default free fall event threshold */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_FREE_FALL_ADDR,
						 ST_LIS2DW12_FREE_FALL_THS_MASK,
						 1);
	if (err < 0)
		return err;

	/* configure default free fall event duration */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_FREE_FALL_ADDR,
						 ST_LIS2DW12_FREE_FALL_DUR_MASK,
						 1);
	if (err < 0)
		return err;

	/* enable tap event on all axes */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_TAP_THS_Z_ADDR,
						 ST_LIS2DW12_TAP_AXIS_MASK,
						 0x7);
	if (err < 0)
		return err;

	/* configure default threshold for Tap event recognition */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_TAP_THS_X_ADDR,
						 ST_LIS2DW12_TAP_THS_MAK, 9);
	if (err < 0)
		return err;

	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_TAP_THS_Y_ADDR,
						 ST_LIS2DW12_TAP_THS_MAK, 9);
	if (err < 0)
		return err;

	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_TAP_THS_Z_ADDR,
						 ST_LIS2DW12_TAP_THS_MAK, 9);
	if (err < 0)
		return err;

	/* low noise enabled by default */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_CTRL6_ADDR,
						 ST_LIS2DW12_LN_MASK, 1);
	if (err < 0)
		return err;

	/* BW = ODR/4 */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_CTRL6_ADDR,
						 ST_LIS2DW12_BW_MASK, 1);
	if (err < 0)
		return err;

	/* enable latched mode */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_CTRL3_ADDR,
						 ST_LIS2DW12_LIR_MASK, 1);
	if (err < 0)
		return err;

	/* configure interrupt pin */
	err = st_lis2dw12_get_drdy_pin(hw);
	if (err < 0)
		return err;

	return st_lis2dw12_write_with_mask_locked(hw, hw->irq_reg,
						  ST_LIS2DW12_FTH_INT_MASK, 1);
}

static ssize_t
st_lis2dw12_sysfs_sampling_frequency_avl(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i, len = 0;

	for (i = 1; i < ARRAY_SIZE(st_lis2dw12_odr_table); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 st_lis2dw12_odr_table[i].hz);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_lis2dw12_sysfs_scale_avail(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st_lis2dw12_fs_table); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
				 st_lis2dw12_fs_table[i].gain);
	buf[len - 1] = '\n';

	return len;
}

int st_lis2dw12_sensor_set_enable(struct st_lis2dw12_sensor *sensor,
				  bool enable)
{
	struct st_lis2dw12_hw *hw = sensor->hw;
	u16 val = enable ? sensor->odr : 0;
	int err;

	err = st_lis2dw12_set_odr(sensor, val);
	if (err < 0)
		return err;

	if (enable)
		hw->enable_mask |= BIT(sensor->id);
	else
		hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

static int st_lis2dw12_read_oneshot(struct st_lis2dw12_sensor *sensor,
				    u8 addr, int *val)
{
	struct st_lis2dw12_hw *hw = sensor->hw;
	int err, delay;
	u8 data[2];

	err = st_lis2dw12_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	/* sample to discard, 3 * odr us */
	delay = 3000000 / sensor->odr;
	usleep_range(delay, delay + 1);

	err = st_lis2dw12_read(hw, addr, &data, sizeof(data));
	if (err < 0)
		return err;

	st_lis2dw12_sensor_set_enable(sensor, false);

	*val = (s16)get_unaligned_le16(data);

	return IIO_VAL_INT;
}

static int st_lis2dw12_read_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *ch,
				int *val, int *val2, long mask)
{
	struct st_lis2dw12_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&iio_dev->mlock);
		if (iio_buffer_enabled(iio_dev)) {
			ret = -EBUSY;
			mutex_unlock(&iio_dev->mlock);
			break;
		}
		ret = st_lis2dw12_read_oneshot(sensor, ch->address, val);
		mutex_unlock(&iio_dev->mlock);
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sensor->gain;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = sensor->odr;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_lis2dw12_write_raw(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct st_lis2dw12_sensor *sensor = iio_priv(iio_dev);
	int err;

	mutex_lock(&iio_dev->mlock);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_lis2dw12_set_fs(sensor, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		u8 data;

		err = st_lis2dw12_set_std_level(sensor->hw, val);
		if (err < 0)
			break;

		err = st_lis2dw12_get_odr_idx(val, &data);
		if (!err)
			sensor->odr = val;
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	mutex_unlock(&iio_dev->mlock);

	return err;
}

static int st_lis2dw12_read_event_config(struct iio_dev *iio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir)
{
	struct st_lis2dw12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2dw12_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

static int st_lis2dw12_write_event_config(struct iio_dev *iio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  int state)
{
	struct st_lis2dw12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2dw12_hw *hw = sensor->hw;
	u8 data[2] = {}, drdy_val, drdy_mask;
	int err;

	/* Read initial configuration data */
	err = st_lis2dw12_read(hw, ST_LIS2DW12_INT_DUR_ADDR,
			       &data, sizeof(data));
	if (err < 0)
		return -EINVAL;

	switch (sensor->id) {
	case ST_LIS2DW12_ID_WU:
		drdy_mask = ST_LIS2DW12_WU_INT1_MASK;
		drdy_val = state ? 1 : 0;
		data[1] = state ? 0x02 : 0;
		break;
	case ST_LIS2DW12_ID_TAP_TAP:
		drdy_mask = ST_LIS2DW12_TAP_TAP_INT1_MASK;
		drdy_val = state ? 1 : 0;
		if (state) {
			data[0] |= 0x7f;
			data[1] |= 0x80;
		} else {
			data[0] &= ~0x7f;
			data[1] &= ~0x80;
		}
		break;
	case ST_LIS2DW12_ID_TAP:
		drdy_mask = ST_LIS2DW12_TAP_INT1_MASK;
		drdy_val = state ? 1 : 0;
		if (state) {
			data[0] |= 6;
		} else {
			data[0] &= ~6;
		}
		break;
	default:
		return -EINVAL;
	}

	err = st_lis2dw12_write_locked(hw, ST_LIS2DW12_INT_DUR_ADDR,
				       data, sizeof(data));
	if (err < 0)
		return err;

	err = st_lis2dw12_write_with_mask_locked(hw,
					       ST_LIS2DW12_CTRL4_INT1_CTRL_ADDR,
					       drdy_mask, drdy_val);
	if (err < 0)
		return err;

	return st_lis2dw12_sensor_set_enable(sensor, state);
}

static ssize_t st_lis2dw12_get_hwfifo_watermark(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2dw12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2dw12_hw *hw = sensor->hw;

	return sprintf(buf, "%d\n", hw->watermark);
}

static ssize_t
st_lis2dw12_get_max_hwfifo_watermark(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return sprintf(buf, "%d\n", ST_LIS2DW12_MAX_WATERMARK);
}

static ssize_t st_lis2dw12_get_selftest_avail(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return sprintf(buf, "%s, %s\n", st_lis2dw12_selftest_table[1].mode,
		       st_lis2dw12_selftest_table[2].mode);
}

static ssize_t st_lis2dw12_get_selftest_status(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2dw12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2dw12_hw *hw = sensor->hw;
	char *ret;

	switch (hw->st_status) {
	case ST_LIS2DW12_ST_PASS:
		ret = "pass";
		break;
	case ST_LIS2DW12_ST_FAIL:
		ret = "fail";
		break;
	default:
	case ST_LIS2DW12_ST_RESET:
		ret = "na";
		break;
	}

	return sprintf(buf, "%s\n", ret);
}

static ssize_t st_lis2dw12_enable_selftest(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2dw12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2dw12_hw *hw = sensor->hw;
	s16 acc_st_x = 0, acc_st_y = 0, acc_st_z = 0;
	s16 acc_x = 0, acc_y = 0, acc_z = 0;
	u8 data[ST_LIS2DW12_DATA_SIZE], val;
	int i, err, gain;

	mutex_lock(&iio_dev->mlock);

	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;
		goto unlock;
	}

	for (i = 0; i < ARRAY_SIZE(st_lis2dw12_selftest_table); i++)
		if (!strncmp(buf, st_lis2dw12_selftest_table[i].mode,
			     size - 2))
			break;

	if (i == ARRAY_SIZE(st_lis2dw12_selftest_table)) {
		err = -EINVAL;
		goto unlock;
	}

	hw->st_status = ST_LIS2DW12_ST_RESET;
	val = st_lis2dw12_selftest_table[i].val;
	gain = sensor->gain;

	/* fs = 2g, odr = 50Hz */
	err = st_lis2dw12_set_fs(sensor, ST_LIS2DW12_FS_2G_GAIN);
	if (err < 0)
		goto unlock;

	err = st_lis2dw12_set_odr(sensor, 50);
	if (err < 0)
		goto unlock;

	msleep(200);

	for (i = 0; i < 5; i++) {
		err = st_lis2dw12_read(hw, ST_LIS2DW12_OUT_X_L_ADDR,
				       &data, sizeof(data));
		if (err < 0)
			goto unlock;

		acc_x += ((s16)get_unaligned_le16(&data[0]) >> 2) / 5;
		acc_y += ((s16)get_unaligned_le16(&data[2]) >> 2) / 5;
		acc_z += ((s16)get_unaligned_le16(&data[4]) >> 2) / 5;

		msleep(10);
	}

	/* enable self test */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_CTRL3_ADDR,
						 ST_LIS2DW12_ST_MASK, val);
	if (err < 0)
		goto unlock;

	msleep(200);

	for (i = 0; i < 5; i++) {
		err = st_lis2dw12_read(hw, ST_LIS2DW12_OUT_X_L_ADDR,
				       &data, sizeof(data));
		if (err < 0)
			goto unlock;

		acc_st_x += ((s16)get_unaligned_le16(&data[0]) >> 2) / 5;
		acc_st_y += ((s16)get_unaligned_le16(&data[2]) >> 2) / 5;
		acc_st_z += ((s16)get_unaligned_le16(&data[4]) >> 2) / 5;

		msleep(10);
	}

	if (abs(acc_st_x - acc_x) >= ST_LIS2DW12_SELFTEST_MIN &&
	    abs(acc_st_x - acc_x) <= ST_LIS2DW12_SELFTEST_MAX &&
	    abs(acc_st_y - acc_y) >= ST_LIS2DW12_SELFTEST_MIN &&
	    abs(acc_st_y - acc_y) >= ST_LIS2DW12_SELFTEST_MIN &&
	    abs(acc_st_z - acc_z) >= ST_LIS2DW12_SELFTEST_MIN &&
	    abs(acc_st_z - acc_z) >= ST_LIS2DW12_SELFTEST_MIN)
		hw->st_status = ST_LIS2DW12_ST_PASS;
	else
		hw->st_status = ST_LIS2DW12_ST_FAIL;

	/* disable self test */
	err = st_lis2dw12_write_with_mask_locked(hw, ST_LIS2DW12_CTRL3_ADDR,
						 ST_LIS2DW12_ST_MASK, 0);
	if (err < 0)
		goto unlock;

	err = st_lis2dw12_set_fs(sensor, gain);
	if (err < 0)
		goto unlock;

	err = st_lis2dw12_sensor_set_enable(sensor, false);

unlock:
	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : size;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lis2dw12_sysfs_sampling_frequency_avl);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_lis2dw12_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0644,
		       st_lis2dw12_get_hwfifo_watermark,
		       st_lis2dw12_set_hwfifo_watermark, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark_max, 0444,
		       st_lis2dw12_get_max_hwfifo_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_flush, S_IWUSR, NULL,
		       st_lis2dw12_flush_fifo, 0);
static IIO_DEVICE_ATTR(selftest_available, 0444,
		       st_lis2dw12_get_selftest_avail, NULL, 0);
static IIO_DEVICE_ATTR(selftest, 0644, st_lis2dw12_get_selftest_status,
		       st_lis2dw12_enable_selftest, 0);

static struct attribute *st_lis2dw12_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2dw12_acc_attribute_group = {
	.attrs = st_lis2dw12_acc_attributes,
};

static const struct iio_info st_lis2dw12_acc_info = {
	.attrs = &st_lis2dw12_acc_attribute_group,
	.read_raw = st_lis2dw12_read_raw,
	.write_raw = st_lis2dw12_write_raw,
};

static struct attribute *st_lis2dw12_wu_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2dw12_wu_attribute_group = {
	.attrs = st_lis2dw12_wu_attributes,
};

static const struct iio_info st_lis2dw12_wu_info = {
	.attrs = &st_lis2dw12_wu_attribute_group,
	.read_event_config = st_lis2dw12_read_event_config,
	.write_event_config = st_lis2dw12_write_event_config,
};

static struct attribute *st_lis2dw12_tap_tap_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2dw12_tap_tap_attribute_group = {
	.attrs = st_lis2dw12_tap_tap_attributes,
};

static const struct iio_info st_lis2dw12_tap_tap_info = {
	.attrs = &st_lis2dw12_tap_tap_attribute_group,
	.read_event_config = st_lis2dw12_read_event_config,
	.write_event_config = st_lis2dw12_write_event_config,
};

static struct attribute *st_lis2dw12_tap_attributes[] = {
	NULL,
};

static const struct attribute_group st_lis2dw12_tap_attribute_group = {
	.attrs = st_lis2dw12_tap_attributes,
};

static const struct iio_info st_lis2dw12_tap_info = {
	.attrs = &st_lis2dw12_tap_attribute_group,
	.read_event_config = st_lis2dw12_read_event_config,
	.write_event_config = st_lis2dw12_write_event_config,
};

static const unsigned long st_lis2dw12_avail_scan_masks[] = { 0x7, 0x0 };
static const unsigned long st_lis2dw12_event_avail_scan_masks[] = { 0x1, 0x0 };

static struct iio_dev *st_lis2dw12_alloc_iiodev(struct st_lis2dw12_hw *hw,
						enum st_lis2dw12_sensor_id id)
{
	struct st_lis2dw12_sensor *sensor;
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
	case ST_LIS2DW12_ID_ACC:
		iio_dev->channels = st_lis2dw12_acc_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2dw12_acc_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_accel", hw->name);
		iio_dev->info = &st_lis2dw12_acc_info;
		iio_dev->available_scan_masks = st_lis2dw12_avail_scan_masks;

		sensor->odr = st_lis2dw12_odr_table[1].hz;
		sensor->gain = st_lis2dw12_fs_table[0].gain;
		break;
	case ST_LIS2DW12_ID_WU:
		iio_dev->channels = st_lis2dw12_wu_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2dw12_wu_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_wk", hw->name);
		iio_dev->info = &st_lis2dw12_wu_info;
		iio_dev->available_scan_masks =
				st_lis2dw12_event_avail_scan_masks;

		sensor->odr = st_lis2dw12_odr_table[5].hz;
		break;
	case ST_LIS2DW12_ID_TAP_TAP:
		iio_dev->channels = st_lis2dw12_tap_tap_channels;
		iio_dev->num_channels =
			ARRAY_SIZE(st_lis2dw12_tap_tap_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_tap_tap", hw->name);
		iio_dev->info = &st_lis2dw12_tap_tap_info;
		iio_dev->available_scan_masks =
				st_lis2dw12_event_avail_scan_masks;

		sensor->odr = st_lis2dw12_odr_table[6].hz;
		break;
	case ST_LIS2DW12_ID_TAP:
		iio_dev->channels = st_lis2dw12_tap_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2dw12_tap_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_tap", hw->name);
		iio_dev->info = &st_lis2dw12_tap_info;
		iio_dev->available_scan_masks =
				st_lis2dw12_event_avail_scan_masks;

		sensor->odr = st_lis2dw12_odr_table[6].hz;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

int st_lis2dw12_probe(struct device *dev, int irq, const char *name,
		      struct regmap *regmap)
{
	struct st_lis2dw12_hw *hw;
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
	hw->watermark = 1;

	err = st_lis2dw12_check_whoami(hw);
	if (err < 0)
		return err;

	scnprintf(hw->name, sizeof(hw->name), "%s", name);

	err = st_lis2dw12_init_hw(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_LIS2DW12_ID_MAX; i++) {
		hw->iio_devs[i] = st_lis2dw12_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	if (hw->irq > 0) {
		err = st_lis2dw12_fifo_setup(hw);
		if (err)
			return err;
	}

	for (i = 0; i < ST_LIS2DW12_ID_MAX; i++) {
		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

	device_init_wakeup(dev,
			   device_property_read_bool(dev,
						     "wakeup-source"));
	return 0;
}
EXPORT_SYMBOL(st_lis2dw12_probe);

static int __maybe_unused st_lis2dw12_suspend(struct device *dev)
{
	struct st_lis2dw12_hw *hw = dev_get_drvdata(dev);
	struct st_lis2dw12_sensor *sensor;
	int i, err = 0;

	if (hw->irq > 0)
		disable_hardirq(hw->irq);

	for (i = 0; i < ST_LIS2DW12_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		/* do not disable sensors if requested by wake-up */
		if (!((hw->enable_mask & BIT(sensor->id)) &
		      ST_LIS2DW12_WAKE_UP_SENSORS)) {
			err = st_lis2dw12_set_odr(sensor, 0);
			if (err < 0)
				return err;
		} else {
			err = st_lis2dw12_set_odr(sensor,
					 ST_LIS2DW12_MIN_ODR_IN_WAKEUP);
			if (err < 0)
				return err;
		}
	}

	if (st_lis2dw12_is_fifo_enabled(hw)) {
		err = st_lis2dw12_suspend_fifo(hw);
		if (err < 0)
			return err;
	}

	regcache_mark_dirty(hw->regmap);

	if (hw->enable_mask & ST_LIS2DW12_WAKE_UP_SENSORS) {
		if (device_may_wakeup(dev))
			enable_irq_wake(hw->irq);
	}

	dev_info(dev, "Suspending device\n");

	return err < 0 ? err : 0;
}

static int __maybe_unused st_lis2dw12_resume(struct device *dev)
{
	struct st_lis2dw12_hw *hw = dev_get_drvdata(dev);
	struct st_lis2dw12_sensor *sensor;
	int i, err = 0;

	dev_info(dev, "Resuming device\n");

	regcache_sync(hw->regmap);

	if (hw->enable_mask & ST_LIS2DW12_WAKE_UP_SENSORS) {
		if (device_may_wakeup(dev))
			disable_irq_wake(hw->irq);
	}

	for (i = 0; i < ST_LIS2DW12_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		if (!hw->iio_devs[i])
			continue;

		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_lis2dw12_set_odr(sensor, sensor->odr);
		if (err < 0)
			return err;
	}

	if (st_lis2dw12_is_fifo_enabled(hw))
		err = st_lis2dw12_set_fifo_mode(hw,
					   ST_LIS2DW12_FIFO_CONTINUOUS);

	if (hw->irq > 0)
		enable_irq(hw->irq);

	return err < 0 ? err : 0;
}

const struct dev_pm_ops st_lis2dw12_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_lis2dw12_suspend, st_lis2dw12_resume)
};
EXPORT_SYMBOL(st_lis2dw12_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2dw12 driver");
MODULE_LICENSE("GPL v2");


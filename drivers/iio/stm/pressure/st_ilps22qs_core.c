// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ilps22qs driver
 *
 * Copyright 2023 STMicroelectronics Inc.
 *
 * MEMS Software Solutions Team
 */

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>

#include "st_ilps22qs.h"

const static struct st_ilps22qs_odr_table_t st_ilps22qs_odr_table = {
	.size = ST_ILPS22QS_ODR_LIST_NUM,
	.reg = {
		.addr = ST_ILPS22QS_CTRL1_ADDR,
		.mask = ST_ILPS22QS_ODR_MASK,
	},
	.odr_avl[0] = {   1, 0x01 },
	.odr_avl[1] = {   4, 0x02 },
	.odr_avl[2] = {  10, 0x03 },
	.odr_avl[3] = {  25, 0x04 },
	.odr_avl[4] = {  50, 0x05 },
	.odr_avl[5] = {  75, 0x06 },
	.odr_avl[6] = { 100, 0x07 },
	.odr_avl[7] = { 200, 0x08 },
};

static const struct iio_chan_spec st_ilps22qs_press_channels[] = {
	{
		.type = IIO_PRESSURE,
		.address = ST_ILPS22QS_PRESS_OUT_XL_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.channel2 = IIO_NO_MOD,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec st_ilps22qs_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.address = ST_ILPS22QS_TEMP_OUT_L_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.channel2 = IIO_NO_MOD,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec st_ilps22qs_qvar_channels[] = {
	{
		.type = IIO_ALTVOLTAGE,
		.address = ST_ILPS22QS_PRESS_OUT_XL_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.channel2 = IIO_NO_MOD,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		}
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static enum hrtimer_restart
st_ilps22qs_poll_function_read(struct hrtimer *timer)
{
	struct st_ilps22qs_sensor *sensor;

	sensor = container_of((struct hrtimer *)timer,
			      struct st_ilps22qs_sensor, hr_timer);

	sensor->timestamp = st_ilps22qs_get_time_ns(sensor->hw);
	queue_work(sensor->hw->workqueue, &sensor->iio_work);

	return HRTIMER_NORESTART;
}

static void st_ilps22qs_report_temp(struct st_ilps22qs_sensor *sensor,
				    u8 *tmp, int64_t timestamp)
{
	struct iio_dev *iio_dev = sensor->hw->iio_devs[sensor->id];
	u8 iio_buf[ALIGN(2, sizeof(s64)) + sizeof(s64)];

	memcpy(iio_buf, tmp, 2);
	iio_push_to_buffers_with_timestamp(iio_dev, iio_buf, timestamp);
}

static void st_silps22qs_report_press_qvar(struct st_ilps22qs_sensor *sensor,
					   u8 *tmp, int64_t timestamp)
{
	u8 iio_buf[ALIGN(3, sizeof(s64)) + sizeof(s64)];
	struct st_ilps22qs_hw *hw = sensor->hw;
	struct iio_dev *iio_dev;

	mutex_lock(&hw->lock);
	if (hw->interleave) {
		if (tmp[0] & 0x01)
			iio_dev = sensor->hw->iio_devs[ST_ILPS22QS_QVAR];
		else
			iio_dev = sensor->hw->iio_devs[ST_ILPS22QS_PRESS];
	} else {
		iio_dev = sensor->hw->iio_devs[sensor->id];
	}
	mutex_unlock(&hw->lock);

	memcpy(iio_buf, tmp, 3);
	iio_push_to_buffers_with_timestamp(iio_dev, iio_buf, timestamp);
}

static void st_ilps22qs_poll_function_work(struct work_struct *iio_work)
{
	struct st_ilps22qs_sensor *sensor;
	struct st_ilps22qs_hw *hw;
	ktime_t tmpkt, ktdelta;
	int len;
	int err;
	int id;

	sensor = container_of((struct work_struct *)iio_work,
			      struct st_ilps22qs_sensor, iio_work);
	hw = sensor->hw;
	id = sensor->id;

	/* adjust delta time */
	ktdelta = ktime_set(0,
			    (st_ilps22qs_get_time_ns(hw) - sensor->timestamp));

	/* avoid negative value in case of high odr */
	mutex_lock(&hw->lock);
	if (ktime_after(sensor->ktime, ktdelta))
		tmpkt = ktime_sub(sensor->ktime, ktdelta);
	else
		tmpkt = sensor->ktime;

	hrtimer_start(&sensor->hr_timer, tmpkt, HRTIMER_MODE_REL);
	mutex_unlock(&sensor->hw->lock);

	len = hw->iio_devs[id]->channels->scan_type.realbits >> 3;

	switch (id) {
	case ST_ILPS22QS_PRESS:
	case ST_ILPS22QS_QVAR: {
		u8 data[3];

		err = st_ilps22qs_read_locked(hw,
					    hw->iio_devs[id]->channels->address,
					    data, len);
		if (err < 0)
			return;

		st_silps22qs_report_press_qvar(sensor, data, sensor->timestamp);
		}
		break;
	case ST_ILPS22QS_TEMP: {
		u8 data[2];

		err = st_ilps22qs_read_locked(hw,
					    hw->iio_devs[id]->channels->address,
					    data, len);
		if (err < 0)
			return;

		st_ilps22qs_report_temp(sensor, data, sensor->timestamp);
		}
		break;
	default:
		break;
	}
}

static int st_ilps22qs_check_whoami(struct st_ilps22qs_hw *hw)
{
	int data;
	int err;

	err = regmap_read(hw->regmap, ST_ILPS22QS_WHO_AM_I_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");

		return err;
	}

	if (data != ST_ILPS22QS_WHOAMI_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);

		return -ENODEV;
	}

	return 0;
}

static __maybe_unused int st_ilps22qs_reg_access(struct iio_dev *iio_dev,
						 unsigned int reg,
						 unsigned int writeval,
						 unsigned int *readval)
{
	struct st_ilps22qs_sensor *sensor = iio_priv(iio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	if (readval == NULL)
		ret = regmap_write(sensor->hw->regmap, reg, writeval);
	else
		ret = regmap_read(sensor->hw->regmap, reg, readval);

	iio_device_release_direct_mode(iio_dev);

	return (ret < 0) ? ret : 0;
}

static int st_ilps22qs_get_odr(struct st_ilps22qs_sensor *sensor, u8 odr)
{
	int i;

	for (i = 0; i < st_ilps22qs_odr_table.size; i++) {
		if (st_ilps22qs_odr_table.odr_avl[i].hz >= odr)
			break;
	}

	return i == st_ilps22qs_odr_table.size ? -EINVAL : i;
}

static int st_ilps22qs_set_odr(struct st_ilps22qs_sensor *sensor, u8 odr)
{
	struct st_ilps22qs_hw *hw = sensor->hw;
	u8 max_odr = odr;
	int i;

	for (i = 0; i < ST_ILPS22QS_SENSORS_NUM; i++) {
		if ((hw->enable_mask & BIT(i)) && (sensor->id != i)) {
			struct st_ilps22qs_sensor *temp;

			temp = iio_priv(hw->iio_devs[i]);
			max_odr = max_t(u32, max_odr, temp->odr);
		}
	}

	if (max_odr != hw->odr) {
		int err, ret;

		ret = st_ilps22qs_get_odr(sensor, max_odr);
		if (ret < 0)
			return ret;

		err = st_ilps22qs_update_locked(hw,
					 st_ilps22qs_odr_table.reg.addr,
					 st_ilps22qs_odr_table.reg.mask,
					 st_ilps22qs_odr_table.odr_avl[ret].val);
		if (err < 0)
			return err;

		hw->odr = max_odr;
	}

	return 0;
}

/* need hw->lock */
static int st_ilps22qs_set_interleave(struct st_ilps22qs_sensor *sensor,
				      bool enable)
{
	struct st_ilps22qs_hw *hw = sensor->hw;
	int otherid = sensor->id == ST_ILPS22QS_PRESS ? ST_ILPS22QS_QVAR :
							ST_ILPS22QS_PRESS;
	int interleave;
	int err = 0;

	/* both press / qvar enabling ? */
	mutex_lock(&hw->lock);
	interleave = (!!(hw->enable_mask & BIT(otherid))) && enable;
	if (interleave) {
		unsigned int ctrl1;

		err = regmap_bulk_read(hw->regmap,
				       ST_ILPS22QS_CTRL1_ADDR,
				       &ctrl1, 1);
		if (err < 0)
			goto unlock;

		err = regmap_update_bits(hw->regmap,
					 ST_ILPS22QS_CTRL1_ADDR,
					 ST_ILPS22QS_ODR_MASK, 0);
		if (err < 0)
			goto unlock;

		err = regmap_update_bits(hw->regmap,
					 ST_ILPS22QS_CTRL3_ADDR,
					 ST_ILPS22QS_AH_QVAR_EN_MASK, 0);
		if (err < 0)
			goto unlock;

		err = regmap_update_bits(hw->regmap, ST_ILPS22QS_CTRL3_ADDR,
					 ST_ILPS22QS_AH_QVAR_P_AUTO_EN_MASK,
					 ST_ILPS22QS_SHIFT_VAL(interleave,
					    ST_ILPS22QS_AH_QVAR_P_AUTO_EN_MASK));
		if (err < 0)
			goto unlock;

		err = regmap_update_bits(hw->regmap, ST_ILPS22QS_CTRL1_ADDR,
					 ST_ILPS22QS_ODR_MASK, ctrl1);
		if (err < 0)
			goto unlock;
	} else if (hw->interleave) {
		err = regmap_update_bits(hw->regmap, ST_ILPS22QS_CTRL3_ADDR,
					 ST_ILPS22QS_AH_QVAR_P_AUTO_EN_MASK, 0);
		if (err < 0)
			goto unlock;
	}

	hw->interleave = interleave;

unlock:
	mutex_unlock(&hw->lock);

	return err;
}

static int st_ilps22qs_hw_enable(struct st_ilps22qs_sensor *sensor, bool enable)
{
	int ret = 0;

	switch (sensor->id) {
	case ST_ILPS22QS_QVAR:
	case ST_ILPS22QS_PRESS:
		ret = st_ilps22qs_set_interleave(sensor, enable);
		break;
	default:
		return 0;
	}

	return ret;
}

static int st_ilps22qs_set_enable(struct st_ilps22qs_sensor *sensor,
				  bool enable)
{
	struct st_ilps22qs_hw *hw = sensor->hw;
	u8 odr = enable ? sensor->odr : 0;
	int err;

	err = st_ilps22qs_hw_enable(sensor, enable);
	if (err < 0)
		return err;

	err = st_ilps22qs_set_odr(sensor, odr);
	if (err < 0)
		return err;

	mutex_lock(&hw->lock);
	if (enable) {
		ktime_t ktime = ktime_set(0, 1000000000 / sensor->odr);

		hrtimer_start(&sensor->hr_timer, ktime, HRTIMER_MODE_REL);
		sensor->ktime = ktime;
		hw->enable_mask |= BIT(sensor->id);
	} else {
		cancel_work_sync(&sensor->iio_work);
		hrtimer_cancel(&sensor->hr_timer);
		hw->enable_mask &= ~BIT(sensor->id);
	}

	if (!hw->interleave) {
		err = regmap_update_bits(hw->regmap,
					 ST_ILPS22QS_CTRL3_ADDR,
					 ST_ILPS22QS_AH_QVAR_EN_MASK,
					 ST_ILPS22QS_SHIFT_VAL(!!(hw->enable_mask & BIT(ST_ILPS22QS_QVAR)),
					  ST_ILPS22QS_AH_QVAR_EN_MASK));
	}
	mutex_unlock(&hw->lock);

	return err;
}

static int st_ilps22qs_init_sensors(struct st_ilps22qs_hw *hw)
{
	int err;

	/* soft reset the device on power on */
	err = st_ilps22qs_update_locked(hw, ST_ILPS22QS_CTRL2_ADDR,
					ST_ILPS22QS_SOFT_RESET_MASK, 1);
	if (err < 0)
		return err;

	usleep_range(50, 60);

	/* interleave disabled by default */
	hw->interleave = false;

	/* enable BDU */
	return st_ilps22qs_update_locked(hw, ST_ILPS22QS_CTRL1_ADDR,
					 ST_ILPS22QS_BDU_MASK, 1);
}

static ssize_t
st_ilps22qs_get_sampling_frequency_avail(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i, len = 0;

	for (i = 0; i < st_ilps22qs_odr_table.size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 st_ilps22qs_odr_table.odr_avl[i].hz);
	}

	buf[len - 1] = '\n';

	return len;
}

static int st_ilps22qs_read_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *ch,
				int *val, int *val2, long mask)
{
	struct st_ilps22qs_sensor *sensor = iio_priv(iio_dev);
	struct st_ilps22qs_hw *hw = sensor->hw;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		u8 data[4] = {};
		int delay;

		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			return ret;

		ret = st_ilps22qs_set_enable(sensor, true);
		if (ret < 0)
			goto read_error;

		delay = 1000000 / sensor->odr;
		usleep_range(delay, 2 * delay);

		ret = regmap_bulk_read(hw->regmap, ch->address, data,
				       ch->scan_type.realbits >> 3);
		if (ret < 0)
			goto read_error;

		switch (sensor->id) {
		case ST_ILPS22QS_PRESS:
			*val = (s32)get_unaligned_le32(data);
			break;
		case ST_ILPS22QS_TEMP:
			*val = (s16)get_unaligned_le16(data);
			break;
		case ST_ILPS22QS_QVAR:
			*val = (s32)get_unaligned_le32(data);
			break;
		default:
			ret = -ENODEV;
			goto read_error;
		}

read_error:
		st_ilps22qs_set_enable(sensor, false);
		iio_device_release_direct_mode(iio_dev);

		if (ret < 0)
			return ret;

		ret = IIO_VAL_INT;
		break;
	}
	case IIO_CHAN_INFO_SCALE:
		switch (ch->type) {
		case IIO_TEMP:
			*val = 1000;
			*val2 = sensor->gain;
			ret = IIO_VAL_FRACTIONAL;
			break;
		case IIO_PRESSURE:
			*val = 0;
			*val2 = sensor->gain;
			ret = IIO_VAL_INT_PLUS_NANO;
			break;
		case IIO_ALTVOLTAGE:
			*val = 0;
			*val2 = sensor->gain;
			ret = IIO_VAL_INT_PLUS_NANO;
			break;
		default:
			ret = -ENODEV;
			break;
		}
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

static int st_ilps22qs_write_raw(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *ch,
				 int val, int val2, long mask)
{
	struct st_ilps22qs_sensor *sensor = iio_priv(iio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = st_ilps22qs_get_odr(sensor, val);
		if (ret < 0)
			goto exit_fail;

		sensor->odr = st_ilps22qs_odr_table.odr_avl[ret].hz;
		break;
	default:
		ret = -EINVAL;
		break;
	}

exit_fail:
	iio_device_release_direct_mode(iio_dev);

	return ret < 0 ? ret : 0;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_ilps22qs_get_sampling_frequency_avail);

static struct attribute *st_ilps22qs_press_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_ilps22qs_press_attribute_group = {
	.attrs = st_ilps22qs_press_attributes,
};

static const struct iio_info st_ilps22qs_press_info = {
	.attrs = &st_ilps22qs_press_attribute_group,
	.read_raw = st_ilps22qs_read_raw,
	.write_raw = st_ilps22qs_write_raw,
	.debugfs_reg_access = st_ilps22qs_reg_access,
};

static struct attribute *st_ilps22qs_temp_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_ilps22qs_temp_attribute_group = {
	.attrs = st_ilps22qs_temp_attributes,
};

static const struct iio_info st_ilps22qs_temp_info = {
	.attrs = &st_ilps22qs_temp_attribute_group,
	.read_raw = st_ilps22qs_read_raw,
	.write_raw = st_ilps22qs_write_raw,
	.debugfs_reg_access = st_ilps22qs_reg_access,
};

static struct attribute *st_ilps22qs_qvar_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_ilps22qs_qvar_attribute_group = {
	.attrs = st_ilps22qs_qvar_attributes,
};

static const struct iio_info st_ilps22qs_qvar_info = {
	.attrs = &st_ilps22qs_qvar_attribute_group,
	.read_raw = st_ilps22qs_read_raw,
	.write_raw = st_ilps22qs_write_raw,
	.debugfs_reg_access = st_ilps22qs_reg_access,
};

static int st_ilps22qs_preenable(struct iio_dev *iio_dev)
{
	struct st_ilps22qs_sensor *sensor = iio_priv(iio_dev);

	return st_ilps22qs_set_enable(sensor, true);
}

static int st_ilps22qs_postdisable(struct iio_dev *iio_dev)
{
	struct st_ilps22qs_sensor *sensor = iio_priv(iio_dev);

	return st_ilps22qs_set_enable(sensor, false);
}

static const struct iio_buffer_setup_ops st_ilps22qs_fifo_ops = {
	.preenable = st_ilps22qs_preenable,
	.postdisable = st_ilps22qs_postdisable,
};

static void st_ilps22qs_disable_regulator_action(void *_data)
{
	struct st_ilps22qs_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static int st_ilps22qs_power_enable(struct st_ilps22qs_hw *hw)
{
	int err;

	hw->vdd_supply = devm_regulator_get(hw->dev, "vdd");
	if (IS_ERR(hw->vdd_supply)) {
		if (PTR_ERR(hw->vdd_supply) != -EPROBE_DEFER)
			dev_err(hw->dev, "Failed to get vdd regulator %d\n",
				(int)PTR_ERR(hw->vdd_supply));

		return PTR_ERR(hw->vdd_supply);
	}

	hw->vddio_supply = devm_regulator_get(hw->dev, "vddio");
	if (IS_ERR(hw->vddio_supply)) {
		if (PTR_ERR(hw->vddio_supply) != -EPROBE_DEFER)
			dev_err(hw->dev, "Failed to get vddio regulator %d\n",
				(int)PTR_ERR(hw->vddio_supply));

		return PTR_ERR(hw->vddio_supply);
	}

	err = regulator_enable(hw->vdd_supply);
	if (err) {
		dev_err(hw->dev, "Failed to enable vdd regulator: %d\n", err);

		return err;
	}

	err = regulator_enable(hw->vddio_supply);
	if (err) {
		regulator_disable(hw->vdd_supply);

		return err;
	}

	err = devm_add_action_or_reset(hw->dev,
				       st_ilps22qs_disable_regulator_action,
				       hw);
	if (err) {
		dev_err(hw->dev,
			"Failed to setup regulator cleanup action %d\n", err);

		return err;
	}

	/*
	 * after the device is powered up, the ILPS22QS performs a 10 ms
	 * boot procedure to load the trimming parameters
	 */
	usleep_range(10000, 11000);

	return 0;
}

static struct iio_dev *st_ilps22qs_alloc_iiodev(struct st_ilps22qs_hw *hw,
						enum st_ilps22qs_sensor_id id)
{
	struct st_ilps22qs_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->hw = hw;
	sensor->id = id;
	sensor->odr = st_ilps22qs_odr_table.odr_avl[0].hz;

	switch (id) {
	case ST_ILPS22QS_PRESS:
		sensor->gain = ST_ILPS22QS_PRESS_FS_AVL_GAIN;
		scnprintf(sensor->name, sizeof(sensor->name),
			  ST_ILPS22QS_DEV_NAME "_press");
		iio_dev->channels = st_ilps22qs_press_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_ilps22qs_press_channels);
		iio_dev->info = &st_ilps22qs_press_info;
		break;
	case ST_ILPS22QS_TEMP:
		sensor->gain = ST_ILPS22QS_TEMP_FS_AVL_GAIN;
		scnprintf(sensor->name, sizeof(sensor->name),
			  ST_ILPS22QS_DEV_NAME "_temp");
		iio_dev->channels = st_ilps22qs_temp_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_ilps22qs_temp_channels);
		iio_dev->info = &st_ilps22qs_temp_info;
		break;
	case ST_ILPS22QS_QVAR:
		sensor->gain = ST_ILPS22QS_QVAR_FS_AVL_GAIN;
		scnprintf(sensor->name, sizeof(sensor->name),
			  ST_ILPS22QS_DEV_NAME "_qvar");
		iio_dev->channels = st_ilps22qs_qvar_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_ilps22qs_qvar_channels);
		iio_dev->info = &st_ilps22qs_qvar_info;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	/* configure sensor hrtimer */
	hrtimer_init(&sensor->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sensor->hr_timer.function = &st_ilps22qs_poll_function_read;
	INIT_WORK(&sensor->iio_work, st_ilps22qs_poll_function_work);

	return iio_dev;
}

int st_ilps22qs_probe(struct device *dev, struct regmap *regmap)
{
	struct st_ilps22qs_hw *hw;
	int err, i;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	mutex_init(&hw->lock);

	dev_set_drvdata(dev, (void *)hw);
	hw->dev = dev;
	hw->regmap = regmap;

	err = st_ilps22qs_power_enable(hw);
	if (err)
		return err;

	err = st_ilps22qs_check_whoami(hw);
	if (err < 0)
		return err;

	err = st_ilps22qs_init_sensors(hw);
	if (err < 0)
		return err;

	for (i = 0; i < ST_ILPS22QS_SENSORS_NUM; i++) {

#if KERNEL_VERSION(5, 13, 0) > LINUX_VERSION_CODE
		struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */

		hw->iio_devs[i] = st_ilps22qs_alloc_iiodev(hw, i);
		if (!hw->iio_devs[i])
			return -ENOMEM;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev,
						  hw->iio_devs[i],
						  &st_ilps22qs_fifo_ops);
		if (err)
			return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  INDIO_BUFFER_SOFTWARE,
						  &st_ilps22qs_fifo_ops);
		if (err)
			return err;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[i], buffer);
		hw->iio_devs[i]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[i]->setup_ops = &st_ilps22qs_fifo_ops;
#endif /* LINUX_VERSION_CODE */

		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

	err = st_ilps22qs_allocate_workqueue(hw);
	if (err)
		return err;

	dev_info(dev, "device probed\n");

	return 0;
}
EXPORT_SYMBOL(st_ilps22qs_probe);

int st_ilps22qs_remove(struct device *dev)
{
	struct st_ilps22qs_hw *hw = dev_get_drvdata(dev);
	struct st_ilps22qs_sensor *sensor;
	int i;

	for (i = 0; i < ST_ILPS22QS_SENSORS_NUM; i++) {
		int err;

		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_ilps22qs_set_enable(sensor, false);
		if (err < 0)
			return err;
	}

	st_ilps22qs_flush_works(hw);
	st_ilps22qs_destroy_workqueue(hw);

	return 0;
}
EXPORT_SYMBOL(st_ilps22qs_remove);

static int __maybe_unused st_ilps22qs_suspend(struct device *dev)
{
	struct st_ilps22qs_hw *hw = dev_get_drvdata(dev);
	struct st_ilps22qs_sensor *sensor;
	int i;

	for (i = 0; i < ST_ILPS22QS_SENSORS_NUM; i++) {
		int err;

		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_ilps22qs_set_odr(sensor, 0);
		if (err < 0)
			return err;

		cancel_work_sync(&sensor->iio_work);
		hrtimer_cancel(&sensor->hr_timer);
	}

	dev_info(dev, "Suspending device\n");

	return 0;
}

static int __maybe_unused st_ilps22qs_resume(struct device *dev)
{
	struct st_ilps22qs_hw *hw = dev_get_drvdata(dev);
	struct st_ilps22qs_sensor *sensor;
	int i;

	dev_info(dev, "Resuming device\n");

	for (i = 0; i < ST_ILPS22QS_SENSORS_NUM; i++) {
		int err;

		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		err = st_ilps22qs_set_enable(sensor, true);
		if (err < 0)
			return err;
	}

	return 0;
}

const struct dev_pm_ops st_ilps22qs_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_ilps22qs_suspend, st_ilps22qs_resume)
};
EXPORT_SYMBOL(st_ilps22qs_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics ilps22qs driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 qvar sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/workqueue.h>

#include "st_lis2duxs12.h"

#define ST_LIS2DUXS12_REG_OUT_T_AH_QVAR_L_ADDR		0x2e

static const struct iio_chan_spec st_lis2duxs12_qvar_channels[] = {
	ST_LIS2DUXS12_DATA_CHANNEL(IIO_ALTVOLTAGE,
				   ST_LIS2DUXS12_REG_OUT_T_AH_QVAR_L_ADDR,
				   0, 0, 0, 12, 16, 's',
				   NULL),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static int st_lis2duxs12_qvar_init(struct st_lis2duxs12_hw *hw)
{
	/* impedance selection */
	return st_lis2duxs12_write_with_mask_locked(hw,
				   ST_LIS2DUXS12_AH_QVAR_CFG_ADDR,
				   ST_LIS2DUXS12_AH_QVAR_C_ZIN_MASK, 3);
}

static ssize_t
st_lis2duxs12_sysfs_qvar_sampling_freq_avail(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int len = 0;
	int i;

	/* qvar share the same XL odr table */
	for (i = 0; i < hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].size; i++) {
		if (!hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].hz)
			continue;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].hz,
				 hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static int
st_lis2duxs12_get_qvar_odr_val(struct st_lis2duxs12_hw *hw,
			       int odr, int uodr,
			       struct st_lis2duxs12_odr *oe)
{
	int req_odr = ST_LIS2DUXS12_ODR_EXPAND(odr, uodr);
	int sensor_odr;
	int i;

	for (i = 0; i < hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].size; i++) {
		sensor_odr = ST_LIS2DUXS12_ODR_EXPAND(
				hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].hz,
				hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].uhz);
		if (sensor_odr >= req_odr) {
			oe->hz = hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].hz;
			oe->uhz = hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].uhz;
			oe->val = hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].val;

			return 0;
		}
	}

	return -EINVAL;
}

static int
st_lis2duxs12_qvar_read_oneshot(struct st_lis2duxs12_sensor *sensor,
				u8 addr, int *val)
{
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int err, delay;
	__le16 data;

	err = st_lis2duxs12_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	delay = 1000000 / sensor->odr;
	usleep_range(delay, 2 * delay);
	err = st_lis2duxs12_read_locked(hw, addr, &data, sizeof(data));

	st_lis2duxs12_sensor_set_enable(sensor, false);
	if (err < 0)
		return err;

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_lis2duxs12_qvar_read_raw(struct iio_dev *iio_dev,
				       struct iio_chan_spec const *ch,
				       int *val, int *val2, long mask)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			return ret;

		ret = st_lis2duxs12_qvar_read_oneshot(sensor,
						      ch->address, val);
		iio_device_release_direct_mode(iio_dev);
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

static int
st_lis2duxs12_qvar_write_raw(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct st_lis2duxs12_odr oe = { 0 };

		err = st_lis2duxs12_get_qvar_odr_val(sensor->hw,
						     val, val2, &oe);
		if (!err) {
			sensor->odr = oe.hz;
			sensor->uodr = oe.uhz;
		}
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(iio_dev);

	return err;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lis2duxs12_sysfs_qvar_sampling_freq_avail);

static struct attribute *st_lis2duxs12_qvar_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lis2duxs12_qvar_attribute_group = {
	.attrs = st_lis2duxs12_qvar_attributes,
};

static const struct iio_info st_lis2duxs12_qvar_info = {
	.attrs = &st_lis2duxs12_qvar_attribute_group,
	.read_raw = st_lis2duxs12_qvar_read_raw,
	.write_raw = st_lis2duxs12_qvar_write_raw,
};

static const unsigned long st_lis2duxs12_qvar_available_scan_masks[] = {
	0x1, 0x0
};

int st_lis2duxs12_qvar_set_enable(struct st_lis2duxs12_sensor *sensor,
				  bool enable)
{
	int err;

	err = st_lis2duxs12_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	return st_lis2duxs12_write_with_mask_locked(sensor->hw,
				ST_LIS2DUXS12_AH_QVAR_CFG_ADDR,
				ST_LIS2DUXS12_AH_QVAR_EN_MASK,
				enable ? 1 : 0);
}

struct iio_dev *
st_lis2duxs12_alloc_qvar_iiodev(struct st_lis2duxs12_hw *hw)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = ST_LIS2DUXS12_ID_QVAR;
	sensor->hw = hw;

	iio_dev->channels = st_lis2duxs12_qvar_channels;
	iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_qvar_channels);
	iio_dev->name = "lis2duxs12_qvar";
	iio_dev->info = &st_lis2duxs12_qvar_info;
	iio_dev->available_scan_masks =
				st_lis2duxs12_qvar_available_scan_masks;

	sensor->odr = hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[0].hz;
	sensor->uodr = hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[0].uhz;
	sensor->gain = 1;
	sensor->watermark = 1;

	return iio_dev;
}

int st_lis2duxs12_qvar_probe(struct st_lis2duxs12_hw *hw)
{
	int err;

	hw->iio_devs[ST_LIS2DUXS12_ID_QVAR] =
				    st_lis2duxs12_alloc_qvar_iiodev(hw);
	if (!hw->iio_devs[ST_LIS2DUXS12_ID_QVAR])
		return -ENOMEM;

	err = st_lis2duxs12_qvar_init(hw);

	return err < 0 ? err : 0;
}

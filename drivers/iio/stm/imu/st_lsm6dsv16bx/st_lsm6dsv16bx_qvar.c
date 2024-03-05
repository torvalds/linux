// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsv16bx qvar sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
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

#include "st_lsm6dsv16bx.h"

static const struct st_lsm6dsv16bx_odr_table_entry
st_lsm6dsv16bx_qvar_odr_table = {
	.size = 1,
	.odr_avl[0] = { 240, 0, 0x00, 0x00 },
};

static const struct iio_chan_spec st_lsm6dsv16bx_qvar_channels[] = {
	{
		.type = IIO_ALTVOLTAGE,
		.address = ST_LSM6DSV16BX_REG_OUT_QVAR_ADDR,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		}
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static int st_lsm6dsv16bx_qvar_init(struct st_lsm6dsv16bx_hw *hw)
{
	int err;

	/* enable fifo batching by default */
	err = st_lsm6dsv16bx_write_with_mask(hw,
					   ST_LSM6DSV16BX_COUNTER_BDR_REG1_ADDR,
					   ST_LSM6DSV16BX_AH_QVAR_BATCH_EN_MASK,
					   1);
	if (err < 0)
		return err;

	/* impedance selection 235 Mohm */
	return st_lsm6dsv16bx_write_with_mask(hw,
					      ST_LSM6DSV16BX_REG_CTRL7_ADDR,
					      ST_LSM6DSV16BX_AH_QVAR_C_ZIN_MASK,
					      3);
}

static ssize_t
st_lsm6dsv16bx_sysfs_qvar_sampling_freq_avail(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	int len = 0;
	int i;

	for (i = 0; i < st_lsm6dsv16bx_qvar_odr_table.size; i++) {
		if (!st_lsm6dsv16bx_qvar_odr_table.odr_avl[i].hz)
			continue;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_lsm6dsv16bx_qvar_odr_table.odr_avl[i].hz,
				 st_lsm6dsv16bx_qvar_odr_table.odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lsm6dsv16bx_sysfs_qvar_sampling_freq_avail);
static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsv16bx_get_module_id, NULL, 0);

static struct attribute *st_lsm6dsv16bx_qvar_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group
st_lsm6dsv16bx_qvar_attribute_group = {
	.attrs = st_lsm6dsv16bx_qvar_attributes,
};

static const struct iio_info st_lsm6dsv16bx_qvar_info = {
	.attrs = &st_lsm6dsv16bx_qvar_attribute_group,
};

static const unsigned long st_lsm6dsv16bx_qvar_available_scan_masks[] = {
	BIT(0), 0x0
};

static int
_st_lsm6dsv16bx_qvar_sensor_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
				       bool enable)
{
	u16 odr = enable ? sensor->odr : 0;
	int err;

	err = st_lsm6dsv16bx_sensor_set_enable(sensor, odr);
	if (err < 0)
		return err;

	err = st_lsm6dsv16bx_write_with_mask(sensor->hw,
					     ST_LSM6DSV16BX_REG_CTRL7_ADDR,
					     ST_LSM6DSV16BX_AH_QVARx_EN_MASK,
					     enable ? 3 : 0);
	if (err < 0)
		return err;

	return st_lsm6dsv16bx_write_with_mask(sensor->hw,
					      ST_LSM6DSV16BX_REG_CTRL7_ADDR,
					      ST_LSM6DSV16BX_AH_QVAR_EN_MASK,
					      enable ? 1 : 0);
}

int st_lsm6dsv16bx_qvar_sensor_set_enable(struct st_lsm6dsv16bx_sensor *sensor,
					  bool enable)
{
	int err;

	err = _st_lsm6dsv16bx_qvar_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask |= BIT(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

static struct iio_dev *
st_lsm6dsv16bx_alloc_qvar_iiodev(struct st_lsm6dsv16bx_hw *hw)
{
	struct st_lsm6dsv16bx_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = ST_LSM6DSV16BX_ID_QVAR;
	sensor->hw = hw;

	iio_dev->channels = st_lsm6dsv16bx_qvar_channels;
	iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsv16bx_qvar_channels);
	scnprintf(sensor->name, sizeof(sensor->name),
		 "%s_qvar", hw->settings->id.name);
	iio_dev->info = &st_lsm6dsv16bx_qvar_info;
	iio_dev->available_scan_masks = st_lsm6dsv16bx_qvar_available_scan_masks;
	iio_dev->name = sensor->name;

	sensor->odr = st_lsm6dsv16bx_qvar_odr_table.odr_avl[0].hz;
	sensor->uodr = st_lsm6dsv16bx_qvar_odr_table.odr_avl[0].uhz;
	sensor->gain = 1;
	sensor->watermark = 1;

	return iio_dev;
}

int st_lsm6dsv16bx_qvar_probe(struct st_lsm6dsv16bx_hw *hw)
{
	int err;

	hw->iio_devs[ST_LSM6DSV16BX_ID_QVAR] = st_lsm6dsv16bx_alloc_qvar_iiodev(hw);
	if (!hw->iio_devs[ST_LSM6DSV16BX_ID_QVAR])
		return -ENOMEM;

	err = st_lsm6dsv16bx_qvar_init(hw);

	return err < 0 ? err : 0;
}

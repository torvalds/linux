// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsvx qvar sensor driver
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
#include <linux/version.h>

#include "st_lsm6dsvx.h"

#ifdef CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO
static const u8 qvar_fifo_config[][2] = {
	{ 0x05, 0x00 }, { 0x17, 0x40 }, { 0x02, 0x11 }, { 0x08, 0xE8 },
	{ 0x09, 0x00 }, { 0x09, 0x3C }, { 0x09, 0x96 }, { 0x09, 0x03 },
	{ 0x09, 0xB0 }, { 0x09, 0x03 }, { 0x09, 0x02 }, { 0x09, 0x00 },
	{ 0x09, 0x14 }, { 0x02, 0x11 }, { 0x08, 0xF2 }, { 0x09, 0xFF },
	{ 0x02, 0x11 }, { 0x08, 0xFA }, { 0x09, 0x80 }, { 0x09, 0x03 },
	{ 0x09, 0xB2 }, { 0x09, 0x03 }, { 0x09, 0xBE }, { 0x09, 0x03 },
	{ 0x02, 0x31 }, { 0x08, 0x80 }, { 0x09, 0xA8 }, { 0x09, 0x00 },
	{ 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 },
	{ 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x3C },
	{ 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 },
	{ 0x09, 0x3F }, { 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x2C },
	{ 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x1F }, { 0x09, 0x00 },
	{ 0x02, 0x31 }, { 0x08, 0xB2 }, { 0x09, 0x00 }, { 0x09, 0x00 },
	{ 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 },
	{ 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 }, { 0x09, 0x00 },
	{ 0x09, 0x00 }, { 0x08, 0xBE }, { 0x09, 0x00 }, { 0x09, 0x00 },
	{ 0x09, 0x40 }, { 0x09, 0xE0 }, { 0x17, 0x00 }, { 0x04, 0x00 },
	{ 0x05, 0x10 }, { 0x02, 0x01 }, { 0x60, 0x45 }, { 0x45, 0x02 },
};
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO */

static const struct st_lsm6dsvx_odr_table_entry
st_lsm6dsvx_qvar_odr_table = {
	.size = 1,
	.odr_avl[0] = { 240, 0, 0x00, 0x00 },
};

static const struct iio_chan_spec st_lsm6dsvx_qvar_channels[] = {
	{
		.type = IIO_ALTVOLTAGE,
		.address = ST_LSM6DSVX_REG_OUT_QVAR_ADDR,
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

static int st_lsm6dsvx_qvar_init(struct st_lsm6dsvx_hw *hw)
{

#ifdef CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO
	uint8_t i;
	int err;

	mutex_lock(&hw->page_lock);
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 1);
	for (i = 0; i < ARRAY_SIZE(qvar_fifo_config); i++) {
		err = regmap_write(hw->regmap, qvar_fifo_config[i][0],
				   qvar_fifo_config[i][1]);
		if (err < 0) {
			st_lsm6dsvx_set_page_access(hw,
				   ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);
			mutex_unlock(&hw->page_lock);
			dev_err(hw->dev, "failed to configure qvar\n");

			return err;
		}
	}
	st_lsm6dsvx_set_page_access(hw,
				   ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);
	mutex_unlock(&hw->page_lock);
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO */

	/* impedance selection */
	return st_lsm6dsvx_write_with_mask(hw,
					   ST_LSM6DSVX_REG_CTRL7_ADDR,
					   ST_LSM6DSVX_AH_QVAR_C_ZIN_MASK, 3);
}

static ssize_t
st_lsm6dsvx_sysfs_qvar_sampling_freq_avail(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	int len = 0;
	int i;

	for (i = 0; i < st_lsm6dsvx_qvar_odr_table.size; i++) {
		if (!st_lsm6dsvx_qvar_odr_table.odr_avl[i].hz)
			continue;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 st_lsm6dsvx_qvar_odr_table.odr_avl[i].hz,
				 st_lsm6dsvx_qvar_odr_table.odr_avl[i].uhz);
	}

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lsm6dsvx_sysfs_qvar_sampling_freq_avail);
static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsvx_get_module_id, NULL, 0);

static struct attribute *st_lsm6dsvx_qvar_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group
st_lsm6dsvx_qvar_attribute_group = {
	.attrs = st_lsm6dsvx_qvar_attributes,
};

static const struct iio_info st_lsm6dsvx_qvar_info = {
	.attrs = &st_lsm6dsvx_qvar_attribute_group,
};

static const unsigned long st_lsm6dsvx_qvar_available_scan_masks[] = {
	BIT(0), 0x0
};

static int
_st_lsm6dsvx_qvar_sensor_set_enable(struct st_lsm6dsvx_sensor *sensor,
				    bool enable)
{
	u16 odr = enable ? sensor->odr : 0;
	int err;

	err = st_lsm6dsvx_sensor_set_enable(sensor, odr);
	if (err < 0)
		return err;

#ifndef CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO
	if (enable) {
		int64_t newTime;

		newTime = 1000000000 / odr;
		sensor->oldktime = ktime_set(0, newTime);
		hrtimer_start(&sensor->hr_timer, sensor->oldktime,
			      HRTIMER_MODE_REL);
	} else {
		cancel_work_sync(&sensor->iio_work);
		hrtimer_cancel(&sensor->hr_timer);
	}
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO */

	return st_lsm6dsvx_write_with_mask(sensor->hw,
					   ST_LSM6DSVX_REG_CTRL7_ADDR,
					   ST_LSM6DSVX_AH_QVAR_EN_MASK,
					   enable ? 1 : 0);
}

int
st_lsm6dsvx_qvar_sensor_set_enable(struct st_lsm6dsvx_sensor *sensor,
				   bool enable)
{
	int err;

	err = _st_lsm6dsvx_qvar_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask |= BIT(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

#ifndef CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO
static inline void st_lsm6dsvx_flush_works(struct st_lsm6dsvx_hw *hw)
{
	flush_workqueue(hw->qvar_workqueue);
}

static int st_lsm6dsvx_allocate_workqueue(struct st_lsm6dsvx_hw *hw)
{
	if (!hw->qvar_workqueue)
		hw->qvar_workqueue =
		      create_workqueue(hw->iio_devs[ST_LSM6DSVX_ID_QVAR]->name);

	if (!hw->qvar_workqueue)
		return -ENOMEM;

	return 0;
}

static enum hrtimer_restart
st_lsm6dsvx_qvar_poll_function_read(struct hrtimer *timer)
{
	struct st_lsm6dsvx_sensor *sensor;

	sensor = container_of((struct hrtimer *)timer,
			      struct st_lsm6dsvx_sensor, hr_timer);

	sensor->timestamp =
		iio_get_time_ns(sensor->hw->iio_devs[ST_LSM6DSVX_ID_QVAR]);
	queue_work(sensor->hw->qvar_workqueue, &sensor->iio_work);

	return HRTIMER_NORESTART;
}

static void
st_lsm6dsvx_report_1axes_event(struct st_lsm6dsvx_sensor *sensor,
			       u8 *tmp, int64_t timestamp)
{
	struct iio_dev *iio_dev = sensor->hw->iio_devs[sensor->id];
	u8 iio_buf[ALIGN(ST_LSM6DSVX_SAMPLE_SIZE, sizeof(s64)) + sizeof(s64)];

	memcpy(iio_buf, tmp, ST_LSM6DSVX_SAMPLE_SIZE);
	iio_push_to_buffers_with_timestamp(iio_dev, iio_buf, timestamp);
}

static int
st_lsm6dsvx_get_qvar_poll_data(struct st_lsm6dsvx_sensor *sensor,
			       u8 *data)
{
	return st_lsm6dsvx_read_locked(sensor->hw,
				       ST_LSM6DSVX_REG_OUT_QVAR_ADDR, data, 2);
}

static void
st_lsm6dsvx_qvar_poll_function_work(struct work_struct *iio_work)
{
	struct st_lsm6dsvx_sensor *sensor;
	u8 data[2];
	int err;

	sensor = container_of((struct work_struct *)iio_work,
			      struct st_lsm6dsvx_sensor, iio_work);

	hrtimer_start(&sensor->hr_timer, sensor->oldktime, HRTIMER_MODE_REL);
	err = st_lsm6dsvx_get_qvar_poll_data(sensor, data);
	if (err < 0)
		return;

	st_lsm6dsvx_report_1axes_event(sensor, data, sensor->timestamp);
}

static int st_lsm6dsvx_qvar_preenable(struct iio_dev *iio_dev)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);

	return st_lsm6dsvx_qvar_sensor_set_enable(sensor, true);
}

static int st_lsm6dsvx_qvar_postdisable(struct iio_dev *iio_dev)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);

	return st_lsm6dsvx_qvar_sensor_set_enable(sensor, false);
}

static const struct iio_buffer_setup_ops st_lsm6dsvx_qvar_ops = {
	.preenable = st_lsm6dsvx_qvar_preenable,
	.postdisable = st_lsm6dsvx_qvar_postdisable,
};

static int st_lsm6dsvx_qvar_buffer(struct st_lsm6dsvx_hw *hw)
{
	struct iio_buffer *buffer;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	err = devm_iio_kfifo_buffer_setup(hw->dev,
					  hw->iio_devs[ST_LSM6DSVX_ID_QVAR],
					  &st_lsm6dsvx_qvar_ops);
	if (err)
		return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	err = devm_iio_kfifo_buffer_setup(hw->dev,
					  hw->iio_devs[ST_LSM6DSVX_ID_QVAR],
					  INDIO_BUFFER_SOFTWARE,
					  &st_lsm6dsvx_qvar_ops);
	if (err)
		return err;
#else /* LINUX_VERSION_CODE */
	buffer = devm_iio_kfifo_allocate(hw->dev);
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(hw->iio_devs[ST_LSM6DSVX_ID_QVAR], buffer);
	hw->iio_devs[ST_LSM6DSVX_ID_QVAR]->modes |= INDIO_BUFFER_SOFTWARE;
	hw->iio_devs[ST_LSM6DSVX_ID_QVAR]->setup_ops = &st_lsm6dsvx_qvar_ops;
#endif /* LINUX_VERSION_CODE */

	return st_lsm6dsvx_allocate_workqueue(hw);
}
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO */

static struct iio_dev *
st_lsm6dsvx_alloc_qvar_iiodev(struct st_lsm6dsvx_hw *hw)
{
	struct st_lsm6dsvx_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = ST_LSM6DSVX_ID_QVAR;
	sensor->hw = hw;

	iio_dev->channels = st_lsm6dsvx_qvar_channels;
	iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_qvar_channels);
	scnprintf(sensor->name, sizeof(sensor->name),
		 "%s_qvar", hw->settings->id.name);
	iio_dev->info = &st_lsm6dsvx_qvar_info;
	iio_dev->available_scan_masks = st_lsm6dsvx_qvar_available_scan_masks;
	iio_dev->name = sensor->name;

	sensor->odr = st_lsm6dsvx_qvar_odr_table.odr_avl[0].hz;
	sensor->uodr = st_lsm6dsvx_qvar_odr_table.odr_avl[0].uhz;
	sensor->gain = 1;
	sensor->watermark = 1;

#ifndef CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO
	/* configure hrtimer */
	hrtimer_init(&sensor->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sensor->hr_timer.function = &st_lsm6dsvx_qvar_poll_function_read;

	sensor->oldktime = ktime_set(0, 1000000000 / sensor->odr);
	INIT_WORK(&sensor->iio_work, st_lsm6dsvx_qvar_poll_function_work);
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO */

	return iio_dev;
}


int st_lsm6dsvx_qvar_probe(struct st_lsm6dsvx_hw *hw)
{
	int err;

	hw->iio_devs[ST_LSM6DSVX_ID_QVAR] = st_lsm6dsvx_alloc_qvar_iiodev(hw);
	if (!hw->iio_devs[ST_LSM6DSVX_ID_QVAR])
		return -ENOMEM;

#ifndef CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO
	/* if qvar not in FIFO the st_lsm6dsvx_qvar_ops is local */
	err = st_lsm6dsvx_qvar_buffer(hw);
	if (err)
		return err;
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO */

	err = st_lsm6dsvx_qvar_init(hw);

	return err < 0 ? err : 0;
}

int st_lsm6dsvx_qvar_remove(struct device *dev)
{

#ifndef CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO
	struct st_lsm6dsvx_hw *hw = dev_get_drvdata(dev);

	st_lsm6dsvx_flush_works(hw);
	destroy_workqueue(hw->qvar_workqueue);
	hw->qvar_workqueue = NULL;
#endif /* CONFIG_IIO_ST_LSM6DSVX_QVAR_IN_FIFO */

	return 0;
}

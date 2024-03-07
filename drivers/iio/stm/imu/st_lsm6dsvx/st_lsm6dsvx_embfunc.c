// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsvx embedded function sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "st_lsm6dsvx.h"

/**
 * Step Counter IIO channels description
 *
 * Step Counter exports to IIO framework the following data channels:
 * Step Counters (16 bit unsigned in little endian)
 * Timestamp (64 bit signed in little endian)
 * Step Counter exports to IIO framework the following event channels:
 * Flush event done
 */
static const struct iio_chan_spec st_lsm6dsvx_step_counter_channels[] = {
	{
		.type = STM_IIO_STEP_COUNTER,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	ST_LSM6DSVX_EVENT_CHANNEL(STM_IIO_STEP_COUNTER, flush),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

/**
 * Step Detector IIO channels description
 *
 * Step Detector exports to IIO framework the following event channels:
 * Step detection event detection
 */
static const struct iio_chan_spec st_lsm6dsvx_step_detector_channels[] = {
	ST_LSM6DSVX_EVENT_CHANNEL(IIO_STEPS, thr),
};

/**
 * Significant Motion IIO channels description
 *
 * Significant Motion exports to IIO framework the following event
 * channels:
 * Significant Motion event detection
 */
static const struct iio_chan_spec st_lsm6dsvx_sign_motion_channels[] = {
	ST_LSM6DSVX_EVENT_CHANNEL(STM_IIO_SIGN_MOTION, thr),
};

/**
 * Tilt IIO channels description
 *
 * Tilt exports to IIO framework the following event channels:
 * Tilt event detection
 */
static const struct iio_chan_spec st_lsm6dsvx_tilt_channels[] = {
	ST_LSM6DSVX_EVENT_CHANNEL(STM_IIO_TILT, thr),
};

static const unsigned long st_lsm6dsvx_embfunc_available_scan_masks[] = {
	BIT(0), 0x0
};

static int
st_lsm6dsvx_embfunc_set_enable(struct st_lsm6dsvx_sensor *sensor,
			       u8 mask, u8 irq_mask, bool enable)
{
	struct st_lsm6dsvx_hw *hw = sensor->hw;
	u8 int_reg = hw->int_pin == 1 ? ST_LSM6DSVX_REG_EMB_FUNC_INT1_ADDR :
					ST_LSM6DSVX_REG_EMB_FUNC_INT2_ADDR;
	int err;

	err = st_lsm6dsvx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	mutex_lock(&hw->page_lock);
	err = st_lsm6dsvx_set_page_access(hw,
					  ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK,
					  1);
	if (err < 0)
		goto unlock;

	err = __st_lsm6dsvx_write_with_mask(hw,
				     ST_LSM6DSVX_REG_EMB_FUNC_EN_A_ADDR,
				     mask,
				     enable ? 1 : 0);
	if (err < 0)
		goto reset_page;

	err = __st_lsm6dsvx_write_with_mask(hw, int_reg, irq_mask,
					    enable ? 1 : 0);

reset_page:
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);

unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * st_lsm6dsvx_embfunc_sensor_set_enable() - Enable Embedded Function
 *					     sensor [EMB_FUN]
 *
 * @sensor: ST IMU sensor instance
 * @enable: Enable/Disable sensor
 *
 * return < 0 if error, 0 otherwise
 */
static int
st_lsm6dsvx_embfunc_sensor_set_enable(struct st_lsm6dsvx_sensor *sensor,
				      bool enable)
{
	int err;

	switch (sensor->id) {
	case ST_LSM6DSVX_ID_STEP_DETECTOR:
		err = st_lsm6dsvx_embfunc_set_enable(sensor,
					ST_LSM6DSVX_REG_PEDO_EN_MASK,
					ST_LSM6DSVX_INT_STEP_DETECTOR_MASK,
					enable);
		break;
	case ST_LSM6DSVX_ID_SIGN_MOTION:
		err = st_lsm6dsvx_embfunc_set_enable(sensor,
					ST_LSM6DSVX_REG_SIGN_MOTION_EN_MASK,
					ST_LSM6DSVX_INT_SIG_MOT_MASK,
					enable);
		break;
	case ST_LSM6DSVX_ID_TILT:
		err = st_lsm6dsvx_embfunc_set_enable(sensor,
						ST_LSM6DSVX_REG_TILT_EN_MASK,
						ST_LSM6DSVX_INT_TILT_MASK,
						enable);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

/**
 * st_lsm6dsvx_reset_step_counter() - Reset Step Counter value [EMB_FUN]
 *
 * @iio_dev: IIO device
 *
 * return < 0 if error, 0 otherwise
 */
static int st_lsm6dsvx_reset_step_counter(struct iio_dev *iio_dev)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsvx_hw *hw = sensor->hw;
	int err;

	mutex_lock(&hw->page_lock);
	err = st_lsm6dsvx_set_page_access(hw,
					  ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK,
					  1);
	if (err < 0)
		goto unlock_page;

	err = __st_lsm6dsvx_write_with_mask(hw,
				     ST_LSM6DSVX_REG_EMB_FUNC_SRC_ADDR,
				     ST_LSM6DSVX_PEDO_RST_STEP_MASK, 1);
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);

unlock_page:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * st_lsm6dsvx_read_embfunc_config() - Read embedded function sensor
 *				       event configuration
 *
 * @iio_dev: IIO Device.
 * @chan: IIO Channel.
 * @type: Event Type.
 * @dir: Event Direction.
 *
 * return 1 if Enabled, 0 Disabled
 */
static int
st_lsm6dsvx_read_embfunc_config(struct iio_dev *iio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsvx_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

/**
 * st_lsm6dsvx_write_embfunc_config() - Write embedded function
 *					sensor event configuration
 *
 * @iio_dev: IIO Device.
 * @chan: IIO Channel.
 * @type: Event Type.
 * @dir: Event Direction.
 * @state: New event state.
 *
 * return 0 if OK, negative for ERROR
 */
static int
st_lsm6dsvx_write_embfunc_config(struct iio_dev *iio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 int state)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = st_lsm6dsvx_embfunc_sensor_set_enable(sensor, state);
	iio_device_release_direct_mode(iio_dev);

	return err;
}

/**
 * st_lsm6dsvx_sysfs_reset_step_counter() - Reset step counter value
 *
 * @dev: IIO Device.
 * @attr: IIO Channel attribute.
 * @buf: User buffer.
 * @size: User buffer size.
 *
 * return buffer len, negative for ERROR
 */
static ssize_t
st_lsm6dsvx_sysfs_reset_step_counter(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = st_lsm6dsvx_reset_step_counter(iio_dev);
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

static IIO_DEVICE_ATTR(reset_stepc, 0200, NULL,
		       st_lsm6dsvx_sysfs_reset_step_counter, 0);

static IIO_DEVICE_ATTR(hwfifo_stepc_watermark_max, 0444,
		       st_lsm6dsvx_get_max_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_stepc_flush, 0200, NULL,
		       st_lsm6dsvx_flush_fifo, 0);
static IIO_DEVICE_ATTR(hwfifo_stepc_watermark, 0644,
		       st_lsm6dsvx_get_watermark,
		       st_lsm6dsvx_set_watermark, 0);
static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsvx_get_module_id, NULL, 0);

static struct attribute *st_lsm6dsvx_step_counter_attributes[] = {
	&iio_dev_attr_hwfifo_stepc_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_stepc_watermark.dev_attr.attr,
	&iio_dev_attr_reset_stepc.dev_attr.attr,
	&iio_dev_attr_hwfifo_stepc_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_step_counter_attribute_group = {
	.attrs = st_lsm6dsvx_step_counter_attributes,
};

static const struct iio_info st_lsm6dsvx_step_counter_info = {
	.attrs = &st_lsm6dsvx_step_counter_attribute_group,
};

static struct attribute *st_lsm6dsvx_step_detector_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_step_detector_attribute_group = {
	.attrs = st_lsm6dsvx_step_detector_attributes,
};

static const struct iio_info st_lsm6dsvx_step_detector_info = {
	.attrs = &st_lsm6dsvx_step_detector_attribute_group,
	.read_event_config = st_lsm6dsvx_read_embfunc_config,
	.write_event_config = st_lsm6dsvx_write_embfunc_config,
};

static struct attribute *st_lsm6dsvx_sign_motion_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_sign_motion_attribute_group = {
	.attrs = st_lsm6dsvx_sign_motion_attributes,
};

static const struct iio_info st_lsm6dsvx_sign_motion_info = {
	.attrs = &st_lsm6dsvx_sign_motion_attribute_group,
	.read_event_config = st_lsm6dsvx_read_embfunc_config,
	.write_event_config = st_lsm6dsvx_write_embfunc_config,
};

static struct attribute *st_lsm6dsvx_tilt_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsvx_tilt_attribute_group = {
	.attrs = st_lsm6dsvx_tilt_attributes,
};

static const struct iio_info st_lsm6dsvx_tilt_info = {
	.attrs = &st_lsm6dsvx_tilt_attribute_group,
	.read_event_config = st_lsm6dsvx_read_embfunc_config,
	.write_event_config = st_lsm6dsvx_write_embfunc_config,
};

static int st_lsm6dsvx_embfunc_init(struct st_lsm6dsvx_hw *hw)
{
	u8 int_reg = hw->int_pin == 1 ? ST_LSM6DSVX_REG_MD1_CFG_ADDR :
					ST_LSM6DSVX_REG_MD2_CFG_ADDR;
	int err;

	mutex_lock(&hw->page_lock);
	err = st_lsm6dsvx_set_page_access(hw,
					  ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK,
					  1);
	if (err < 0)
		goto unlock_page;

	/* enable embedded function latched interrupt */
	err = __st_lsm6dsvx_write_with_mask(hw,
				     ST_LSM6DSVX_REG_PAGE_RW_ADDR,
				     ST_LSM6DSVX_EMB_FUNC_LIR_MASK, 1);
	if (err < 0)
		goto unlock_page;

	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);

	/* enable embedded function interrupt by default */
	err = __st_lsm6dsvx_write_with_mask(hw, int_reg,
					    ST_LSM6DSVX_REG_INT_EMB_FUNC_MASK,
					    1);
unlock_page:
	mutex_unlock(&hw->page_lock);

	return err;
}

static struct iio_dev *
st_lsm6dsvx_alloc_embfunc_iiodev(struct st_lsm6dsvx_hw *hw,
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
	iio_dev->available_scan_masks = st_lsm6dsvx_embfunc_available_scan_masks;

	/* set main sensor odr to 26 Hz */
	sensor->odr = hw->odr_table[ST_LSM6DSVX_ID_ACC].odr_avl[2].hz;
	switch (id) {
	case ST_LSM6DSVX_ID_STEP_COUNTER:
		iio_dev->channels = st_lsm6dsvx_step_counter_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_step_counter_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_stepc", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_step_counter_info;
		break;
	case ST_LSM6DSVX_ID_STEP_DETECTOR:
		iio_dev->channels = st_lsm6dsvx_step_detector_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_step_detector_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_stepd", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_step_detector_info;
		break;
	case ST_LSM6DSVX_ID_SIGN_MOTION:
		iio_dev->channels = st_lsm6dsvx_sign_motion_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_sign_motion_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_sigmot", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_sign_motion_info;
		break;
	case ST_LSM6DSVX_ID_TILT:
		iio_dev->channels = st_lsm6dsvx_tilt_channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_tilt_channels);
		scnprintf(sensor->name, sizeof(sensor->name),
			 "%s_tilt", hw->settings->id.name);
		iio_dev->info = &st_lsm6dsvx_tilt_info;
		break;
	default:
		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

/**
 * st_lsm6dsvx_step_counter_set_enable() - Enable Step Counter
 *					   Sensor [EMB_FUN]
 *
 * @sensor: ST IMU sensor instance
 * @enable: Enable/Disable sensor
 *
 * return < 0 if error, 0 otherwise
 */
int st_lsm6dsvx_step_counter_set_enable(struct st_lsm6dsvx_sensor *sensor,
					bool enable)
{
	struct st_lsm6dsvx_hw *hw = sensor->hw;
	int err;

	err = st_lsm6dsvx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	mutex_lock(&hw->page_lock);
	err = st_lsm6dsvx_set_page_access(hw,
					  ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK,
					  1);
	if (err < 0)
		goto unlock;

	err = __st_lsm6dsvx_write_with_mask(hw,
				     ST_LSM6DSVX_REG_EMB_FUNC_EN_A_ADDR,
				     ST_LSM6DSVX_REG_PEDO_EN_MASK,
				     enable);
	if (err < 0)
		goto reset_page;

	/* enable step counter batching in fifo */
	err = __st_lsm6dsvx_write_with_mask(hw,
				 ST_LSM6DSVX_REG_EMB_FUNC_FIFO_EN_A_ADDR,
				 ST_LSM6DSVX_STEP_COUNTER_FIFO_EN_MASK,
				 enable);

reset_page:
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);
unlock:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * st_lsm6dsvx_embfunc_handler_thread() - Bottom handler for embedded
 *					  function event detection
 *
 * @hw: ST IMU MEMS hw instance.
 *
 * return IRQ_HANDLED or < 0 for error
 */
int st_lsm6dsvx_embfunc_handler_thread(struct st_lsm6dsvx_hw *hw)
{
	if (hw->enable_mask & (BIT(ST_LSM6DSVX_ID_STEP_DETECTOR) |
			       BIT(ST_LSM6DSVX_ID_SIGN_MOTION) |
			       BIT(ST_LSM6DSVX_ID_TILT))) {
		struct iio_dev *iio_dev;
		u8 status;
		s64 event;
		int err;

		err = st_lsm6dsvx_read_locked(hw,
				  ST_LSM6DSVX_REG_EMB_FUNC_STATUS_MAINPAGE_ADDR,
				  &status, sizeof(status));
		if (err < 0)
			return IRQ_HANDLED;

		/* embedded function sensors */
		if (status & ST_LSM6DSVX_IS_STEP_DET_MASK) {
			iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_STEP_DETECTOR];
			event = IIO_UNMOD_EVENT_CODE(IIO_STEPS,
						    -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}

		if (status & ST_LSM6DSVX_IS_SIGMOT_MASK) {
			iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_SIGN_MOTION];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_SIGN_MOTION,
						    -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}

		if (status & ST_LSM6DSVX_IS_TILT_MASK) {
			iio_dev = hw->iio_devs[ST_LSM6DSVX_ID_TILT];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_TILT, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}
	}

	return IRQ_HANDLED;
}

/**
 * st_lsm6dsvx_probe_embfunc() - Allocate IIO embedded function device
 *
 * @hw: ST IMU MEMS hw instance.
 *
 * return 0 or < 0 for error
 */
int st_lsm6dsvx_probe_embfunc(struct st_lsm6dsvx_hw *hw)
{
	enum st_lsm6dsvx_sensor_id id;
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsvx_embfunc_sensor_list);
	     i++) {

		id = st_lsm6dsvx_embfunc_sensor_list[i];
		hw->iio_devs[id] = st_lsm6dsvx_alloc_embfunc_iiodev(hw,
								    id);
		if (!hw->iio_devs[id])
			return -ENOMEM;
	}

	return st_lsm6dsvx_embfunc_init(hw);
}

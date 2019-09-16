// SPDX-License-Identifier: GPL-2.0
/*
 * cros_ec_sensors_core - Common function for Chrome OS EC sensor driver.
 *
 * Copyright (C) 2016 Google, Inc
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/common/cros_ec_sensors_core.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/kernel.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

static char *cros_ec_loc[] = {
	[MOTIONSENSE_LOC_BASE] = "base",
	[MOTIONSENSE_LOC_LID] = "lid",
	[MOTIONSENSE_LOC_MAX] = "unknown",
};

int cros_ec_sensors_core_init(struct platform_device *pdev,
			      struct iio_dev *indio_dev,
			      bool physical_device)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_sensors_core_state *state = iio_priv(indio_dev);
	struct cros_ec_dev *ec = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_sensor_platform *sensor_platform = dev_get_platdata(dev);

	platform_set_drvdata(pdev, indio_dev);

	state->ec = ec->ec_dev;
	state->msg = devm_kzalloc(&pdev->dev,
				max((u16)sizeof(struct ec_params_motion_sense),
				state->ec->max_response), GFP_KERNEL);
	if (!state->msg)
		return -ENOMEM;

	state->resp = (struct ec_response_motion_sense *)state->msg->data;

	mutex_init(&state->cmd_lock);

	/* Set up the host command structure. */
	state->msg->version = 2;
	state->msg->command = EC_CMD_MOTION_SENSE_CMD + ec->cmd_offset;
	state->msg->outsize = sizeof(struct ec_params_motion_sense);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = pdev->name;

	if (physical_device) {
		indio_dev->modes = INDIO_DIRECT_MODE;

		state->param.cmd = MOTIONSENSE_CMD_INFO;
		state->param.info.sensor_num = sensor_platform->sensor_num;
		if (cros_ec_motion_send_host_cmd(state, 0)) {
			dev_warn(dev, "Can not access sensor info\n");
			return -EIO;
		}
		state->type = state->resp->info.type;
		state->loc = state->resp->info.location;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cros_ec_sensors_core_init);

int cros_ec_motion_send_host_cmd(struct cros_ec_sensors_core_state *state,
				 u16 opt_length)
{
	int ret;

	if (opt_length)
		state->msg->insize = min(opt_length, state->ec->max_response);
	else
		state->msg->insize = state->ec->max_response;

	memcpy(state->msg->data, &state->param, sizeof(state->param));

	ret = cros_ec_cmd_xfer_status(state->ec, state->msg);
	if (ret < 0)
		return -EIO;

	if (ret &&
	    state->resp != (struct ec_response_motion_sense *)state->msg->data)
		memcpy(state->resp, state->msg->data, ret);

	return 0;
}
EXPORT_SYMBOL_GPL(cros_ec_motion_send_host_cmd);

static ssize_t cros_ec_sensors_calibrate(struct iio_dev *indio_dev,
		uintptr_t private, const struct iio_chan_spec *chan,
		const char *buf, size_t len)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	int ret, i;
	bool calibrate;

	ret = strtobool(buf, &calibrate);
	if (ret < 0)
		return ret;
	if (!calibrate)
		return -EINVAL;

	mutex_lock(&st->cmd_lock);
	st->param.cmd = MOTIONSENSE_CMD_PERFORM_CALIB;
	ret = cros_ec_motion_send_host_cmd(st, 0);
	if (ret != 0) {
		dev_warn(&indio_dev->dev, "Unable to calibrate sensor\n");
	} else {
		/* Save values */
		for (i = CROS_EC_SENSOR_X; i < CROS_EC_SENSOR_MAX_AXIS; i++)
			st->calib[i] = st->resp->perform_calib.offset[i];
	}
	mutex_unlock(&st->cmd_lock);

	return ret ? ret : len;
}

static ssize_t cros_ec_sensors_id(struct iio_dev *indio_dev,
				  uintptr_t private,
				  const struct iio_chan_spec *chan, char *buf)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", st->param.info.sensor_num);
}

static ssize_t cros_ec_sensors_loc(struct iio_dev *indio_dev,
		uintptr_t private, const struct iio_chan_spec *chan,
		char *buf)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", cros_ec_loc[st->loc]);
}

const struct iio_chan_spec_ext_info cros_ec_sensors_ext_info[] = {
	{
		.name = "calibrate",
		.shared = IIO_SHARED_BY_ALL,
		.write = cros_ec_sensors_calibrate
	},
	{
		.name = "id",
		.shared = IIO_SHARED_BY_ALL,
		.read = cros_ec_sensors_id
	},
	{
		.name = "location",
		.shared = IIO_SHARED_BY_ALL,
		.read = cros_ec_sensors_loc
	},
	{ },
};
EXPORT_SYMBOL_GPL(cros_ec_sensors_ext_info);

/**
 * cros_ec_sensors_idx_to_reg - convert index into offset in shared memory
 * @st:		pointer to state information for device
 * @idx:	sensor index (should be element of enum sensor_index)
 *
 * Return:	address to read at
 */
static unsigned int cros_ec_sensors_idx_to_reg(
					struct cros_ec_sensors_core_state *st,
					unsigned int idx)
{
	/*
	 * When using LPC interface, only space for 2 Accel and one Gyro.
	 * First halfword of MOTIONSENSE_TYPE_ACCEL is used by angle.
	 */
	if (st->type == MOTIONSENSE_TYPE_ACCEL)
		return EC_MEMMAP_ACC_DATA + sizeof(u16) *
			(1 + idx + st->param.info.sensor_num *
			 CROS_EC_SENSOR_MAX_AXIS);

	return EC_MEMMAP_GYRO_DATA + sizeof(u16) * idx;
}

static int cros_ec_sensors_cmd_read_u8(struct cros_ec_device *ec,
				       unsigned int offset, u8 *dest)
{
	return ec->cmd_readmem(ec, offset, 1, dest);
}

static int cros_ec_sensors_cmd_read_u16(struct cros_ec_device *ec,
					 unsigned int offset, u16 *dest)
{
	__le16 tmp;
	int ret = ec->cmd_readmem(ec, offset, 2, &tmp);

	if (ret >= 0)
		*dest = le16_to_cpu(tmp);

	return ret;
}

/**
 * cros_ec_sensors_read_until_not_busy() - read until is not busy
 *
 * @st:	pointer to state information for device
 *
 * Read from EC status byte until it reads not busy.
 * Return: 8-bit status if ok, -errno on failure.
 */
static int cros_ec_sensors_read_until_not_busy(
					struct cros_ec_sensors_core_state *st)
{
	struct cros_ec_device *ec = st->ec;
	u8 status;
	int ret, attempts = 0;

	ret = cros_ec_sensors_cmd_read_u8(ec, EC_MEMMAP_ACC_STATUS, &status);
	if (ret < 0)
		return ret;

	while (status & EC_MEMMAP_ACC_STATUS_BUSY_BIT) {
		/* Give up after enough attempts, return error. */
		if (attempts++ >= 50)
			return -EIO;

		/* Small delay every so often. */
		if (attempts % 5 == 0)
			msleep(25);

		ret = cros_ec_sensors_cmd_read_u8(ec, EC_MEMMAP_ACC_STATUS,
						  &status);
		if (ret < 0)
			return ret;
	}

	return status;
}

/**
 * read_ec_sensors_data_unsafe() - read acceleration data from EC shared memory
 * @indio_dev:	pointer to IIO device
 * @scan_mask:	bitmap of the sensor indices to scan
 * @data:	location to store data
 *
 * This is the unsafe function for reading the EC data. It does not guarantee
 * that the EC will not modify the data as it is being read in.
 *
 * Return: 0 on success, -errno on failure.
 */
static int cros_ec_sensors_read_data_unsafe(struct iio_dev *indio_dev,
			 unsigned long scan_mask, s16 *data)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	struct cros_ec_device *ec = st->ec;
	unsigned int i;
	int ret;

	/* Read all sensors enabled in scan_mask. Each value is 2 bytes. */
	for_each_set_bit(i, &scan_mask, indio_dev->masklength) {
		ret = cros_ec_sensors_cmd_read_u16(ec,
					     cros_ec_sensors_idx_to_reg(st, i),
					     data);
		if (ret < 0)
			return ret;

		data++;
	}

	return 0;
}

/**
 * cros_ec_sensors_read_lpc() - read acceleration data from EC shared memory.
 * @indio_dev: pointer to IIO device.
 * @scan_mask: bitmap of the sensor indices to scan.
 * @data: location to store data.
 *
 * Note: this is the safe function for reading the EC data. It guarantees
 * that the data sampled was not modified by the EC while being read.
 *
 * Return: 0 on success, -errno on failure.
 */
int cros_ec_sensors_read_lpc(struct iio_dev *indio_dev,
			     unsigned long scan_mask, s16 *data)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	struct cros_ec_device *ec = st->ec;
	u8 samp_id = 0xff, status = 0;
	int ret, attempts = 0;

	/*
	 * Continually read all data from EC until the status byte after
	 * all reads reflects that the EC is not busy and the sample id
	 * matches the sample id from before all reads. This guarantees
	 * that data read in was not modified by the EC while reading.
	 */
	while ((status & (EC_MEMMAP_ACC_STATUS_BUSY_BIT |
			  EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK)) != samp_id) {
		/* If we have tried to read too many times, return error. */
		if (attempts++ >= 5)
			return -EIO;

		/* Read status byte until EC is not busy. */
		ret = cros_ec_sensors_read_until_not_busy(st);
		if (ret < 0)
			return ret;

		/*
		 * Store the current sample id so that we can compare to the
		 * sample id after reading the data.
		 */
		samp_id = ret & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;

		/* Read all EC data, format it, and store it into data. */
		ret = cros_ec_sensors_read_data_unsafe(indio_dev, scan_mask,
						       data);
		if (ret < 0)
			return ret;

		/* Read status byte. */
		ret = cros_ec_sensors_cmd_read_u8(ec, EC_MEMMAP_ACC_STATUS,
						  &status);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cros_ec_sensors_read_lpc);

int cros_ec_sensors_read_cmd(struct iio_dev *indio_dev,
			     unsigned long scan_mask, s16 *data)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	int ret;
	unsigned int i;

	/* Read all sensor data through a command. */
	st->param.cmd = MOTIONSENSE_CMD_DATA;
	ret = cros_ec_motion_send_host_cmd(st, sizeof(st->resp->data));
	if (ret != 0) {
		dev_warn(&indio_dev->dev, "Unable to read sensor data\n");
		return ret;
	}

	for_each_set_bit(i, &scan_mask, indio_dev->masklength) {
		*data = st->resp->data.data[i];
		data++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cros_ec_sensors_read_cmd);

irqreturn_t cros_ec_sensors_capture(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->cmd_lock);

	/* Clear capture data. */
	memset(st->samples, 0, indio_dev->scan_bytes);

	/* Read data based on which channels are enabled in scan mask. */
	ret = st->read_ec_sensors_data(indio_dev,
				       *(indio_dev->active_scan_mask),
				       (s16 *)st->samples);
	if (ret < 0)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, st->samples,
					   iio_get_time_ns(indio_dev));

done:
	/*
	 * Tell the core we are done with this trigger and ready for the
	 * next one.
	 */
	iio_trigger_notify_done(indio_dev->trig);

	mutex_unlock(&st->cmd_lock);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(cros_ec_sensors_capture);

int cros_ec_sensors_core_read(struct cros_ec_sensors_core_state *st,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	int ret = IIO_VAL_INT;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		st->param.cmd = MOTIONSENSE_CMD_EC_RATE;
		st->param.ec_rate.data =
			EC_MOTION_SENSE_NO_VALUE;

		if (cros_ec_motion_send_host_cmd(st, 0))
			ret = -EIO;
		else
			*val = st->resp->ec_rate.ret;
		break;
	case IIO_CHAN_INFO_FREQUENCY:
		st->param.cmd = MOTIONSENSE_CMD_SENSOR_ODR;
		st->param.sensor_odr.data =
			EC_MOTION_SENSE_NO_VALUE;

		if (cros_ec_motion_send_host_cmd(st, 0))
			ret = -EIO;
		else
			*val = st->resp->sensor_odr.ret;
		break;
	default:
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cros_ec_sensors_core_read);

int cros_ec_sensors_core_write(struct cros_ec_sensors_core_state *st,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_FREQUENCY:
		st->param.cmd = MOTIONSENSE_CMD_SENSOR_ODR;
		st->param.sensor_odr.data = val;

		/* Always roundup, so caller gets at least what it asks for. */
		st->param.sensor_odr.roundup = 1;

		if (cros_ec_motion_send_host_cmd(st, 0))
			ret = -EIO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		st->param.cmd = MOTIONSENSE_CMD_EC_RATE;
		st->param.ec_rate.data = val;

		if (cros_ec_motion_send_host_cmd(st, 0))
			ret = -EIO;
		else
			st->curr_sampl_freq = val;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(cros_ec_sensors_core_write);

static int __maybe_unused cros_ec_sensors_prepare(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);

	if (st->curr_sampl_freq == 0)
		return 0;

	/*
	 * If the sensors are sampled at high frequency, we will not be able to
	 * sleep. Set sampling to a long period if necessary.
	 */
	if (st->curr_sampl_freq < CROS_EC_MIN_SUSPEND_SAMPLING_FREQUENCY) {
		mutex_lock(&st->cmd_lock);
		st->param.cmd = MOTIONSENSE_CMD_EC_RATE;
		st->param.ec_rate.data = CROS_EC_MIN_SUSPEND_SAMPLING_FREQUENCY;
		cros_ec_motion_send_host_cmd(st, 0);
		mutex_unlock(&st->cmd_lock);
	}
	return 0;
}

static void __maybe_unused cros_ec_sensors_complete(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);

	if (st->curr_sampl_freq == 0)
		return;

	if (st->curr_sampl_freq < CROS_EC_MIN_SUSPEND_SAMPLING_FREQUENCY) {
		mutex_lock(&st->cmd_lock);
		st->param.cmd = MOTIONSENSE_CMD_EC_RATE;
		st->param.ec_rate.data = st->curr_sampl_freq;
		cros_ec_motion_send_host_cmd(st, 0);
		mutex_unlock(&st->cmd_lock);
	}
}

const struct dev_pm_ops cros_ec_sensors_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.prepare = cros_ec_sensors_prepare,
	.complete = cros_ec_sensors_complete
#endif
};
EXPORT_SYMBOL_GPL(cros_ec_sensors_pm_ops);

MODULE_DESCRIPTION("ChromeOS EC sensor hub core functions");
MODULE_LICENSE("GPL v2");

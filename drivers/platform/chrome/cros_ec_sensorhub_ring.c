// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Chrome OS EC Sensor hub FIFO.
 *
 * Copyright 2020 Google LLC
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_ec_sensorhub.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/slab.h>

static inline int
cros_sensorhub_send_sample(struct cros_ec_sensorhub *sensorhub,
			   struct cros_ec_sensors_ring_sample *sample)
{
	cros_ec_sensorhub_push_data_cb_t cb;
	int id = sample->sensor_id;
	struct iio_dev *indio_dev;

	if (id > sensorhub->sensor_num)
		return -EINVAL;

	cb = sensorhub->push_data[id].push_data_cb;
	if (!cb)
		return 0;

	indio_dev = sensorhub->push_data[id].indio_dev;

	if (sample->flag & MOTIONSENSE_SENSOR_FLAG_FLUSH)
		return 0;

	return cb(indio_dev, sample->vector, sample->timestamp);
}

/**
 * cros_ec_sensorhub_register_push_data() - register the callback to the hub.
 *
 * @sensorhub : Sensor Hub object
 * @sensor_num : The sensor the caller is interested in.
 * @indio_dev : The iio device to use when a sample arrives.
 * @cb : The callback to call when a sample arrives.
 *
 * The callback cb will be used by cros_ec_sensorhub_ring to distribute events
 * from the EC.
 *
 * Return: 0 when callback is registered.
 *         EINVAL is the sensor number is invalid or the slot already used.
 */
int cros_ec_sensorhub_register_push_data(struct cros_ec_sensorhub *sensorhub,
					 u8 sensor_num,
					 struct iio_dev *indio_dev,
					 cros_ec_sensorhub_push_data_cb_t cb)
{
	if (sensor_num >= sensorhub->sensor_num)
		return -EINVAL;
	if (sensorhub->push_data[sensor_num].indio_dev)
		return -EINVAL;

	sensorhub->push_data[sensor_num].indio_dev = indio_dev;
	sensorhub->push_data[sensor_num].push_data_cb = cb;

	return 0;
}
EXPORT_SYMBOL_GPL(cros_ec_sensorhub_register_push_data);

void cros_ec_sensorhub_unregister_push_data(struct cros_ec_sensorhub *sensorhub,
					    u8 sensor_num)
{
	sensorhub->push_data[sensor_num].indio_dev = NULL;
	sensorhub->push_data[sensor_num].push_data_cb = NULL;
}
EXPORT_SYMBOL_GPL(cros_ec_sensorhub_unregister_push_data);

/**
 * cros_ec_sensorhub_ring_fifo_enable() - Enable or disable interrupt generation
 *					  for FIFO events.
 * @sensorhub: Sensor Hub object
 * @on: true when events are requested.
 *
 * To be called before sleeping or when noone is listening.
 * Return: 0 on success, or an error when we can not communicate with the EC.
 *
 */
int cros_ec_sensorhub_ring_fifo_enable(struct cros_ec_sensorhub *sensorhub,
				       bool on)
{
	int ret;

	mutex_lock(&sensorhub->cmd_lock);
	sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_INT_ENABLE;
	sensorhub->params->fifo_int_enable.enable = on;

	sensorhub->msg->outsize = sizeof(struct ec_params_motion_sense);
	sensorhub->msg->insize = sizeof(struct ec_response_motion_sense);

	ret = cros_ec_cmd_xfer_status(sensorhub->ec->ec_dev, sensorhub->msg);
	mutex_unlock(&sensorhub->cmd_lock);

	/* We expect to receive a payload of 4 bytes, ignore. */
	if (ret > 0)
		ret = 0;

	return ret;
}

/**
 * cros_ec_sensor_ring_process_event() - process one EC FIFO event
 *
 * @sensorhub: Sensor Hub object.
 * @fifo_info: FIFO information from the EC (includes b point, EC timebase).
 * @fifo_timestamp: EC IRQ, kernel timebase (aka c).
 * @current_timestamp: calculated event timestamp, kernel timebase (aka a').
 * @in: incoming FIFO event from EC (includes a point, EC timebase).
 * @out: outgoing event to user space (includes a').
 *
 * Process one EC event, add it in the ring if necessary.
 *
 * Return: true if out event has been populated.
 */
static bool
cros_ec_sensor_ring_process_event(struct cros_ec_sensorhub *sensorhub,
				const struct ec_response_motion_sense_fifo_info
				*fifo_info,
				const ktime_t fifo_timestamp,
				ktime_t *current_timestamp,
				struct ec_response_motion_sensor_data *in,
				struct cros_ec_sensors_ring_sample *out)
{
	const s64 now = cros_ec_get_time_ns();
	int axis, async_flags;

	/* Do not populate the filter based on asynchronous events. */
	async_flags = in->flags &
		(MOTIONSENSE_SENSOR_FLAG_ODR | MOTIONSENSE_SENSOR_FLAG_FLUSH);

	if (in->flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP && !async_flags) {
		s64 new_timestamp;

		/*
		 * Disable filtering since we might add more jitter
		 * if b is in a random point in time.
		 */
		new_timestamp = fifo_timestamp -
				fifo_info->timestamp  * 1000 +
				in->timestamp * 1000;

		/*
		 * The timestamp can be stale if we had to use the fifo
		 * info timestamp.
		 */
		if (new_timestamp - *current_timestamp > 0)
			*current_timestamp = new_timestamp;
	}

	if (in->flags & MOTIONSENSE_SENSOR_FLAG_FLUSH) {
		out->sensor_id = in->sensor_num;
		out->timestamp = *current_timestamp;
		out->flag = in->flags;
		/*
		 * No other payload information provided with
		 * flush ack.
		 */
		return true;
	}

	if (in->flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP)
		/* If we just have a timestamp, skip this entry. */
		return false;

	/* Regular sample */
	out->sensor_id = in->sensor_num;
	if (*current_timestamp - now > 0)
		/* If the timestamp is in the future. */
		out->timestamp = now;
	else
		out->timestamp = *current_timestamp;

	out->flag = in->flags;
	for (axis = 0; axis < 3; axis++)
		out->vector[axis] = in->data[axis];

	return true;
}

/**
 * cros_ec_sensorhub_ring_handler() - The trigger handler function
 *
 * @sensorhub: Sensor Hub object.
 *
 * Called by the notifier, process the EC sensor FIFO queue.
 */
static void cros_ec_sensorhub_ring_handler(struct cros_ec_sensorhub *sensorhub)
{
	struct ec_response_motion_sense_fifo_info *fifo_info =
		sensorhub->fifo_info;
	struct cros_ec_dev *ec = sensorhub->ec;
	ktime_t fifo_timestamp, current_timestamp;
	int i, j, number_data, ret;
	struct ec_response_motion_sensor_data *in;
	struct cros_ec_sensors_ring_sample *out, *last_out;

	mutex_lock(&sensorhub->cmd_lock);

	/* Get FIFO information if there are lost vectors. */
	if (fifo_info->total_lost) {
		int fifo_info_length =
			sizeof(struct ec_response_motion_sense_fifo_info) +
			sizeof(u16) * sensorhub->sensor_num;

		/* Need to retrieve the number of lost vectors per sensor */
		sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_INFO;
		sensorhub->msg->outsize = 1;
		sensorhub->msg->insize = fifo_info_length;

		if (cros_ec_cmd_xfer_status(ec->ec_dev, sensorhub->msg) < 0)
			goto error;

		memcpy(fifo_info, &sensorhub->resp->fifo_info,
		       fifo_info_length);

		/*
		 * Update collection time, will not be as precise as the
		 * non-error case.
		 */
		fifo_timestamp = cros_ec_get_time_ns();
	} else {
		fifo_timestamp = sensorhub->fifo_timestamp[
			CROS_EC_SENSOR_NEW_TS];
	}

	if (fifo_info->count > sensorhub->fifo_size ||
	    fifo_info->size != sensorhub->fifo_size) {
		dev_warn(sensorhub->dev,
			 "Mismatch EC data: count %d, size %d - expected %d",
			 fifo_info->count, fifo_info->size,
			 sensorhub->fifo_size);
		goto error;
	}

	/* Copy elements in the main fifo */
	current_timestamp = sensorhub->fifo_timestamp[CROS_EC_SENSOR_LAST_TS];
	out = sensorhub->ring;
	for (i = 0; i < fifo_info->count; i += number_data) {
		sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_READ;
		sensorhub->params->fifo_read.max_data_vector =
			fifo_info->count - i;
		sensorhub->msg->outsize =
			sizeof(struct ec_params_motion_sense);
		sensorhub->msg->insize =
			sizeof(sensorhub->resp->fifo_read) +
			sensorhub->params->fifo_read.max_data_vector *
			  sizeof(struct ec_response_motion_sensor_data);
		ret = cros_ec_cmd_xfer_status(ec->ec_dev, sensorhub->msg);
		if (ret < 0) {
			dev_warn(sensorhub->dev, "Fifo error: %d\n", ret);
			break;
		}
		number_data = sensorhub->resp->fifo_read.number_data;
		if (number_data == 0) {
			dev_dbg(sensorhub->dev, "Unexpected empty FIFO\n");
			break;
		}
		if (number_data > fifo_info->count - i) {
			dev_warn(sensorhub->dev,
				 "Invalid EC data: too many entry received: %d, expected %d",
				 number_data, fifo_info->count - i);
			break;
		}
		if (out + number_data >
		    sensorhub->ring + fifo_info->count) {
			dev_warn(sensorhub->dev,
				 "Too many samples: %d (%zd data) to %d entries for expected %d entries",
				 i, out - sensorhub->ring, i + number_data,
				 fifo_info->count);
			break;
		}

		for (in = sensorhub->resp->fifo_read.data, j = 0;
		     j < number_data; j++, in++) {
			if (cros_ec_sensor_ring_process_event(
						sensorhub, fifo_info,
						fifo_timestamp,
						&current_timestamp,
						in, out))
				out++;
		}
	}
	mutex_unlock(&sensorhub->cmd_lock);
	last_out = out;

	if (out == sensorhub->ring)
		/* Unexpected empty FIFO. */
		goto ring_handler_end;

	/*
	 * Check if current_timestamp is ahead of the last sample.
	 * Normally, the EC appends a timestamp after the last sample, but if
	 * the AP is slow to respond to the IRQ, the EC may have added new
	 * samples. Use the FIFO info timestamp as last timestamp then.
	 */
	if ((last_out - 1)->timestamp == current_timestamp)
		current_timestamp = fifo_timestamp;

	/* Warn on lost samples. */
	if (fifo_info->total_lost)
		for (i = 0; i < sensorhub->sensor_num; i++) {
			if (fifo_info->lost[i])
				dev_warn_ratelimited(sensorhub->dev,
						     "Sensor %d: lost: %d out of %d\n",
						     i, fifo_info->lost[i],
						     fifo_info->total_lost);
		}

	/* Push the event into the FIFO. */
	for (out = sensorhub->ring; out < last_out; out++)
		cros_sensorhub_send_sample(sensorhub, out);

ring_handler_end:
	sensorhub->fifo_timestamp[CROS_EC_SENSOR_LAST_TS] = current_timestamp;
	return;

error:
	mutex_unlock(&sensorhub->cmd_lock);
}

static int cros_ec_sensorhub_event(struct notifier_block *nb,
				   unsigned long queued_during_suspend,
				   void *_notify)
{
	struct cros_ec_sensorhub *sensorhub;
	struct cros_ec_device *ec_dev;

	sensorhub = container_of(nb, struct cros_ec_sensorhub, notifier);
	ec_dev = sensorhub->ec->ec_dev;

	if (ec_dev->event_data.event_type != EC_MKBP_EVENT_SENSOR_FIFO)
		return NOTIFY_DONE;

	if (ec_dev->event_size != sizeof(ec_dev->event_data.data.sensor_fifo)) {
		dev_warn(ec_dev->dev, "Invalid fifo info size\n");
		return NOTIFY_DONE;
	}

	if (queued_during_suspend)
		return NOTIFY_OK;

	memcpy(sensorhub->fifo_info, &ec_dev->event_data.data.sensor_fifo.info,
	       sizeof(*sensorhub->fifo_info));
	sensorhub->fifo_timestamp[CROS_EC_SENSOR_NEW_TS] =
		ec_dev->last_event_time;
	cros_ec_sensorhub_ring_handler(sensorhub);

	return NOTIFY_OK;
}

/**
 * cros_ec_sensorhub_ring_add() - Add the FIFO functionality if the EC
 *				  supports it.
 *
 * @sensorhub : Sensor Hub object.
 *
 * Return: 0 on success.
 */
int cros_ec_sensorhub_ring_add(struct cros_ec_sensorhub *sensorhub)
{
	struct cros_ec_dev *ec = sensorhub->ec;
	int ret;
	int fifo_info_length =
		sizeof(struct ec_response_motion_sense_fifo_info) +
		sizeof(u16) * sensorhub->sensor_num;

	/* Allocate the array for lost events. */
	sensorhub->fifo_info = devm_kzalloc(sensorhub->dev, fifo_info_length,
					    GFP_KERNEL);
	if (!sensorhub->fifo_info)
		return -ENOMEM;

	/* Retrieve FIFO information */
	sensorhub->msg->version = 2;
	sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_INFO;
	sensorhub->msg->outsize = 1;
	sensorhub->msg->insize = fifo_info_length;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, sensorhub->msg);
	if (ret < 0)
		return ret;

	/*
	 * Allocate the full fifo. We need to copy the whole FIFO to set
	 * timestamps properly.
	 */
	sensorhub->fifo_size = sensorhub->resp->fifo_info.size;
	sensorhub->ring = devm_kcalloc(sensorhub->dev, sensorhub->fifo_size,
				       sizeof(*sensorhub->ring), GFP_KERNEL);
	if (!sensorhub->ring)
		return -ENOMEM;

	/*
	 * Allocate the callback area based on the number of sensors.
	 */
	sensorhub->push_data = devm_kcalloc(
			sensorhub->dev, sensorhub->sensor_num,
			sizeof(*sensorhub->push_data),
			GFP_KERNEL);
	if (!sensorhub->push_data)
		return -ENOMEM;

	sensorhub->fifo_timestamp[CROS_EC_SENSOR_LAST_TS] =
		cros_ec_get_time_ns();

	/* Register the notifier that will act as a top half interrupt. */
	sensorhub->notifier.notifier_call = cros_ec_sensorhub_event;
	ret = blocking_notifier_chain_register(&ec->ec_dev->event_notifier,
					       &sensorhub->notifier);
	if (ret < 0)
		return ret;

	/* Start collection samples. */
	return cros_ec_sensorhub_ring_fifo_enable(sensorhub, true);
}

void cros_ec_sensorhub_ring_remove(void *arg)
{
	struct cros_ec_sensorhub *sensorhub = arg;
	struct cros_ec_device *ec_dev = sensorhub->ec->ec_dev;

	/* Disable the ring, prevent EC interrupt to the AP for nothing. */
	cros_ec_sensorhub_ring_fifo_enable(sensorhub, false);
	blocking_notifier_chain_unregister(&ec_dev->event_notifier,
					   &sensorhub->notifier);
}

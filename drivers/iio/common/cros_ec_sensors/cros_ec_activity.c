// SPDX-License-Identifier: GPL-2.0
/*
 * cros_ec_activity - Driver for activities/gesture recognition.
 *
 * Copyright 2025 Google, Inc
 *
 * This driver uses the cros-ec interface to communicate with the ChromeOS
 * EC about activity data.
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include <linux/iio/common/cros_ec_sensors_core.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>

#define DRV_NAME "cros-ec-activity"

/* state data for ec_sensors iio driver. */
struct cros_ec_sensors_state {
	/* Shared by all sensors */
	struct cros_ec_sensors_core_state core;

	struct iio_chan_spec *channels;

	int body_detection_channel_index;
	int sig_motion_channel_index;
};

static const struct iio_event_spec cros_ec_activity_single_shot[] = {
	{
		.type = IIO_EV_TYPE_CHANGE,
		/* significant motion trigger when we get out of still. */
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_event_spec cros_ec_body_detect_events[] = {
	{
		.type = IIO_EV_TYPE_CHANGE,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static int cros_ec_activity_sensors_read_raw(struct iio_dev *indio_dev,
					     struct iio_chan_spec const *chan,
					     int *val, int *val2, long mask)
{
	struct cros_ec_sensors_state *st = iio_priv(indio_dev);
	int ret;

	if (chan->type != IIO_PROXIMITY || mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	guard(mutex)(&st->core.cmd_lock);
	st->core.param.cmd = MOTIONSENSE_CMD_GET_ACTIVITY;
	st->core.param.get_activity.activity =
		MOTIONSENSE_ACTIVITY_BODY_DETECTION;
	ret = cros_ec_motion_send_host_cmd(&st->core, 0);
	if (ret)
		return ret;

	/*
	 * EC actually report if a body is near (1) or far (0).
	 * Units for proximity sensor after scale is in meter,
	 * so invert the result to return 0m when near and 1m when far.
	 */
	*val = !st->core.resp->get_activity.state;
	return IIO_VAL_INT;
}

static int cros_ec_activity_read_event_config(struct iio_dev *indio_dev,
					      const struct iio_chan_spec *chan,
					      enum iio_event_type type,
					      enum iio_event_direction dir)
{
	struct cros_ec_sensors_state *st = iio_priv(indio_dev);
	int ret;

	if (chan->type != IIO_ACTIVITY && chan->type != IIO_PROXIMITY)
		return -EINVAL;

	guard(mutex)(&st->core.cmd_lock);
	st->core.param.cmd = MOTIONSENSE_CMD_LIST_ACTIVITIES;
	ret = cros_ec_motion_send_host_cmd(&st->core, 0);
	if (ret)
		return ret;

	switch (chan->type) {
	case IIO_PROXIMITY:
		return !!(st->core.resp->list_activities.enabled &
			 (1 << MOTIONSENSE_ACTIVITY_BODY_DETECTION));
	case IIO_ACTIVITY:
		if (chan->channel2 == IIO_MOD_STILL) {
			return !!(st->core.resp->list_activities.enabled &
				 (1 << MOTIONSENSE_ACTIVITY_SIG_MOTION));
		}

		dev_warn(&indio_dev->dev, "Unknown activity: %d\n",
			 chan->channel2);
		return -EINVAL;
	default:
		dev_warn(&indio_dev->dev, "Unknown channel type: %d\n",
			 chan->type);
		return -EINVAL;
	}
}

static int cros_ec_activity_write_event_config(struct iio_dev *indio_dev,
					       const struct iio_chan_spec *chan,
					       enum iio_event_type type,
					       enum iio_event_direction dir,
					       bool state)
{
	struct cros_ec_sensors_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->core.cmd_lock);
	st->core.param.cmd = MOTIONSENSE_CMD_SET_ACTIVITY;
	switch (chan->type) {
	case IIO_PROXIMITY:
		st->core.param.set_activity.activity =
			MOTIONSENSE_ACTIVITY_BODY_DETECTION;
		break;
	case IIO_ACTIVITY:
		if (chan->channel2 == IIO_MOD_STILL) {
			st->core.param.set_activity.activity =
				MOTIONSENSE_ACTIVITY_SIG_MOTION;
			break;
		}
		dev_warn(&indio_dev->dev, "Unknown activity: %d\n",
			 chan->channel2);
		return -EINVAL;
	default:
		dev_warn(&indio_dev->dev, "Unknown channel type: %d\n",
			 chan->type);
		return -EINVAL;
	}
	st->core.param.set_activity.enable = state;
	return cros_ec_motion_send_host_cmd(&st->core, 0);
}

static int cros_ec_activity_push_data(struct iio_dev *indio_dev,
				      s16 *data, s64 timestamp)
{
	struct ec_response_activity_data *activity_data =
			(struct ec_response_activity_data *)data;
	enum motionsensor_activity activity = activity_data->activity;
	u8 state = activity_data->state;
	const struct cros_ec_sensors_state *st = iio_priv(indio_dev);
	const struct iio_chan_spec *chan;
	enum iio_event_direction dir;
	int index;

	switch (activity) {
	case MOTIONSENSE_ACTIVITY_BODY_DETECTION:
		index = st->body_detection_channel_index;
		dir = state ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING;
		break;
	case MOTIONSENSE_ACTIVITY_SIG_MOTION:
		index = st->sig_motion_channel_index;
		dir = IIO_EV_DIR_FALLING;
		break;
	default:
		dev_warn(&indio_dev->dev, "Unknown activity: %d\n", activity);
		return 0;
	}
	chan = &st->channels[index];
	iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(chan->type, index, chan->event_spec[0].type, dir),
		       timestamp);
	return 0;
}

static irqreturn_t cros_ec_activity_capture(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;

	/*
	 * This callback would be called when a software trigger is
	 * used. But when this virtual sensor is present, it is guaranteed
	 * the sensor hub is advanced enough to not need a software trigger.
	 */
	dev_warn(&indio_dev->dev, "%s: Not Expected\n", __func__);
	return IRQ_NONE;
}

static const struct iio_info ec_sensors_info = {
	.read_raw = &cros_ec_activity_sensors_read_raw,
	.read_event_config = cros_ec_activity_read_event_config,
	.write_event_config = cros_ec_activity_write_event_config,
};

static int cros_ec_sensors_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_device *ec_device = dev_get_drvdata(dev->parent);
	struct iio_dev *indio_dev;
	struct cros_ec_sensors_state *st;
	struct iio_chan_spec *channel;
	unsigned long activities;
	int i, index, ret, nb_activities;

	if (!ec_device) {
		dev_warn(dev, "No CROS EC device found.\n");
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	ret = cros_ec_sensors_core_init(pdev, indio_dev, true,
					cros_ec_activity_capture);
	if (ret)
		return ret;

	indio_dev->info = &ec_sensors_info;
	st = iio_priv(indio_dev);
	st->core.type = st->core.resp->info.type;
	st->core.read_ec_sensors_data = cros_ec_sensors_read_cmd;

	st->core.param.cmd = MOTIONSENSE_CMD_LIST_ACTIVITIES;
	ret = cros_ec_motion_send_host_cmd(&st->core, 0);
	if (ret)
		return ret;

	activities = st->core.resp->list_activities.enabled |
		     st->core.resp->list_activities.disabled;
	if (!activities)
		return -ENODEV;

	/* Allocate a channel per activity and one for timestamp */
	nb_activities = hweight_long(activities) + 1;
	st->channels = devm_kcalloc(dev, nb_activities,
				    sizeof(*st->channels), GFP_KERNEL);
	if (!st->channels)
		return -ENOMEM;

	channel = &st->channels[0];
	index = 0;
	for_each_set_bit(i, &activities, BITS_PER_LONG) {
		/* List all available triggers */
		if (i == MOTIONSENSE_ACTIVITY_BODY_DETECTION) {
			channel->type = IIO_PROXIMITY;
			channel->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
			channel->event_spec = cros_ec_body_detect_events;
			channel->num_event_specs =
				ARRAY_SIZE(cros_ec_body_detect_events);
			st->body_detection_channel_index = index;
		} else {
			channel->type = IIO_ACTIVITY;
			channel->modified = 1;
			channel->event_spec = cros_ec_activity_single_shot;
			channel->num_event_specs =
				ARRAY_SIZE(cros_ec_activity_single_shot);
			if (i == MOTIONSENSE_ACTIVITY_SIG_MOTION) {
				channel->channel2 = IIO_MOD_STILL;
				st->sig_motion_channel_index = index;
			} else {
				dev_warn(dev, "Unknown activity: %d\n", i);
				continue;
			}
		}
		channel->ext_info = cros_ec_sensors_limited_info;
		channel->scan_index = index++;
		channel++;
	}

	/* Timestamp */
	channel->scan_index = index;
	channel->type = IIO_TIMESTAMP;
	channel->channel = -1;
	channel->scan_type.sign = 's';
	channel->scan_type.realbits = 64;
	channel->scan_type.storagebits = 64;

	indio_dev->channels = st->channels;
	indio_dev->num_channels = index + 1;

	return cros_ec_sensors_core_register(dev, indio_dev,
					     cros_ec_activity_push_data);
}

static struct platform_driver cros_ec_sensors_platform_driver = {
	.driver = {
		.name	= DRV_NAME,
	},
	.probe		= cros_ec_sensors_probe,
};
module_platform_driver(cros_ec_sensors_platform_driver);

MODULE_DESCRIPTION("ChromeOS EC activity sensors driver");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_LICENSE("GPL v2");

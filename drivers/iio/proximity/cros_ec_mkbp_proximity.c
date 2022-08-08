// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for cros-ec proximity sensor exposed through MKBP switch
 *
 * Copyright 2021 Google LLC.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <asm/unaligned.h>

struct cros_ec_mkbp_proximity_data {
	struct cros_ec_device *ec;
	struct iio_dev *indio_dev;
	struct mutex lock;
	struct notifier_block notifier;
	int last_proximity;
	bool enabled;
};

static const struct iio_event_spec cros_ec_mkbp_proximity_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec cros_ec_mkbp_proximity_chan_spec[] = {
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = cros_ec_mkbp_proximity_events,
		.num_event_specs = ARRAY_SIZE(cros_ec_mkbp_proximity_events),
	},
};

static int cros_ec_mkbp_proximity_parse_state(const void *data)
{
	u32 switches = get_unaligned_le32(data);

	return !!(switches & BIT(EC_MKBP_FRONT_PROXIMITY));
}

static int cros_ec_mkbp_proximity_query(struct cros_ec_device *ec_dev,
					int *state)
{
	struct {
		struct cros_ec_command msg;
		union {
			struct ec_params_mkbp_info params;
			u32 switches;
		};
	} __packed buf = { };
	struct ec_params_mkbp_info *params = &buf.params;
	struct cros_ec_command *msg = &buf.msg;
	u32 *switches = &buf.switches;
	size_t insize = sizeof(*switches);
	int ret;

	msg->command = EC_CMD_MKBP_INFO;
	msg->version = 1;
	msg->outsize = sizeof(*params);
	msg->insize = insize;

	params->info_type = EC_MKBP_INFO_CURRENT;
	params->event_type = EC_MKBP_EVENT_SWITCH;

	ret = cros_ec_cmd_xfer_status(ec_dev, msg);
	if (ret < 0)
		return ret;

	if (ret != insize) {
		dev_warn(ec_dev->dev, "wrong result size: %d != %zu\n", ret,
			 insize);
		return -EPROTO;
	}

	*state = cros_ec_mkbp_proximity_parse_state(switches);
	return IIO_VAL_INT;
}

static void cros_ec_mkbp_proximity_push_event(struct cros_ec_mkbp_proximity_data *data, int state)
{
	s64 timestamp;
	u64 ev;
	int dir;
	struct iio_dev *indio_dev = data->indio_dev;
	struct cros_ec_device *ec = data->ec;

	mutex_lock(&data->lock);
	if (state != data->last_proximity) {
		if (data->enabled) {
			timestamp = ktime_to_ns(ec->last_event_time);
			if (iio_device_get_clock(indio_dev) != CLOCK_BOOTTIME)
				timestamp = iio_get_time_ns(indio_dev);

			dir = state ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING;
			ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
						  IIO_EV_TYPE_THRESH, dir);
			iio_push_event(indio_dev, ev, timestamp);
		}
		data->last_proximity = state;
	}
	mutex_unlock(&data->lock);
}

static int cros_ec_mkbp_proximity_notify(struct notifier_block *nb,
					 unsigned long queued_during_suspend,
					 void *_ec)
{
	struct cros_ec_mkbp_proximity_data *data;
	struct cros_ec_device *ec = _ec;
	u8 event_type = ec->event_data.event_type & EC_MKBP_EVENT_TYPE_MASK;
	void *switches;
	int state;

	if (event_type == EC_MKBP_EVENT_SWITCH) {
		data = container_of(nb, struct cros_ec_mkbp_proximity_data,
				    notifier);

		switches = &ec->event_data.data.switches;
		state = cros_ec_mkbp_proximity_parse_state(switches);
		cros_ec_mkbp_proximity_push_event(data, state);
	}

	return NOTIFY_OK;
}

static int cros_ec_mkbp_proximity_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan, int *val,
			   int *val2, long mask)
{
	struct cros_ec_mkbp_proximity_data *data = iio_priv(indio_dev);
	struct cros_ec_device *ec = data->ec;

	if (chan->type == IIO_PROXIMITY && mask == IIO_CHAN_INFO_RAW)
		return cros_ec_mkbp_proximity_query(ec, val);

	return -EINVAL;
}

static int cros_ec_mkbp_proximity_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct cros_ec_mkbp_proximity_data *data = iio_priv(indio_dev);

	return data->enabled;
}

static int cros_ec_mkbp_proximity_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, int state)
{
	struct cros_ec_mkbp_proximity_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	data->enabled = state;
	mutex_unlock(&data->lock);

	return 0;
}

static const struct iio_info cros_ec_mkbp_proximity_info = {
	.read_raw = cros_ec_mkbp_proximity_read_raw,
	.read_event_config = cros_ec_mkbp_proximity_read_event_config,
	.write_event_config = cros_ec_mkbp_proximity_write_event_config,
};

static __maybe_unused int cros_ec_mkbp_proximity_resume(struct device *dev)
{
	struct cros_ec_mkbp_proximity_data *data = dev_get_drvdata(dev);
	struct cros_ec_device *ec = data->ec;
	int ret, state;

	ret = cros_ec_mkbp_proximity_query(ec, &state);
	if (ret < 0) {
		dev_warn(dev, "failed to fetch proximity state on resume: %d\n",
			 ret);
	} else {
		cros_ec_mkbp_proximity_push_event(data, state);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(cros_ec_mkbp_proximity_pm_ops, NULL,
			 cros_ec_mkbp_proximity_resume);

static int cros_ec_mkbp_proximity_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_device *ec = dev_get_drvdata(dev->parent);
	struct iio_dev *indio_dev;
	struct cros_ec_mkbp_proximity_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->ec = ec;
	data->indio_dev = indio_dev;
	data->last_proximity = -1; /* Unknown to start */
	mutex_init(&data->lock);
	platform_set_drvdata(pdev, data);

	indio_dev->name = dev->driver->name;
	indio_dev->info = &cros_ec_mkbp_proximity_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = cros_ec_mkbp_proximity_chan_spec;
	indio_dev->num_channels = ARRAY_SIZE(cros_ec_mkbp_proximity_chan_spec);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ret;

	data->notifier.notifier_call = cros_ec_mkbp_proximity_notify;
	blocking_notifier_chain_register(&ec->event_notifier, &data->notifier);

	return 0;
}

static int cros_ec_mkbp_proximity_remove(struct platform_device *pdev)
{
	struct cros_ec_mkbp_proximity_data *data = platform_get_drvdata(pdev);
	struct cros_ec_device *ec = data->ec;

	blocking_notifier_chain_unregister(&ec->event_notifier,
					   &data->notifier);

	return 0;
}

static const struct of_device_id cros_ec_mkbp_proximity_of_match[] = {
	{ .compatible = "google,cros-ec-mkbp-proximity" },
	{}
};
MODULE_DEVICE_TABLE(of, cros_ec_mkbp_proximity_of_match);

static struct platform_driver cros_ec_mkbp_proximity_driver = {
	.driver = {
		.name = "cros-ec-mkbp-proximity",
		.of_match_table = cros_ec_mkbp_proximity_of_match,
		.pm = &cros_ec_mkbp_proximity_pm_ops,
	},
	.probe = cros_ec_mkbp_proximity_probe,
	.remove = cros_ec_mkbp_proximity_remove,
};
module_platform_driver(cros_ec_mkbp_proximity_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC MKBP proximity sensor driver");

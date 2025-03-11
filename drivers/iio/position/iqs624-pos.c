// SPDX-License-Identifier: GPL-2.0+
/*
 * Azoteq IQS624/625 Angular Position Sensors
 *
 * Copyright (C) 2019 Jeff LaBundy <jeff@labundy.com>
 */

#include <linux/device.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/mfd/iqs62x.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define IQS624_POS_DEG_OUT			0x16

#define IQS624_POS_SCALE1			(314159 / 180)
#define IQS624_POS_SCALE2			100000

struct iqs624_pos_private {
	struct iqs62x_core *iqs62x;
	struct iio_dev *indio_dev;
	struct notifier_block notifier;
	struct mutex lock;
	bool angle_en;
	u16 angle;
};

static int iqs624_pos_angle_en(struct iqs62x_core *iqs62x, bool angle_en)
{
	unsigned int event_mask = IQS624_HALL_UI_WHL_EVENT;

	/*
	 * The IQS625 reports angular position in the form of coarse intervals,
	 * so only interval change events are unmasked. Conversely, the IQS624
	 * reports angular position down to one degree of resolution, so wheel
	 * movement events are unmasked instead.
	 */
	if (iqs62x->dev_desc->prod_num == IQS625_PROD_NUM)
		event_mask = IQS624_HALL_UI_INT_EVENT;

	return regmap_update_bits(iqs62x->regmap, IQS624_HALL_UI, event_mask,
				  angle_en ? 0 : 0xFF);
}

static int iqs624_pos_notifier(struct notifier_block *notifier,
			       unsigned long event_flags, void *context)
{
	struct iqs62x_event_data *event_data = context;
	struct iqs624_pos_private *iqs624_pos;
	struct iqs62x_core *iqs62x;
	struct iio_dev *indio_dev;
	u16 angle = event_data->ui_data;
	s64 timestamp;
	int ret;

	iqs624_pos = container_of(notifier, struct iqs624_pos_private,
				  notifier);
	indio_dev = iqs624_pos->indio_dev;
	timestamp = iio_get_time_ns(indio_dev);

	iqs62x = iqs624_pos->iqs62x;
	if (iqs62x->dev_desc->prod_num == IQS625_PROD_NUM)
		angle = event_data->interval;

	mutex_lock(&iqs624_pos->lock);

	if (event_flags & BIT(IQS62X_EVENT_SYS_RESET)) {
		ret = iqs624_pos_angle_en(iqs62x, iqs624_pos->angle_en);
		if (ret) {
			dev_err(indio_dev->dev.parent,
				"Failed to re-initialize device: %d\n", ret);
			ret = NOTIFY_BAD;
		} else {
			ret = NOTIFY_OK;
		}
	} else if (iqs624_pos->angle_en && (angle != iqs624_pos->angle)) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ANGL, 0,
						    IIO_EV_TYPE_CHANGE,
						    IIO_EV_DIR_NONE),
			       timestamp);

		iqs624_pos->angle = angle;
		ret = NOTIFY_OK;
	} else {
		ret = NOTIFY_DONE;
	}

	mutex_unlock(&iqs624_pos->lock);

	return ret;
}

static void iqs624_pos_notifier_unregister(void *context)
{
	struct iqs624_pos_private *iqs624_pos = context;
	struct iio_dev *indio_dev = iqs624_pos->indio_dev;
	int ret;

	ret = blocking_notifier_chain_unregister(&iqs624_pos->iqs62x->nh,
						 &iqs624_pos->notifier);
	if (ret)
		dev_err(indio_dev->dev.parent,
			"Failed to unregister notifier: %d\n", ret);
}

static int iqs624_pos_angle_get(struct iqs62x_core *iqs62x, unsigned int *val)
{
	int ret;
	__le16 val_buf;

	if (iqs62x->dev_desc->prod_num == IQS625_PROD_NUM)
		return regmap_read(iqs62x->regmap, iqs62x->dev_desc->interval,
				   val);

	ret = regmap_raw_read(iqs62x->regmap, IQS624_POS_DEG_OUT, &val_buf,
			      sizeof(val_buf));
	if (ret)
		return ret;

	*val = le16_to_cpu(val_buf);

	return 0;
}

static int iqs624_pos_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct iqs624_pos_private *iqs624_pos = iio_priv(indio_dev);
	struct iqs62x_core *iqs62x = iqs624_pos->iqs62x;
	unsigned int scale = 1;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iqs624_pos_angle_get(iqs62x, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		if (iqs62x->dev_desc->prod_num == IQS625_PROD_NUM) {
			ret = regmap_read(iqs62x->regmap, IQS624_INTERVAL_DIV,
					  &scale);
			if (ret)
				return ret;
		}

		*val = scale * IQS624_POS_SCALE1;
		*val2 = IQS624_POS_SCALE2;
		return IIO_VAL_FRACTIONAL;

	default:
		return -EINVAL;
	}
}

static int iqs624_pos_read_event_config(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir)
{
	struct iqs624_pos_private *iqs624_pos = iio_priv(indio_dev);
	int ret;

	mutex_lock(&iqs624_pos->lock);
	ret = iqs624_pos->angle_en;
	mutex_unlock(&iqs624_pos->lock);

	return ret;
}

static int iqs624_pos_write_event_config(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir,
					 bool state)
{
	struct iqs624_pos_private *iqs624_pos = iio_priv(indio_dev);
	struct iqs62x_core *iqs62x = iqs624_pos->iqs62x;
	unsigned int val;
	int ret;

	mutex_lock(&iqs624_pos->lock);

	ret = iqs624_pos_angle_get(iqs62x, &val);
	if (ret)
		goto err_mutex;

	ret = iqs624_pos_angle_en(iqs62x, state);
	if (ret)
		goto err_mutex;

	iqs624_pos->angle = val;
	iqs624_pos->angle_en = state;

err_mutex:
	mutex_unlock(&iqs624_pos->lock);

	return ret;
}

static const struct iio_info iqs624_pos_info = {
	.read_raw = &iqs624_pos_read_raw,
	.read_event_config = iqs624_pos_read_event_config,
	.write_event_config = iqs624_pos_write_event_config,
};

static const struct iio_event_spec iqs624_pos_events[] = {
	{
		.type = IIO_EV_TYPE_CHANGE,
		.dir = IIO_EV_DIR_NONE,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec iqs624_pos_channels[] = {
	{
		.type = IIO_ANGL,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.event_spec = iqs624_pos_events,
		.num_event_specs = ARRAY_SIZE(iqs624_pos_events),
	},
};

static int iqs624_pos_probe(struct platform_device *pdev)
{
	struct iqs62x_core *iqs62x = dev_get_drvdata(pdev->dev.parent);
	struct iqs624_pos_private *iqs624_pos;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*iqs624_pos));
	if (!indio_dev)
		return -ENOMEM;

	iqs624_pos = iio_priv(indio_dev);
	iqs624_pos->iqs62x = iqs62x;
	iqs624_pos->indio_dev = indio_dev;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = iqs624_pos_channels;
	indio_dev->num_channels = ARRAY_SIZE(iqs624_pos_channels);
	indio_dev->name = iqs62x->dev_desc->dev_name;
	indio_dev->info = &iqs624_pos_info;

	mutex_init(&iqs624_pos->lock);

	iqs624_pos->notifier.notifier_call = iqs624_pos_notifier;
	ret = blocking_notifier_chain_register(&iqs624_pos->iqs62x->nh,
					       &iqs624_pos->notifier);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register notifier: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&pdev->dev,
				       iqs624_pos_notifier_unregister,
				       iqs624_pos);
	if (ret)
		return ret;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static struct platform_driver iqs624_pos_platform_driver = {
	.driver = {
		.name = "iqs624-pos",
	},
	.probe = iqs624_pos_probe,
};
module_platform_driver(iqs624_pos_platform_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("Azoteq IQS624/625 Angular Position Sensors");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:iqs624-pos");

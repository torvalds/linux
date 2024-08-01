// SPDX-License-Identifier: GPL-2.0+
/*
 * Azoteq IQS621/622 Ambient Light Sensors
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

#define IQS621_ALS_FLAGS_LIGHT			BIT(7)
#define IQS621_ALS_FLAGS_RANGE			GENMASK(3, 0)

#define IQS621_ALS_UI_OUT			0x17

#define IQS621_ALS_THRESH_DARK			0x80
#define IQS621_ALS_THRESH_LIGHT			0x81

#define IQS622_IR_RANGE				0x15
#define IQS622_IR_FLAGS				0x16
#define IQS622_IR_FLAGS_TOUCH			BIT(1)
#define IQS622_IR_FLAGS_PROX			BIT(0)

#define IQS622_IR_UI_OUT			0x17

#define IQS622_IR_THRESH_PROX			0x91
#define IQS622_IR_THRESH_TOUCH			0x92

struct iqs621_als_private {
	struct iqs62x_core *iqs62x;
	struct iio_dev *indio_dev;
	struct notifier_block notifier;
	struct mutex lock;
	bool light_en;
	bool range_en;
	bool prox_en;
	u8 als_flags;
	u8 ir_flags_mask;
	u8 ir_flags;
	u8 thresh_light;
	u8 thresh_dark;
	u8 thresh_prox;
};

static int iqs621_als_init(struct iqs621_als_private *iqs621_als)
{
	struct iqs62x_core *iqs62x = iqs621_als->iqs62x;
	unsigned int event_mask = 0;
	int ret;

	switch (iqs621_als->ir_flags_mask) {
	case IQS622_IR_FLAGS_TOUCH:
		ret = regmap_write(iqs62x->regmap, IQS622_IR_THRESH_TOUCH,
				   iqs621_als->thresh_prox);
		break;

	case IQS622_IR_FLAGS_PROX:
		ret = regmap_write(iqs62x->regmap, IQS622_IR_THRESH_PROX,
				   iqs621_als->thresh_prox);
		break;

	default:
		ret = regmap_write(iqs62x->regmap, IQS621_ALS_THRESH_LIGHT,
				   iqs621_als->thresh_light);
		if (ret)
			return ret;

		ret = regmap_write(iqs62x->regmap, IQS621_ALS_THRESH_DARK,
				   iqs621_als->thresh_dark);
	}

	if (ret)
		return ret;

	if (iqs621_als->light_en || iqs621_als->range_en)
		event_mask |= iqs62x->dev_desc->als_mask;

	if (iqs621_als->prox_en)
		event_mask |= iqs62x->dev_desc->ir_mask;

	return regmap_clear_bits(iqs62x->regmap, IQS620_GLBL_EVENT_MASK,
				 event_mask);
}

static int iqs621_als_notifier(struct notifier_block *notifier,
			       unsigned long event_flags, void *context)
{
	struct iqs62x_event_data *event_data = context;
	struct iqs621_als_private *iqs621_als;
	struct iio_dev *indio_dev;
	bool light_new, light_old;
	bool prox_new, prox_old;
	u8 range_new, range_old;
	s64 timestamp;
	int ret;

	iqs621_als = container_of(notifier, struct iqs621_als_private,
				  notifier);
	indio_dev = iqs621_als->indio_dev;
	timestamp = iio_get_time_ns(indio_dev);

	mutex_lock(&iqs621_als->lock);

	if (event_flags & BIT(IQS62X_EVENT_SYS_RESET)) {
		ret = iqs621_als_init(iqs621_als);
		if (ret) {
			dev_err(indio_dev->dev.parent,
				"Failed to re-initialize device: %d\n", ret);
			ret = NOTIFY_BAD;
		} else {
			ret = NOTIFY_OK;
		}

		goto err_mutex;
	}

	if (!iqs621_als->light_en && !iqs621_als->range_en &&
	    !iqs621_als->prox_en) {
		ret = NOTIFY_DONE;
		goto err_mutex;
	}

	/* IQS621 only */
	light_new = event_data->als_flags & IQS621_ALS_FLAGS_LIGHT;
	light_old = iqs621_als->als_flags & IQS621_ALS_FLAGS_LIGHT;

	if (iqs621_als->light_en && light_new && !light_old)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);
	else if (iqs621_als->light_en && !light_new && light_old)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);

	/* IQS621 and IQS622 */
	range_new = event_data->als_flags & IQS621_ALS_FLAGS_RANGE;
	range_old = iqs621_als->als_flags & IQS621_ALS_FLAGS_RANGE;

	if (iqs621_als->range_en && (range_new > range_old))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_INTENSITY, 0,
						    IIO_EV_TYPE_CHANGE,
						    IIO_EV_DIR_RISING),
			       timestamp);
	else if (iqs621_als->range_en && (range_new < range_old))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_INTENSITY, 0,
						    IIO_EV_TYPE_CHANGE,
						    IIO_EV_DIR_FALLING),
			       timestamp);

	/* IQS622 only */
	prox_new = event_data->ir_flags & iqs621_als->ir_flags_mask;
	prox_old = iqs621_als->ir_flags & iqs621_als->ir_flags_mask;

	if (iqs621_als->prox_en && prox_new && !prox_old)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);
	else if (iqs621_als->prox_en && !prox_new && prox_old)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);

	iqs621_als->als_flags = event_data->als_flags;
	iqs621_als->ir_flags = event_data->ir_flags;
	ret = NOTIFY_OK;

err_mutex:
	mutex_unlock(&iqs621_als->lock);

	return ret;
}

static void iqs621_als_notifier_unregister(void *context)
{
	struct iqs621_als_private *iqs621_als = context;
	struct iio_dev *indio_dev = iqs621_als->indio_dev;
	int ret;

	ret = blocking_notifier_chain_unregister(&iqs621_als->iqs62x->nh,
						 &iqs621_als->notifier);
	if (ret)
		dev_err(indio_dev->dev.parent,
			"Failed to unregister notifier: %d\n", ret);
}

static int iqs621_als_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct iqs621_als_private *iqs621_als = iio_priv(indio_dev);
	struct iqs62x_core *iqs62x = iqs621_als->iqs62x;
	int ret;
	__le16 val_buf;

	switch (chan->type) {
	case IIO_INTENSITY:
		ret = regmap_read(iqs62x->regmap, chan->address, val);
		if (ret)
			return ret;

		*val &= IQS621_ALS_FLAGS_RANGE;
		return IIO_VAL_INT;

	case IIO_PROXIMITY:
	case IIO_LIGHT:
		ret = regmap_raw_read(iqs62x->regmap, chan->address, &val_buf,
				      sizeof(val_buf));
		if (ret)
			return ret;

		*val = le16_to_cpu(val_buf);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int iqs621_als_read_event_config(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir)
{
	struct iqs621_als_private *iqs621_als = iio_priv(indio_dev);
	int ret;

	mutex_lock(&iqs621_als->lock);

	switch (chan->type) {
	case IIO_LIGHT:
		ret = iqs621_als->light_en;
		break;

	case IIO_INTENSITY:
		ret = iqs621_als->range_en;
		break;

	case IIO_PROXIMITY:
		ret = iqs621_als->prox_en;
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&iqs621_als->lock);

	return ret;
}

static int iqs621_als_write_event_config(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir,
					 int state)
{
	struct iqs621_als_private *iqs621_als = iio_priv(indio_dev);
	struct iqs62x_core *iqs62x = iqs621_als->iqs62x;
	unsigned int val;
	int ret;

	mutex_lock(&iqs621_als->lock);

	ret = regmap_read(iqs62x->regmap, iqs62x->dev_desc->als_flags, &val);
	if (ret)
		goto err_mutex;
	iqs621_als->als_flags = val;

	switch (chan->type) {
	case IIO_LIGHT:
		ret = regmap_update_bits(iqs62x->regmap, IQS620_GLBL_EVENT_MASK,
					 iqs62x->dev_desc->als_mask,
					 iqs621_als->range_en || state ? 0 :
									 0xFF);
		if (!ret)
			iqs621_als->light_en = state;
		break;

	case IIO_INTENSITY:
		ret = regmap_update_bits(iqs62x->regmap, IQS620_GLBL_EVENT_MASK,
					 iqs62x->dev_desc->als_mask,
					 iqs621_als->light_en || state ? 0 :
									 0xFF);
		if (!ret)
			iqs621_als->range_en = state;
		break;

	case IIO_PROXIMITY:
		ret = regmap_read(iqs62x->regmap, IQS622_IR_FLAGS, &val);
		if (ret)
			goto err_mutex;
		iqs621_als->ir_flags = val;

		ret = regmap_update_bits(iqs62x->regmap, IQS620_GLBL_EVENT_MASK,
					 iqs62x->dev_desc->ir_mask,
					 state ? 0 : 0xFF);
		if (!ret)
			iqs621_als->prox_en = state;
		break;

	default:
		ret = -EINVAL;
	}

err_mutex:
	mutex_unlock(&iqs621_als->lock);

	return ret;
}

static int iqs621_als_read_event_value(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       enum iio_event_info info,
				       int *val, int *val2)
{
	struct iqs621_als_private *iqs621_als = iio_priv(indio_dev);
	int ret = IIO_VAL_INT;

	mutex_lock(&iqs621_als->lock);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		*val = iqs621_als->thresh_light * 16;
		break;

	case IIO_EV_DIR_FALLING:
		*val = iqs621_als->thresh_dark * 4;
		break;

	case IIO_EV_DIR_EITHER:
		if (iqs621_als->ir_flags_mask == IQS622_IR_FLAGS_TOUCH)
			*val = iqs621_als->thresh_prox * 4;
		else
			*val = iqs621_als->thresh_prox;
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&iqs621_als->lock);

	return ret;
}

static int iqs621_als_write_event_value(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir,
					enum iio_event_info info,
					int val, int val2)
{
	struct iqs621_als_private *iqs621_als = iio_priv(indio_dev);
	struct iqs62x_core *iqs62x = iqs621_als->iqs62x;
	unsigned int thresh_reg, thresh_val;
	u8 ir_flags_mask, *thresh_cache;
	int ret = -EINVAL;

	mutex_lock(&iqs621_als->lock);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		thresh_reg = IQS621_ALS_THRESH_LIGHT;
		thresh_val = val / 16;

		thresh_cache = &iqs621_als->thresh_light;
		ir_flags_mask = 0;
		break;

	case IIO_EV_DIR_FALLING:
		thresh_reg = IQS621_ALS_THRESH_DARK;
		thresh_val = val / 4;

		thresh_cache = &iqs621_als->thresh_dark;
		ir_flags_mask = 0;
		break;

	case IIO_EV_DIR_EITHER:
		/*
		 * The IQS622 supports two detection thresholds, both measured
		 * in the same arbitrary units reported by read_raw: proximity
		 * (0 through 255 in steps of 1), and touch (0 through 1020 in
		 * steps of 4).
		 *
		 * Based on the single detection threshold chosen by the user,
		 * select the hardware threshold that gives the best trade-off
		 * between range and resolution.
		 *
		 * By default, the close-range (but coarse) touch threshold is
		 * chosen during probe.
		 */
		switch (val) {
		case 0 ... 255:
			thresh_reg = IQS622_IR_THRESH_PROX;
			thresh_val = val;

			ir_flags_mask = IQS622_IR_FLAGS_PROX;
			break;

		case 256 ... 1020:
			thresh_reg = IQS622_IR_THRESH_TOUCH;
			thresh_val = val / 4;

			ir_flags_mask = IQS622_IR_FLAGS_TOUCH;
			break;

		default:
			goto err_mutex;
		}

		thresh_cache = &iqs621_als->thresh_prox;
		break;

	default:
		goto err_mutex;
	}

	if (thresh_val > 0xFF)
		goto err_mutex;

	ret = regmap_write(iqs62x->regmap, thresh_reg, thresh_val);
	if (ret)
		goto err_mutex;

	*thresh_cache = thresh_val;
	iqs621_als->ir_flags_mask = ir_flags_mask;

err_mutex:
	mutex_unlock(&iqs621_als->lock);

	return ret;
}

static const struct iio_info iqs621_als_info = {
	.read_raw = &iqs621_als_read_raw,
	.read_event_config = iqs621_als_read_event_config,
	.write_event_config = iqs621_als_write_event_config,
	.read_event_value = iqs621_als_read_event_value,
	.write_event_value = iqs621_als_write_event_value,
};

static const struct iio_event_spec iqs621_als_range_events[] = {
	{
		.type = IIO_EV_TYPE_CHANGE,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_event_spec iqs621_als_light_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_chan_spec iqs621_als_channels[] = {
	{
		.type = IIO_INTENSITY,
		.address = IQS621_ALS_FLAGS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = iqs621_als_range_events,
		.num_event_specs = ARRAY_SIZE(iqs621_als_range_events),
	},
	{
		.type = IIO_LIGHT,
		.address = IQS621_ALS_UI_OUT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.event_spec = iqs621_als_light_events,
		.num_event_specs = ARRAY_SIZE(iqs621_als_light_events),
	},
};

static const struct iio_event_spec iqs622_als_prox_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_chan_spec iqs622_als_channels[] = {
	{
		.type = IIO_INTENSITY,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.address = IQS622_ALS_FLAGS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = iqs621_als_range_events,
		.num_event_specs = ARRAY_SIZE(iqs621_als_range_events),
		.modified = true,
	},
	{
		.type = IIO_INTENSITY,
		.channel2 = IIO_MOD_LIGHT_IR,
		.address = IQS622_IR_RANGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.modified = true,
	},
	{
		.type = IIO_PROXIMITY,
		.address = IQS622_IR_UI_OUT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = iqs622_als_prox_events,
		.num_event_specs = ARRAY_SIZE(iqs622_als_prox_events),
	},
};

static int iqs621_als_probe(struct platform_device *pdev)
{
	struct iqs62x_core *iqs62x = dev_get_drvdata(pdev->dev.parent);
	struct iqs621_als_private *iqs621_als;
	struct iio_dev *indio_dev;
	unsigned int val;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*iqs621_als));
	if (!indio_dev)
		return -ENOMEM;

	iqs621_als = iio_priv(indio_dev);
	iqs621_als->iqs62x = iqs62x;
	iqs621_als->indio_dev = indio_dev;

	if (iqs62x->dev_desc->prod_num == IQS622_PROD_NUM) {
		ret = regmap_read(iqs62x->regmap, IQS622_IR_THRESH_TOUCH,
				  &val);
		if (ret)
			return ret;
		iqs621_als->thresh_prox = val;
		iqs621_als->ir_flags_mask = IQS622_IR_FLAGS_TOUCH;

		indio_dev->channels = iqs622_als_channels;
		indio_dev->num_channels = ARRAY_SIZE(iqs622_als_channels);
	} else {
		ret = regmap_read(iqs62x->regmap, IQS621_ALS_THRESH_LIGHT,
				  &val);
		if (ret)
			return ret;
		iqs621_als->thresh_light = val;

		ret = regmap_read(iqs62x->regmap, IQS621_ALS_THRESH_DARK,
				  &val);
		if (ret)
			return ret;
		iqs621_als->thresh_dark = val;

		indio_dev->channels = iqs621_als_channels;
		indio_dev->num_channels = ARRAY_SIZE(iqs621_als_channels);
	}

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = iqs62x->dev_desc->dev_name;
	indio_dev->info = &iqs621_als_info;

	mutex_init(&iqs621_als->lock);

	iqs621_als->notifier.notifier_call = iqs621_als_notifier;
	ret = blocking_notifier_chain_register(&iqs621_als->iqs62x->nh,
					       &iqs621_als->notifier);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register notifier: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&pdev->dev,
				       iqs621_als_notifier_unregister,
				       iqs621_als);
	if (ret)
		return ret;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static struct platform_driver iqs621_als_platform_driver = {
	.driver = {
		.name = "iqs621-als",
	},
	.probe = iqs621_als_probe,
};
module_platform_driver(iqs621_als_platform_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("Azoteq IQS621/622 Ambient Light Sensors");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:iqs621-als");

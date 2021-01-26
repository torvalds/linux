// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/hid-sensor-hub.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include "../common/hid-sensors/hid-sensor-trigger.h"

enum {
	CHANNEL_SCAN_INDEX_INTENSITY = 0,
	CHANNEL_SCAN_INDEX_ILLUM = 1,
	CHANNEL_SCAN_INDEX_MAX
};

struct als_state {
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info als_illum;
	u32 illum[CHANNEL_SCAN_INDEX_MAX];
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
};

/* Channel definitions */
static const struct iio_chan_spec als_channels[] = {
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_INTENSITY,
	},
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_ILLUM,
	}
};

/* Adjust channel real bits based on report descriptor */
static void als_adjust_channel_bit_mask(struct iio_chan_spec *channels,
					int channel, int size)
{
	channels[channel].scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	channels[channel].scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is u32 */
	channels[channel].scan_type.storagebits = sizeof(u32) * 8;
}

/* Channel read_raw handler */
static int als_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct als_state *als_state = iio_priv(indio_dev);
	int report_id = -1;
	u32 address;
	int ret_type;
	s32 min;

	*val = 0;
	*val2 = 0;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->scan_index) {
		case  CHANNEL_SCAN_INDEX_INTENSITY:
		case  CHANNEL_SCAN_INDEX_ILLUM:
			report_id = als_state->als_illum.report_id;
			min = als_state->als_illum.logical_minimum;
			address = HID_USAGE_SENSOR_LIGHT_ILLUM;
			break;
		default:
			report_id = -1;
			break;
		}
		if (report_id >= 0) {
			hid_sensor_power_state(&als_state->common_attributes,
						true);
			*val = sensor_hub_input_attr_get_raw_value(
					als_state->common_attributes.hsdev,
					HID_USAGE_SENSOR_ALS, address,
					report_id,
					SENSOR_HUB_SYNC,
					min < 0);
			hid_sensor_power_state(&als_state->common_attributes,
						false);
		} else {
			*val = 0;
			return -EINVAL;
		}
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = als_state->scale_pre_decml;
		*val2 = als_state->scale_post_decml;
		ret_type = als_state->scale_precision;
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = als_state->value_offset;
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret_type = hid_sensor_read_samp_freq_value(
				&als_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret_type = hid_sensor_read_raw_hyst_value(
				&als_state->common_attributes, val, val2);
		break;
	default:
		ret_type = -EINVAL;
		break;
	}

	return ret_type;
}

/* Channel write_raw handler */
static int als_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct als_state *als_state = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hid_sensor_write_samp_freq_value(
				&als_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret = hid_sensor_write_raw_hyst_value(
				&als_state->common_attributes, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info als_info = {
	.read_raw = &als_read_raw,
	.write_raw = &als_write_raw,
};

/* Function to push data to buffer */
static void hid_sensor_push_data(struct iio_dev *indio_dev, const void *data,
					int len)
{
	dev_dbg(&indio_dev->dev, "hid_sensor_push_data\n");
	iio_push_to_buffers(indio_dev, data);
}

/* Callback handler to send event after all samples are received and captured */
static int als_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct als_state *als_state = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "als_proc_event\n");
	if (atomic_read(&als_state->common_attributes.data_ready))
		hid_sensor_push_data(indio_dev,
				&als_state->illum,
				sizeof(als_state->illum));

	return 0;
}

/* Capture samples in local storage */
static int als_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				size_t raw_len, char *raw_data,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct als_state *als_state = iio_priv(indio_dev);
	int ret = -EINVAL;
	u32 sample_data = *(u32 *)raw_data;

	switch (usage_id) {
	case HID_USAGE_SENSOR_LIGHT_ILLUM:
		als_state->illum[CHANNEL_SCAN_INDEX_INTENSITY] = sample_data;
		als_state->illum[CHANNEL_SCAN_INDEX_ILLUM] = sample_data;
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

/* Parse report which is specific to an usage id*/
static int als_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec *channels,
				unsigned usage_id,
				struct als_state *st)
{
	int ret;

	ret = sensor_hub_input_get_attribute_info(hsdev, HID_INPUT_REPORT,
			usage_id,
			HID_USAGE_SENSOR_LIGHT_ILLUM,
			&st->als_illum);
	if (ret < 0)
		return ret;
	als_adjust_channel_bit_mask(channels, CHANNEL_SCAN_INDEX_INTENSITY,
				    st->als_illum.size);
	als_adjust_channel_bit_mask(channels, CHANNEL_SCAN_INDEX_ILLUM,
					st->als_illum.size);

	dev_dbg(&pdev->dev, "als %x:%x\n", st->als_illum.index,
			st->als_illum.report_id);

	st->scale_precision = hid_sensor_format_scale(
				HID_USAGE_SENSOR_ALS,
				&st->als_illum,
				&st->scale_pre_decml, &st->scale_post_decml);

	/* Set Sensitivity field ids, when there is no individual modifier */
	if (st->common_attributes.sensitivity.index < 0) {
		sensor_hub_input_get_attribute_info(hsdev,
			HID_FEATURE_REPORT, usage_id,
			HID_USAGE_SENSOR_DATA_MOD_CHANGE_SENSITIVITY_ABS |
			HID_USAGE_SENSOR_DATA_LIGHT,
			&st->common_attributes.sensitivity);
		dev_dbg(&pdev->dev, "Sensitivity index:report %d:%d\n",
			st->common_attributes.sensitivity.index,
			st->common_attributes.sensitivity.report_id);
	}
	return ret;
}

/* Function to initialize the processing for usage id */
static int hid_als_probe(struct platform_device *pdev)
{
	int ret = 0;
	static const char *name = "als";
	struct iio_dev *indio_dev;
	struct als_state *als_state;
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct als_state));
	if (!indio_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, indio_dev);

	als_state = iio_priv(indio_dev);
	als_state->common_attributes.hsdev = hsdev;
	als_state->common_attributes.pdev = pdev;

	ret = hid_sensor_parse_common_attributes(hsdev, HID_USAGE_SENSOR_ALS,
					&als_state->common_attributes);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}

	indio_dev->channels = kmemdup(als_channels,
				      sizeof(als_channels), GFP_KERNEL);
	if (!indio_dev->channels) {
		dev_err(&pdev->dev, "failed to duplicate channels\n");
		return -ENOMEM;
	}

	ret = als_parse_report(pdev, hsdev,
			       (struct iio_chan_spec *)indio_dev->channels,
			       HID_USAGE_SENSOR_ALS, als_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes\n");
		goto error_free_dev_mem;
	}

	indio_dev->num_channels =
				ARRAY_SIZE(als_channels);
	indio_dev->info = &als_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&als_state->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
				&als_state->common_attributes);
	if (ret < 0) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		goto error_free_dev_mem;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_trigger;
	}

	als_state->callbacks.send_event = als_proc_event;
	als_state->callbacks.capture_sample = als_capture_sample;
	als_state->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, HID_USAGE_SENSOR_ALS,
					&als_state->callbacks);
	if (ret < 0) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_iio_unreg;
	}

	return ret;

error_iio_unreg:
	iio_device_unregister(indio_dev);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &als_state->common_attributes);
error_free_dev_mem:
	kfree(indio_dev->channels);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_als_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct als_state *als_state = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_ALS);
	iio_device_unregister(indio_dev);
	hid_sensor_remove_trigger(indio_dev, &als_state->common_attributes);
	kfree(indio_dev->channels);

	return 0;
}

static const struct platform_device_id hid_als_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200041",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_als_ids);

static struct platform_driver hid_als_platform_driver = {
	.id_table = hid_als_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_als_probe,
	.remove		= hid_als_remove,
};
module_platform_driver(hid_als_platform_driver);

MODULE_DESCRIPTION("HID Sensor ALS");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Sensors Driver
 * Copyright (c) 2020, Intel Corporation.
 */
#include <linux/hid-sensor-hub.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include "../common/hid-sensors/hid-sensor-trigger.h"

enum hinge_channel {
	CHANNEL_SCAN_INDEX_HINGE_ANGLE,
	CHANNEL_SCAN_INDEX_SCREEN_ANGLE,
	CHANNEL_SCAN_INDEX_KEYBOARD_ANGLE,
	CHANNEL_SCAN_INDEX_MAX,
};

#define CHANNEL_SCAN_INDEX_TIMESTAMP CHANNEL_SCAN_INDEX_MAX

static const u32 hinge_addresses[CHANNEL_SCAN_INDEX_MAX] = {
	HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(1),
	HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(2),
	HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(3)
};

static const char *const hinge_labels[CHANNEL_SCAN_INDEX_MAX] = { "hinge",
								  "screen",
								  "keyboard" };

struct hinge_state {
	struct iio_dev *indio_dev;
	struct hid_sensor_hub_attribute_info hinge[CHANNEL_SCAN_INDEX_MAX];
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common common_attributes;
	const char *labels[CHANNEL_SCAN_INDEX_MAX];
	struct {
		u32 hinge_val[3];
		aligned_s64 timestamp;
	} scan;

	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
	u64 timestamp;
};

static const u32 hinge_sensitivity_addresses[] = {
	HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(1),
};

/* Channel definitions */
static const struct iio_chan_spec hinge_channels[] = {
	{
		.type = IIO_ANGL,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ) | BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_HINGE_ANGLE,
		.scan_type = {
			.sign = 's',
			.storagebits = 32,
		},
	}, {
		.type = IIO_ANGL,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ) | BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_SCREEN_ANGLE,
		.scan_type = {
			.sign = 's',
			.storagebits = 32,
		},
	}, {
		.type = IIO_ANGL,
		.indexed = 1,
		.channel = 2,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ) | BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_KEYBOARD_ANGLE,
		.scan_type = {
			.sign = 's',
			.storagebits = 32,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(CHANNEL_SCAN_INDEX_TIMESTAMP)
};

/* Adjust channel real bits based on report descriptor */
static void hinge_adjust_channel_realbits(struct iio_chan_spec *channels,
					  int channel, int size)
{
	channels[channel].scan_type.realbits = size * 8;
}

/* Channel read_raw handler */
static int hinge_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val, int *val2,
			  long mask)
{
	struct hinge_state *st = iio_priv(indio_dev);
	struct hid_sensor_hub_device *hsdev;
	int report_id;
	s32 min;

	hsdev = st->common_attributes.hsdev;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		hid_sensor_power_state(&st->common_attributes, true);
		report_id = st->hinge[chan->scan_index].report_id;
		min = st->hinge[chan->scan_index].logical_minimum;
		if (report_id < 0) {
			hid_sensor_power_state(&st->common_attributes, false);
			return -EINVAL;
		}

		*val = sensor_hub_input_attr_get_raw_value(st->common_attributes.hsdev,
							   hsdev->usage,
							   hinge_addresses[chan->scan_index],
							   report_id,
							   SENSOR_HUB_SYNC, min < 0);

		hid_sensor_power_state(&st->common_attributes, false);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->scale_pre_decml;
		*val2 = st->scale_post_decml;
		return st->scale_precision;
	case IIO_CHAN_INFO_OFFSET:
		*val = st->value_offset;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return hid_sensor_read_samp_freq_value(&st->common_attributes,
						       val, val2);
	case IIO_CHAN_INFO_HYSTERESIS:
		return hid_sensor_read_raw_hyst_value(&st->common_attributes,
						      val, val2);
	default:
		return -EINVAL;
	}
}

/* Channel write_raw handler */
static int hinge_write_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int val, int val2,
			   long mask)
{
	struct hinge_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return hid_sensor_write_samp_freq_value(&st->common_attributes,
							val, val2);
	case IIO_CHAN_INFO_HYSTERESIS:
		return hid_sensor_write_raw_hyst_value(&st->common_attributes,
						       val, val2);
	default:
		return -EINVAL;
	}
}

static int hinge_read_label(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, char *label)
{
	struct hinge_state *st = iio_priv(indio_dev);

	return sprintf(label, "%s\n", st->labels[chan->channel]);
}

static const struct iio_info hinge_info = {
	.read_raw = hinge_read_raw,
	.write_raw = hinge_write_raw,
	.read_label = hinge_read_label,
};

/*
 * Callback handler to send event after all samples are received
 * and captured.
 */
static int hinge_proc_event(struct hid_sensor_hub_device *hsdev,
			    unsigned int usage_id, void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct hinge_state *st = iio_priv(indio_dev);

	if (atomic_read(&st->common_attributes.data_ready)) {
		if (!st->timestamp)
			st->timestamp = iio_get_time_ns(indio_dev);

		iio_push_to_buffers_with_timestamp(indio_dev, &st->scan,
						   st->timestamp);

		st->timestamp = 0;
	}
	return 0;
}

/* Capture samples in local storage */
static int hinge_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned int usage_id, size_t raw_len,
				char *raw_data, void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct hinge_state *st = iio_priv(indio_dev);
	int offset;

	switch (usage_id) {
	case HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(1):
	case HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(2):
	case HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(3):
		offset = usage_id - HID_USAGE_SENSOR_DATA_FIELD_CUSTOM_VALUE(1);
		st->scan.hinge_val[offset] = *(u32 *)raw_data;
		return 0;
	case HID_USAGE_SENSOR_TIME_TIMESTAMP:
		st->timestamp = hid_sensor_convert_timestamp(&st->common_attributes,
							     *(int64_t *)raw_data);
		return 0;
	default:
		return -EINVAL;
	}
}

/* Parse report which is specific to an usage id */
static int hinge_parse_report(struct platform_device *pdev,
			      struct hid_sensor_hub_device *hsdev,
			      struct iio_chan_spec *channels,
			      unsigned int usage_id, struct hinge_state *st)
{
	int ret;
	int i;

	for (i = 0; i < CHANNEL_SCAN_INDEX_MAX; ++i) {
		ret = sensor_hub_input_get_attribute_info(hsdev,
							  HID_INPUT_REPORT,
							  usage_id,
							  hinge_addresses[i],
							  &st->hinge[i]);
		if (ret < 0)
			return ret;

		hinge_adjust_channel_realbits(channels, i, st->hinge[i].size);
	}

	st->scale_precision = hid_sensor_format_scale(HID_USAGE_SENSOR_HINGE,
			&st->hinge[CHANNEL_SCAN_INDEX_HINGE_ANGLE],
			&st->scale_pre_decml, &st->scale_post_decml);

	return ret;
}

/* Function to initialize the processing for usage id */
static int hid_hinge_probe(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	struct hinge_state *st;
	struct iio_dev *indio_dev;
	int ret;
	int i;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);

	st = iio_priv(indio_dev);
	st->common_attributes.hsdev = hsdev;
	st->common_attributes.pdev = pdev;
	st->indio_dev = indio_dev;
	for (i = 0; i < CHANNEL_SCAN_INDEX_MAX; i++)
		st->labels[i] = hinge_labels[i];

	ret = hid_sensor_parse_common_attributes(hsdev, hsdev->usage,
						 &st->common_attributes,
						 hinge_sensitivity_addresses,
						 ARRAY_SIZE(hinge_sensitivity_addresses));
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}

	indio_dev->num_channels = ARRAY_SIZE(hinge_channels);
	indio_dev->channels = devm_kmemdup(&indio_dev->dev, hinge_channels,
					   sizeof(hinge_channels), GFP_KERNEL);
	if (!indio_dev->channels)
		return -ENOMEM;

	ret = hinge_parse_report(pdev, hsdev,
				 (struct iio_chan_spec *)indio_dev->channels,
				 hsdev->usage, st);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes\n");
		return ret;
	}

	indio_dev->info = &hinge_info;
	indio_dev->name = "hinge";
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&st->common_attributes.data_ready, 0);
	ret = hid_sensor_setup_trigger(indio_dev, indio_dev->name,
				       &st->common_attributes);
	if (ret < 0) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		return ret;
	}

	st->callbacks.send_event = hinge_proc_event;
	st->callbacks.capture_sample = hinge_capture_sample;
	st->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, hsdev->usage, &st->callbacks);
	if (ret < 0) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_remove_trigger;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_callback;
	}

	return ret;

error_remove_callback:
	sensor_hub_remove_callback(hsdev, hsdev->usage);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &st->common_attributes);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static void hid_hinge_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hinge_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	sensor_hub_remove_callback(hsdev, hsdev->usage);
	hid_sensor_remove_trigger(indio_dev, &st->common_attributes);
}

static const struct platform_device_id hid_hinge_ids[] = {
	{
		/* Format: HID-SENSOR-INT-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-INT-020b",
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, hid_hinge_ids);

static struct platform_driver hid_hinge_platform_driver = {
	.id_table = hid_hinge_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_hinge_probe,
	.remove		= hid_hinge_remove,
};
module_platform_driver(hid_hinge_platform_driver);

MODULE_DESCRIPTION("HID Sensor INTEL Hinge");
MODULE_AUTHOR("Ye Xiang <xiang.ye@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_HID");

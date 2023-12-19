// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Sensors Driver
 * Copyright (c) 2017, Intel Corporation.
 */
#include <linux/device.h>
#include <linux/hid-sensor-hub.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "hid-sensor-trigger.h"

struct hid_humidity_state {
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info humidity_attr;
	struct {
		s32 humidity_data;
		u64 timestamp __aligned(8);
	} scan;
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
};

static const u32 humidity_sensitivity_addresses[] = {
	HID_USAGE_SENSOR_ATMOSPHERIC_HUMIDITY,
};

/* Channel definitions */
static const struct iio_chan_spec humidity_channels[] = {
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ) |
			BIT(IIO_CHAN_INFO_HYSTERESIS),
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

/* Adjust channel real bits based on report descriptor */
static void humidity_adjust_channel_bit_mask(struct iio_chan_spec *channels,
					int channel, int size)
{
	channels[channel].scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	channels[channel].scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is s32 */
	channels[channel].scan_type.storagebits = sizeof(s32) * 8;
}

static int humidity_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct hid_humidity_state *humid_st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_HUMIDITYRELATIVE)
			return -EINVAL;
		hid_sensor_power_state(&humid_st->common_attributes, true);
		*val = sensor_hub_input_attr_get_raw_value(
				humid_st->common_attributes.hsdev,
				HID_USAGE_SENSOR_HUMIDITY,
				HID_USAGE_SENSOR_ATMOSPHERIC_HUMIDITY,
				humid_st->humidity_attr.report_id,
				SENSOR_HUB_SYNC,
				humid_st->humidity_attr.logical_minimum < 0);
		hid_sensor_power_state(&humid_st->common_attributes, false);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = humid_st->scale_pre_decml;
		*val2 = humid_st->scale_post_decml;

		return humid_st->scale_precision;

	case IIO_CHAN_INFO_OFFSET:
		*val = humid_st->value_offset;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SAMP_FREQ:
		return hid_sensor_read_samp_freq_value(
				&humid_st->common_attributes, val, val2);

	case IIO_CHAN_INFO_HYSTERESIS:
		return hid_sensor_read_raw_hyst_value(
				&humid_st->common_attributes, val, val2);

	default:
		return -EINVAL;
	}
}

static int humidity_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct hid_humidity_state *humid_st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return hid_sensor_write_samp_freq_value(
				&humid_st->common_attributes, val, val2);

	case IIO_CHAN_INFO_HYSTERESIS:
		return hid_sensor_write_raw_hyst_value(
				&humid_st->common_attributes, val, val2);

	default:
		return -EINVAL;
	}
}

static const struct iio_info humidity_info = {
	.read_raw = &humidity_read_raw,
	.write_raw = &humidity_write_raw,
};

/* Callback handler to send event after all samples are received and captured */
static int humidity_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned int usage_id, void *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hid_humidity_state *humid_st = iio_priv(indio_dev);

	if (atomic_read(&humid_st->common_attributes.data_ready))
		iio_push_to_buffers_with_timestamp(indio_dev, &humid_st->scan,
						   iio_get_time_ns(indio_dev));

	return 0;
}

/* Capture samples in local storage */
static int humidity_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned int usage_id, size_t raw_len,
				char *raw_data, void *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hid_humidity_state *humid_st = iio_priv(indio_dev);

	switch (usage_id) {
	case HID_USAGE_SENSOR_ATMOSPHERIC_HUMIDITY:
		humid_st->scan.humidity_data = *(s32 *)raw_data;

		return 0;
	default:
		return -EINVAL;
	}
}

/* Parse report which is specific to an usage id */
static int humidity_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec *channels,
				unsigned int usage_id,
				struct hid_humidity_state *st)
{
	int ret;

	ret = sensor_hub_input_get_attribute_info(hsdev, HID_INPUT_REPORT,
					usage_id,
					HID_USAGE_SENSOR_ATMOSPHERIC_HUMIDITY,
					&st->humidity_attr);
	if (ret < 0)
		return ret;

	humidity_adjust_channel_bit_mask(channels, 0, st->humidity_attr.size);

	st->scale_precision = hid_sensor_format_scale(
						HID_USAGE_SENSOR_HUMIDITY,
						&st->humidity_attr,
						&st->scale_pre_decml,
						&st->scale_post_decml);

	return ret;
}

static struct hid_sensor_hub_callbacks humidity_callbacks = {
	.send_event = &humidity_proc_event,
	.capture_sample = &humidity_capture_sample,
};

/* Function to initialize the processing for usage id */
static int hid_humidity_probe(struct platform_device *pdev)
{
	static const char *name = "humidity";
	struct iio_dev *indio_dev;
	struct hid_humidity_state *humid_st;
	struct iio_chan_spec *humid_chans;
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*humid_st));
	if (!indio_dev)
		return -ENOMEM;

	humid_st = iio_priv(indio_dev);
	humid_st->common_attributes.hsdev = hsdev;
	humid_st->common_attributes.pdev = pdev;

	ret = hid_sensor_parse_common_attributes(hsdev,
					HID_USAGE_SENSOR_HUMIDITY,
					&humid_st->common_attributes,
					humidity_sensitivity_addresses,
					ARRAY_SIZE(humidity_sensitivity_addresses));
	if (ret)
		return ret;

	humid_chans = devm_kmemdup(&indio_dev->dev, humidity_channels,
					sizeof(humidity_channels), GFP_KERNEL);
	if (!humid_chans)
		return -ENOMEM;

	ret = humidity_parse_report(pdev, hsdev, humid_chans,
				HID_USAGE_SENSOR_HUMIDITY, humid_st);
	if (ret)
		return ret;

	indio_dev->channels = humid_chans;
	indio_dev->num_channels = ARRAY_SIZE(humidity_channels);
	indio_dev->info = &humidity_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&humid_st->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
				&humid_st->common_attributes);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	humidity_callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, HID_USAGE_SENSOR_HUMIDITY,
					&humidity_callbacks);
	if (ret)
		goto error_remove_trigger;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_remove_callback;

	return ret;

error_remove_callback:
	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_HUMIDITY);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &humid_st->common_attributes);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static void hid_humidity_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hid_humidity_state *humid_st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_HUMIDITY);
	hid_sensor_remove_trigger(indio_dev, &humid_st->common_attributes);
}

static const struct platform_device_id hid_humidity_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200032",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_humidity_ids);

static struct platform_driver hid_humidity_platform_driver = {
	.id_table = hid_humidity_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_humidity_probe,
	.remove_new	= hid_humidity_remove,
};
module_platform_driver(hid_humidity_platform_driver);

MODULE_DESCRIPTION("HID Environmental humidity sensor");
MODULE_AUTHOR("Song Hongyan <hongyan.song@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_HID);

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
#include <linux/platform_device.h>

#include "../common/hid-sensors/hid-sensor-trigger.h"

struct temperature_state {
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info temperature_attr;
	struct {
		s32 temperature_data;
		u64 timestamp __aligned(8);
	} scan;
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
};

static const u32 temperature_sensitivity_addresses[] = {
	HID_USAGE_SENSOR_DATA_ENVIRONMENTAL_TEMPERATURE,
};

/* Channel definitions */
static const struct iio_chan_spec temperature_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ) |
			BIT(IIO_CHAN_INFO_HYSTERESIS),
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

/* Adjust channel real bits based on report descriptor */
static void temperature_adjust_channel_bit_mask(struct iio_chan_spec *channels,
					int channel, int size)
{
	channels[channel].scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	channels[channel].scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is s32 */
	channels[channel].scan_type.storagebits = sizeof(s32) * 8;
}

static int temperature_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct temperature_state *temp_st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_TEMP)
			return -EINVAL;
		hid_sensor_power_state(
			&temp_st->common_attributes, true);
		*val = sensor_hub_input_attr_get_raw_value(
			temp_st->common_attributes.hsdev,
			HID_USAGE_SENSOR_TEMPERATURE,
			HID_USAGE_SENSOR_DATA_ENVIRONMENTAL_TEMPERATURE,
			temp_st->temperature_attr.report_id,
			SENSOR_HUB_SYNC,
			temp_st->temperature_attr.logical_minimum < 0);
		hid_sensor_power_state(
				&temp_st->common_attributes,
				false);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = temp_st->scale_pre_decml;
		*val2 = temp_st->scale_post_decml;
		return temp_st->scale_precision;

	case IIO_CHAN_INFO_OFFSET:
		*val = temp_st->value_offset;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SAMP_FREQ:
		return hid_sensor_read_samp_freq_value(
				&temp_st->common_attributes, val, val2);

	case IIO_CHAN_INFO_HYSTERESIS:
		return hid_sensor_read_raw_hyst_value(
				&temp_st->common_attributes, val, val2);
	default:
		return -EINVAL;
	}
}

static int temperature_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct temperature_state *temp_st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return hid_sensor_write_samp_freq_value(
				&temp_st->common_attributes, val, val2);
	case IIO_CHAN_INFO_HYSTERESIS:
		return hid_sensor_write_raw_hyst_value(
				&temp_st->common_attributes, val, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_info temperature_info = {
	.read_raw = &temperature_read_raw,
	.write_raw = &temperature_write_raw,
};

/* Callback handler to send event after all samples are received and captured */
static int temperature_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned int usage_id, void *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct temperature_state *temp_st = iio_priv(indio_dev);

	if (atomic_read(&temp_st->common_attributes.data_ready))
		iio_push_to_buffers_with_timestamp(indio_dev, &temp_st->scan,
						   iio_get_time_ns(indio_dev));

	return 0;
}

/* Capture samples in local storage */
static int temperature_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned int usage_id, size_t raw_len,
				char *raw_data, void *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct temperature_state *temp_st = iio_priv(indio_dev);

	switch (usage_id) {
	case HID_USAGE_SENSOR_DATA_ENVIRONMENTAL_TEMPERATURE:
		temp_st->scan.temperature_data = *(s32 *)raw_data;
		return 0;
	default:
		return -EINVAL;
	}
}

/* Parse report which is specific to an usage id*/
static int temperature_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec *channels,
				unsigned int usage_id,
				struct temperature_state *st)
{
	int ret;

	ret = sensor_hub_input_get_attribute_info(hsdev, HID_INPUT_REPORT,
			usage_id,
			HID_USAGE_SENSOR_DATA_ENVIRONMENTAL_TEMPERATURE,
			&st->temperature_attr);
	if (ret < 0)
		return ret;

	temperature_adjust_channel_bit_mask(channels, 0,
					st->temperature_attr.size);

	st->scale_precision = hid_sensor_format_scale(
				HID_USAGE_SENSOR_TEMPERATURE,
				&st->temperature_attr,
				&st->scale_pre_decml, &st->scale_post_decml);

	return ret;
}

static struct hid_sensor_hub_callbacks temperature_callbacks = {
	.send_event = &temperature_proc_event,
	.capture_sample = &temperature_capture_sample,
};

/* Function to initialize the processing for usage id */
static int hid_temperature_probe(struct platform_device *pdev)
{
	static const char *name = "temperature";
	struct iio_dev *indio_dev;
	struct temperature_state *temp_st;
	struct iio_chan_spec *temp_chans;
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*temp_st));
	if (!indio_dev)
		return -ENOMEM;

	temp_st = iio_priv(indio_dev);
	temp_st->common_attributes.hsdev = hsdev;
	temp_st->common_attributes.pdev = pdev;

	ret = hid_sensor_parse_common_attributes(hsdev,
					HID_USAGE_SENSOR_TEMPERATURE,
					&temp_st->common_attributes,
					temperature_sensitivity_addresses,
					ARRAY_SIZE(temperature_sensitivity_addresses));
	if (ret)
		return ret;

	temp_chans = devm_kmemdup(&indio_dev->dev, temperature_channels,
				sizeof(temperature_channels), GFP_KERNEL);
	if (!temp_chans)
		return -ENOMEM;

	ret = temperature_parse_report(pdev, hsdev, temp_chans,
				HID_USAGE_SENSOR_TEMPERATURE, temp_st);
	if (ret)
		return ret;

	indio_dev->channels = temp_chans;
	indio_dev->num_channels = ARRAY_SIZE(temperature_channels);
	indio_dev->info = &temperature_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&temp_st->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
				&temp_st->common_attributes);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	temperature_callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, HID_USAGE_SENSOR_TEMPERATURE,
					&temperature_callbacks);
	if (ret)
		goto error_remove_trigger;

	ret = devm_iio_device_register(indio_dev->dev.parent, indio_dev);
	if (ret)
		goto error_remove_callback;

	return ret;

error_remove_callback:
	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_TEMPERATURE);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &temp_st->common_attributes);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_temperature_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct temperature_state *temp_st = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_TEMPERATURE);
	hid_sensor_remove_trigger(indio_dev, &temp_st->common_attributes);

	return 0;
}

static const struct platform_device_id hid_temperature_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200033",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_temperature_ids);

static struct platform_driver hid_temperature_platform_driver = {
	.id_table = hid_temperature_ids,
	.driver = {
		.name	= "temperature-sensor",
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_temperature_probe,
	.remove		= hid_temperature_remove,
};
module_platform_driver(hid_temperature_platform_driver);

MODULE_DESCRIPTION("HID Environmental temperature sensor");
MODULE_AUTHOR("Song Hongyan <hongyan.song@intel.com>");
MODULE_LICENSE("GPL v2");

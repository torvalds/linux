// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/hid-sensor-hub.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include "../common/hid-sensors/hid-sensor-trigger.h"

enum gyro_3d_channel {
	CHANNEL_SCAN_INDEX_X,
	CHANNEL_SCAN_INDEX_Y,
	CHANNEL_SCAN_INDEX_Z,
	GYRO_3D_CHANNEL_MAX,
};

#define CHANNEL_SCAN_INDEX_TIMESTAMP GYRO_3D_CHANNEL_MAX
struct gyro_3d_state {
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info gyro[GYRO_3D_CHANNEL_MAX];
	struct {
		u32 gyro_val[GYRO_3D_CHANNEL_MAX];
		u64 timestamp __aligned(8);
	} scan;
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
	s64 timestamp;
};

static const u32 gyro_3d_addresses[GYRO_3D_CHANNEL_MAX] = {
	HID_USAGE_SENSOR_ANGL_VELOCITY_X_AXIS,
	HID_USAGE_SENSOR_ANGL_VELOCITY_Y_AXIS,
	HID_USAGE_SENSOR_ANGL_VELOCITY_Z_AXIS
};

static const u32 gryo_3d_sensitivity_addresses[] = {
	HID_USAGE_SENSOR_DATA_ANGL_VELOCITY,
};

/* Channel definitions */
static const struct iio_chan_spec gyro_3d_channels[] = {
	{
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_X,
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Y,
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Z,
	},
	IIO_CHAN_SOFT_TIMESTAMP(CHANNEL_SCAN_INDEX_TIMESTAMP)
};

/* Adjust channel real bits based on report descriptor */
static void gyro_3d_adjust_channel_bit_mask(struct iio_chan_spec *channels,
						int channel, int size)
{
	channels[channel].scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	channels[channel].scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is u32 */
	channels[channel].scan_type.storagebits = sizeof(u32) * 8;
}

/* Channel read_raw handler */
static int gyro_3d_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct gyro_3d_state *gyro_state = iio_priv(indio_dev);
	int report_id = -1;
	u32 address;
	int ret_type;
	s32 min;

	*val = 0;
	*val2 = 0;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		hid_sensor_power_state(&gyro_state->common_attributes, true);
		report_id = gyro_state->gyro[chan->scan_index].report_id;
		min = gyro_state->gyro[chan->scan_index].logical_minimum;
		address = gyro_3d_addresses[chan->scan_index];
		if (report_id >= 0)
			*val = sensor_hub_input_attr_get_raw_value(
					gyro_state->common_attributes.hsdev,
					HID_USAGE_SENSOR_GYRO_3D, address,
					report_id,
					SENSOR_HUB_SYNC,
					min < 0);
		else {
			*val = 0;
			hid_sensor_power_state(&gyro_state->common_attributes,
						false);
			return -EINVAL;
		}
		hid_sensor_power_state(&gyro_state->common_attributes, false);
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = gyro_state->scale_pre_decml;
		*val2 = gyro_state->scale_post_decml;
		ret_type = gyro_state->scale_precision;
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = gyro_state->value_offset;
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret_type = hid_sensor_read_samp_freq_value(
			&gyro_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret_type = hid_sensor_read_raw_hyst_value(
			&gyro_state->common_attributes, val, val2);
		break;
	default:
		ret_type = -EINVAL;
		break;
	}

	return ret_type;
}

/* Channel write_raw handler */
static int gyro_3d_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct gyro_3d_state *gyro_state = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hid_sensor_write_samp_freq_value(
				&gyro_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret = hid_sensor_write_raw_hyst_value(
				&gyro_state->common_attributes, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info gyro_3d_info = {
	.read_raw = &gyro_3d_read_raw,
	.write_raw = &gyro_3d_write_raw,
};

/* Callback handler to send event after all samples are received and captured */
static int gyro_3d_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct gyro_3d_state *gyro_state = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "gyro_3d_proc_event\n");
	if (atomic_read(&gyro_state->common_attributes.data_ready)) {
		if (!gyro_state->timestamp)
			gyro_state->timestamp = iio_get_time_ns(indio_dev);

		iio_push_to_buffers_with_timestamp(indio_dev, &gyro_state->scan,
						   gyro_state->timestamp);

		gyro_state->timestamp = 0;
	}

	return 0;
}

/* Capture samples in local storage */
static int gyro_3d_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				size_t raw_len, char *raw_data,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct gyro_3d_state *gyro_state = iio_priv(indio_dev);
	int offset;
	int ret = -EINVAL;

	switch (usage_id) {
	case HID_USAGE_SENSOR_ANGL_VELOCITY_X_AXIS:
	case HID_USAGE_SENSOR_ANGL_VELOCITY_Y_AXIS:
	case HID_USAGE_SENSOR_ANGL_VELOCITY_Z_AXIS:
		offset = usage_id - HID_USAGE_SENSOR_ANGL_VELOCITY_X_AXIS;
		gyro_state->scan.gyro_val[CHANNEL_SCAN_INDEX_X + offset] =
				*(u32 *)raw_data;
		ret = 0;
	break;
	case HID_USAGE_SENSOR_TIME_TIMESTAMP:
		gyro_state->timestamp =
			hid_sensor_convert_timestamp(&gyro_state->common_attributes,
						     *(s64 *)raw_data);
	break;
	default:
		break;
	}

	return ret;
}

/* Parse report which is specific to an usage id*/
static int gyro_3d_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec *channels,
				unsigned usage_id,
				struct gyro_3d_state *st)
{
	int ret;
	int i;

	for (i = 0; i <= CHANNEL_SCAN_INDEX_Z; ++i) {
		ret = sensor_hub_input_get_attribute_info(hsdev,
				HID_INPUT_REPORT,
				usage_id,
				HID_USAGE_SENSOR_ANGL_VELOCITY_X_AXIS + i,
				&st->gyro[CHANNEL_SCAN_INDEX_X + i]);
		if (ret < 0)
			break;
		gyro_3d_adjust_channel_bit_mask(channels,
				CHANNEL_SCAN_INDEX_X + i,
				st->gyro[CHANNEL_SCAN_INDEX_X + i].size);
	}
	dev_dbg(&pdev->dev, "gyro_3d %x:%x, %x:%x, %x:%x\n",
			st->gyro[0].index,
			st->gyro[0].report_id,
			st->gyro[1].index, st->gyro[1].report_id,
			st->gyro[2].index, st->gyro[2].report_id);

	st->scale_precision = hid_sensor_format_scale(
				HID_USAGE_SENSOR_GYRO_3D,
				&st->gyro[CHANNEL_SCAN_INDEX_X],
				&st->scale_pre_decml, &st->scale_post_decml);

	return ret;
}

/* Function to initialize the processing for usage id */
static int hid_gyro_3d_probe(struct platform_device *pdev)
{
	int ret = 0;
	static const char *name = "gyro_3d";
	struct iio_dev *indio_dev;
	struct gyro_3d_state *gyro_state;
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*gyro_state));
	if (!indio_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, indio_dev);

	gyro_state = iio_priv(indio_dev);
	gyro_state->common_attributes.hsdev = hsdev;
	gyro_state->common_attributes.pdev = pdev;

	ret = hid_sensor_parse_common_attributes(hsdev,
						HID_USAGE_SENSOR_GYRO_3D,
						&gyro_state->common_attributes,
						gryo_3d_sensitivity_addresses,
						ARRAY_SIZE(gryo_3d_sensitivity_addresses));
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}

	indio_dev->channels = devm_kmemdup(&pdev->dev, gyro_3d_channels,
					   sizeof(gyro_3d_channels), GFP_KERNEL);
	if (!indio_dev->channels) {
		dev_err(&pdev->dev, "failed to duplicate channels\n");
		return -ENOMEM;
	}

	ret = gyro_3d_parse_report(pdev, hsdev,
				   (struct iio_chan_spec *)indio_dev->channels,
				   HID_USAGE_SENSOR_GYRO_3D, gyro_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes\n");
		return ret;
	}

	indio_dev->num_channels = ARRAY_SIZE(gyro_3d_channels);
	indio_dev->info = &gyro_3d_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&gyro_state->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
					&gyro_state->common_attributes);
	if (ret < 0) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_trigger;
	}

	gyro_state->callbacks.send_event = gyro_3d_proc_event;
	gyro_state->callbacks.capture_sample = gyro_3d_capture_sample;
	gyro_state->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, HID_USAGE_SENSOR_GYRO_3D,
					&gyro_state->callbacks);
	if (ret < 0) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_iio_unreg;
	}

	return ret;

error_iio_unreg:
	iio_device_unregister(indio_dev);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &gyro_state->common_attributes);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_gyro_3d_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct gyro_3d_state *gyro_state = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_GYRO_3D);
	iio_device_unregister(indio_dev);
	hid_sensor_remove_trigger(indio_dev, &gyro_state->common_attributes);

	return 0;
}

static const struct platform_device_id hid_gyro_3d_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200076",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_gyro_3d_ids);

static struct platform_driver hid_gyro_3d_platform_driver = {
	.id_table = hid_gyro_3d_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_gyro_3d_probe,
	.remove		= hid_gyro_3d_remove,
};
module_platform_driver(hid_gyro_3d_platform_driver);

MODULE_DESCRIPTION("HID Sensor Gyroscope 3D");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_HID);

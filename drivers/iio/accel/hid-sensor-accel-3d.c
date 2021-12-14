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

enum accel_3d_channel {
	CHANNEL_SCAN_INDEX_X,
	CHANNEL_SCAN_INDEX_Y,
	CHANNEL_SCAN_INDEX_Z,
	ACCEL_3D_CHANNEL_MAX,
};

struct accel_3d_state {
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info accel[ACCEL_3D_CHANNEL_MAX];
	/* Ensure timestamp is naturally aligned */
	struct {
		u32 accel_val[3];
		s64 timestamp __aligned(8);
	} scan;
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
	int64_t timestamp;
};

static const u32 accel_3d_addresses[ACCEL_3D_CHANNEL_MAX] = {
	HID_USAGE_SENSOR_ACCEL_X_AXIS,
	HID_USAGE_SENSOR_ACCEL_Y_AXIS,
	HID_USAGE_SENSOR_ACCEL_Z_AXIS
};

/* Channel definitions */
static const struct iio_chan_spec accel_3d_channels[] = {
	{
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_X,
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Y,
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Z,
	},
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

/* Channel definitions */
static const struct iio_chan_spec gravity_channels[] = {
	{
		.type = IIO_GRAVITY,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_X,
	}, {
		.type = IIO_GRAVITY,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Y,
	}, {
		.type = IIO_GRAVITY,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Z,
	}
};

/* Adjust channel real bits based on report descriptor */
static void accel_3d_adjust_channel_bit_mask(struct iio_chan_spec *channels,
						int channel, int size)
{
	channels[channel].scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	channels[channel].scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is u32 */
	channels[channel].scan_type.storagebits = sizeof(u32) * 8;
}

/* Channel read_raw handler */
static int accel_3d_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct accel_3d_state *accel_state = iio_priv(indio_dev);
	int report_id = -1;
	u32 address;
	int ret_type;
	s32 min;
	struct hid_sensor_hub_device *hsdev =
					accel_state->common_attributes.hsdev;

	*val = 0;
	*val2 = 0;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		hid_sensor_power_state(&accel_state->common_attributes, true);
		report_id = accel_state->accel[chan->scan_index].report_id;
		min = accel_state->accel[chan->scan_index].logical_minimum;
		address = accel_3d_addresses[chan->scan_index];
		if (report_id >= 0)
			*val = sensor_hub_input_attr_get_raw_value(
					accel_state->common_attributes.hsdev,
					hsdev->usage, address, report_id,
					SENSOR_HUB_SYNC,
					min < 0);
		else {
			*val = 0;
			hid_sensor_power_state(&accel_state->common_attributes,
						 false);
			return -EINVAL;
		}
		hid_sensor_power_state(&accel_state->common_attributes, false);
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = accel_state->scale_pre_decml;
		*val2 = accel_state->scale_post_decml;
		ret_type = accel_state->scale_precision;
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = accel_state->value_offset;
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret_type = hid_sensor_read_samp_freq_value(
			&accel_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret_type = hid_sensor_read_raw_hyst_value(
			&accel_state->common_attributes, val, val2);
		break;
	default:
		ret_type = -EINVAL;
		break;
	}

	return ret_type;
}

/* Channel write_raw handler */
static int accel_3d_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct accel_3d_state *accel_state = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hid_sensor_write_samp_freq_value(
				&accel_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret = hid_sensor_write_raw_hyst_value(
				&accel_state->common_attributes, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info accel_3d_info = {
	.read_raw = &accel_3d_read_raw,
	.write_raw = &accel_3d_write_raw,
};

/* Function to push data to buffer */
static void hid_sensor_push_data(struct iio_dev *indio_dev, void *data,
				 int len, int64_t timestamp)
{
	dev_dbg(&indio_dev->dev, "hid_sensor_push_data\n");
	iio_push_to_buffers_with_timestamp(indio_dev, data, timestamp);
}

/* Callback handler to send event after all samples are received and captured */
static int accel_3d_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct accel_3d_state *accel_state = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "accel_3d_proc_event\n");
	if (atomic_read(&accel_state->common_attributes.data_ready)) {
		if (!accel_state->timestamp)
			accel_state->timestamp = iio_get_time_ns(indio_dev);

		hid_sensor_push_data(indio_dev,
				     &accel_state->scan,
				     sizeof(accel_state->scan),
				     accel_state->timestamp);

		accel_state->timestamp = 0;
	}

	return 0;
}

/* Capture samples in local storage */
static int accel_3d_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				size_t raw_len, char *raw_data,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct accel_3d_state *accel_state = iio_priv(indio_dev);
	int offset;
	int ret = -EINVAL;

	switch (usage_id) {
	case HID_USAGE_SENSOR_ACCEL_X_AXIS:
	case HID_USAGE_SENSOR_ACCEL_Y_AXIS:
	case HID_USAGE_SENSOR_ACCEL_Z_AXIS:
		offset = usage_id - HID_USAGE_SENSOR_ACCEL_X_AXIS;
		accel_state->scan.accel_val[CHANNEL_SCAN_INDEX_X + offset] =
						*(u32 *)raw_data;
		ret = 0;
	break;
	case HID_USAGE_SENSOR_TIME_TIMESTAMP:
		accel_state->timestamp =
			hid_sensor_convert_timestamp(
					&accel_state->common_attributes,
					*(int64_t *)raw_data);
	break;
	default:
		break;
	}

	return ret;
}

/* Parse report which is specific to an usage id*/
static int accel_3d_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec *channels,
				unsigned usage_id,
				struct accel_3d_state *st)
{
	int ret;
	int i;

	for (i = 0; i <= CHANNEL_SCAN_INDEX_Z; ++i) {
		ret = sensor_hub_input_get_attribute_info(hsdev,
				HID_INPUT_REPORT,
				usage_id,
				HID_USAGE_SENSOR_ACCEL_X_AXIS + i,
				&st->accel[CHANNEL_SCAN_INDEX_X + i]);
		if (ret < 0)
			break;
		accel_3d_adjust_channel_bit_mask(channels,
				CHANNEL_SCAN_INDEX_X + i,
				st->accel[CHANNEL_SCAN_INDEX_X + i].size);
	}
	dev_dbg(&pdev->dev, "accel_3d %x:%x, %x:%x, %x:%x\n",
			st->accel[0].index,
			st->accel[0].report_id,
			st->accel[1].index, st->accel[1].report_id,
			st->accel[2].index, st->accel[2].report_id);

	st->scale_precision = hid_sensor_format_scale(
				hsdev->usage,
				&st->accel[CHANNEL_SCAN_INDEX_X],
				&st->scale_pre_decml, &st->scale_post_decml);

	/* Set Sensitivity field ids, when there is no individual modifier */
	if (st->common_attributes.sensitivity.index < 0) {
		sensor_hub_input_get_attribute_info(hsdev,
			HID_FEATURE_REPORT, usage_id,
			HID_USAGE_SENSOR_DATA_MOD_CHANGE_SENSITIVITY_ABS |
			HID_USAGE_SENSOR_DATA_ACCELERATION,
			&st->common_attributes.sensitivity);
		dev_dbg(&pdev->dev, "Sensitivity index:report %d:%d\n",
			st->common_attributes.sensitivity.index,
			st->common_attributes.sensitivity.report_id);
	}

	return ret;
}

/* Function to initialize the processing for usage id */
static int hid_accel_3d_probe(struct platform_device *pdev)
{
	int ret = 0;
	const char *name;
	struct iio_dev *indio_dev;
	struct accel_3d_state *accel_state;
	const struct iio_chan_spec *channel_spec;
	int channel_size;

	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					  sizeof(struct accel_3d_state));
	if (indio_dev == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);

	accel_state = iio_priv(indio_dev);
	accel_state->common_attributes.hsdev = hsdev;
	accel_state->common_attributes.pdev = pdev;

	if (hsdev->usage == HID_USAGE_SENSOR_ACCEL_3D) {
		name = "accel_3d";
		channel_spec = accel_3d_channels;
		channel_size = sizeof(accel_3d_channels);
		indio_dev->num_channels = ARRAY_SIZE(accel_3d_channels);
	} else {
		name = "gravity";
		channel_spec = gravity_channels;
		channel_size = sizeof(gravity_channels);
		indio_dev->num_channels = ARRAY_SIZE(gravity_channels);
	}
	ret = hid_sensor_parse_common_attributes(hsdev, hsdev->usage,
					&accel_state->common_attributes);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}
	indio_dev->channels = kmemdup(channel_spec, channel_size, GFP_KERNEL);

	if (!indio_dev->channels) {
		dev_err(&pdev->dev, "failed to duplicate channels\n");
		return -ENOMEM;
	}
	ret = accel_3d_parse_report(pdev, hsdev,
				(struct iio_chan_spec *)indio_dev->channels,
				hsdev->usage, accel_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes\n");
		goto error_free_dev_mem;
	}

	indio_dev->info = &accel_3d_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&accel_state->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
					&accel_state->common_attributes);
	if (ret < 0) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		goto error_free_dev_mem;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_trigger;
	}

	accel_state->callbacks.send_event = accel_3d_proc_event;
	accel_state->callbacks.capture_sample = accel_3d_capture_sample;
	accel_state->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, hsdev->usage,
					&accel_state->callbacks);
	if (ret < 0) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_iio_unreg;
	}

	return ret;

error_iio_unreg:
	iio_device_unregister(indio_dev);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &accel_state->common_attributes);
error_free_dev_mem:
	kfree(indio_dev->channels);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_accel_3d_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct accel_3d_state *accel_state = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, hsdev->usage);
	iio_device_unregister(indio_dev);
	hid_sensor_remove_trigger(indio_dev, &accel_state->common_attributes);
	kfree(indio_dev->channels);

	return 0;
}

static const struct platform_device_id hid_accel_3d_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200073",
	},
	{	/* gravity sensor */
		.name = "HID-SENSOR-20007b",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_accel_3d_ids);

static struct platform_driver hid_accel_3d_platform_driver = {
	.id_table = hid_accel_3d_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_accel_3d_probe,
	.remove		= hid_accel_3d_remove,
};
module_platform_driver(hid_accel_3d_platform_driver);

MODULE_DESCRIPTION("HID Sensor Accel 3D");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_LICENSE("GPL");

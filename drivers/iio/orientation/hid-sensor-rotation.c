// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Sensors Driver
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/hid-sensor-hub.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include "../common/hid-sensors/hid-sensor-trigger.h"

struct dev_rot_state {
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info quaternion;
	struct {
		s32 sampled_vals[4] __aligned(16);
		u64 timestamp __aligned(8);
	} scan;
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
	s64 timestamp;
};

static const u32 rotation_sensitivity_addresses[] = {
	HID_USAGE_SENSOR_DATA_ORIENTATION,
	HID_USAGE_SENSOR_ORIENT_QUATERNION,
};

/* Channel definitions */
static const struct iio_chan_spec dev_rot_channels[] = {
	{
		.type = IIO_ROT,
		.modified = 1,
		.channel2 = IIO_MOD_QUATERNION,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) |
					BIT(IIO_CHAN_INFO_OFFSET) |
					BIT(IIO_CHAN_INFO_SCALE) |
					BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = 0
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

/* Adjust channel real bits based on report descriptor */
static void dev_rot_adjust_channel_bit_mask(struct iio_chan_spec *chan,
						int size)
{
	chan->scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	chan->scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is u32 */
	chan->scan_type.storagebits = sizeof(u32) * 8;
	chan->scan_type.repeat = 4;
}

/* Channel read_raw handler */
static int dev_rot_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int size, int *vals, int *val_len,
				long mask)
{
	struct dev_rot_state *rot_state = iio_priv(indio_dev);
	int ret_type;
	int i;

	vals[0] = 0;
	vals[1] = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (size >= 4) {
			for (i = 0; i < 4; ++i)
				vals[i] = rot_state->scan.sampled_vals[i];
			ret_type = IIO_VAL_INT_MULTIPLE;
			*val_len =  4;
		} else
			ret_type = -EINVAL;
		break;
	case IIO_CHAN_INFO_SCALE:
		vals[0] = rot_state->scale_pre_decml;
		vals[1] = rot_state->scale_post_decml;
		return rot_state->scale_precision;

	case IIO_CHAN_INFO_OFFSET:
		*vals = rot_state->value_offset;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SAMP_FREQ:
		ret_type = hid_sensor_read_samp_freq_value(
			&rot_state->common_attributes, &vals[0], &vals[1]);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret_type = hid_sensor_read_raw_hyst_value(
			&rot_state->common_attributes, &vals[0], &vals[1]);
		break;
	default:
		ret_type = -EINVAL;
		break;
	}

	return ret_type;
}

/* Channel write_raw handler */
static int dev_rot_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct dev_rot_state *rot_state = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hid_sensor_write_samp_freq_value(
				&rot_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret = hid_sensor_write_raw_hyst_value(
				&rot_state->common_attributes, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info dev_rot_info = {
	.read_raw_multi = &dev_rot_read_raw,
	.write_raw = &dev_rot_write_raw,
};

/* Callback handler to send event after all samples are received and captured */
static int dev_rot_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct dev_rot_state *rot_state = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "dev_rot_proc_event\n");
	if (atomic_read(&rot_state->common_attributes.data_ready)) {
		if (!rot_state->timestamp)
			rot_state->timestamp = iio_get_time_ns(indio_dev);

		iio_push_to_buffers_with_timestamp(indio_dev, &rot_state->scan,
						   rot_state->timestamp);

		rot_state->timestamp = 0;
	}

	return 0;
}

/* Capture samples in local storage */
static int dev_rot_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				size_t raw_len, char *raw_data,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct dev_rot_state *rot_state = iio_priv(indio_dev);

	if (usage_id == HID_USAGE_SENSOR_ORIENT_QUATERNION) {
		if (raw_len / 4 == sizeof(s16)) {
			rot_state->scan.sampled_vals[0] = ((s16 *)raw_data)[0];
			rot_state->scan.sampled_vals[1] = ((s16 *)raw_data)[1];
			rot_state->scan.sampled_vals[2] = ((s16 *)raw_data)[2];
			rot_state->scan.sampled_vals[3] = ((s16 *)raw_data)[3];
		} else {
			memcpy(&rot_state->scan.sampled_vals, raw_data,
			       sizeof(rot_state->scan.sampled_vals));
		}

		dev_dbg(&indio_dev->dev, "Recd Quat len:%zu::%zu\n", raw_len,
			sizeof(rot_state->scan.sampled_vals));
	} else if (usage_id == HID_USAGE_SENSOR_TIME_TIMESTAMP) {
		rot_state->timestamp = hid_sensor_convert_timestamp(&rot_state->common_attributes,
								    *(s64 *)raw_data);
	}

	return 0;
}

/* Parse report which is specific to an usage id*/
static int dev_rot_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec *channels,
				unsigned usage_id,
				struct dev_rot_state *st)
{
	int ret;

	ret = sensor_hub_input_get_attribute_info(hsdev,
				HID_INPUT_REPORT,
				usage_id,
				HID_USAGE_SENSOR_ORIENT_QUATERNION,
				&st->quaternion);
	if (ret)
		return ret;

	dev_rot_adjust_channel_bit_mask(&channels[0],
		st->quaternion.size / 4);

	dev_dbg(&pdev->dev, "dev_rot %x:%x\n", st->quaternion.index,
		st->quaternion.report_id);

	dev_dbg(&pdev->dev, "dev_rot: attrib size %d\n",
				st->quaternion.size);

	st->scale_precision = hid_sensor_format_scale(
				hsdev->usage,
				&st->quaternion,
				&st->scale_pre_decml, &st->scale_post_decml);

	return 0;
}

/* Function to initialize the processing for usage id */
static int hid_dev_rot_probe(struct platform_device *pdev)
{
	int ret;
	char *name;
	struct iio_dev *indio_dev;
	struct dev_rot_state *rot_state;
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					  sizeof(struct dev_rot_state));
	if (indio_dev == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);

	rot_state = iio_priv(indio_dev);
	rot_state->common_attributes.hsdev = hsdev;
	rot_state->common_attributes.pdev = pdev;

	switch (hsdev->usage) {
	case HID_USAGE_SENSOR_DEVICE_ORIENTATION:
		name = "dev_rotation";
		break;
	case HID_USAGE_SENSOR_RELATIVE_ORIENTATION:
		name = "relative_orientation";
		break;
	case HID_USAGE_SENSOR_GEOMAGNETIC_ORIENTATION:
		name = "geomagnetic_orientation";
		break;
	default:
		return -EINVAL;
	}

	ret = hid_sensor_parse_common_attributes(hsdev,
						 hsdev->usage,
						 &rot_state->common_attributes,
						 rotation_sensitivity_addresses,
						 ARRAY_SIZE(rotation_sensitivity_addresses));
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}

	indio_dev->channels = devm_kmemdup(&pdev->dev, dev_rot_channels,
					   sizeof(dev_rot_channels),
					   GFP_KERNEL);
	if (!indio_dev->channels) {
		dev_err(&pdev->dev, "failed to duplicate channels\n");
		return -ENOMEM;
	}

	ret = dev_rot_parse_report(pdev, hsdev,
				   (struct iio_chan_spec *)indio_dev->channels,
					hsdev->usage, rot_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes\n");
		return ret;
	}

	indio_dev->num_channels = ARRAY_SIZE(dev_rot_channels);
	indio_dev->info = &dev_rot_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&rot_state->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
					&rot_state->common_attributes);
	if (ret) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_trigger;
	}

	rot_state->callbacks.send_event = dev_rot_proc_event;
	rot_state->callbacks.capture_sample = dev_rot_capture_sample;
	rot_state->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, hsdev->usage,
					&rot_state->callbacks);
	if (ret) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_iio_unreg;
	}

	return 0;

error_iio_unreg:
	iio_device_unregister(indio_dev);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &rot_state->common_attributes);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_dev_rot_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct dev_rot_state *rot_state = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, hsdev->usage);
	iio_device_unregister(indio_dev);
	hid_sensor_remove_trigger(indio_dev, &rot_state->common_attributes);

	return 0;
}

static const struct platform_device_id hid_dev_rot_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-20008a",
	},
	{
		/* Relative orientation(AG) sensor */
		.name = "HID-SENSOR-20008e",
	},
	{
		/* Geomagnetic orientation(AM) sensor */
		.name = "HID-SENSOR-2000c1",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_dev_rot_ids);

static struct platform_driver hid_dev_rot_platform_driver = {
	.id_table = hid_dev_rot_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm     = &hid_sensor_pm_ops,
	},
	.probe		= hid_dev_rot_probe,
	.remove		= hid_dev_rot_remove,
};
module_platform_driver(hid_dev_rot_platform_driver);

MODULE_DESCRIPTION("HID Sensor Device Rotation");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL");

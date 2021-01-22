// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Sensors Driver
 * Copyright (c) 2013, Intel Corporation.
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

enum incl_3d_channel {
	CHANNEL_SCAN_INDEX_X,
	CHANNEL_SCAN_INDEX_Y,
	CHANNEL_SCAN_INDEX_Z,
	INCLI_3D_CHANNEL_MAX,
};

#define CHANNEL_SCAN_INDEX_TIMESTAMP INCLI_3D_CHANNEL_MAX

struct incl_3d_state {
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info incl[INCLI_3D_CHANNEL_MAX];
	struct {
		u32 incl_val[INCLI_3D_CHANNEL_MAX];
		u64 timestamp __aligned(8);
	} scan;
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
	s64 timestamp;
};

static const u32 incl_3d_addresses[INCLI_3D_CHANNEL_MAX] = {
	HID_USAGE_SENSOR_ORIENT_TILT_X,
	HID_USAGE_SENSOR_ORIENT_TILT_Y,
	HID_USAGE_SENSOR_ORIENT_TILT_Z
};

/* Channel definitions */
static const struct iio_chan_spec incl_3d_channels[] = {
	{
		.type = IIO_INCLI,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_X,
	}, {
		.type = IIO_INCLI,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Y,
	}, {
		.type = IIO_INCLI,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
		.scan_index = CHANNEL_SCAN_INDEX_Z,
	},
	IIO_CHAN_SOFT_TIMESTAMP(CHANNEL_SCAN_INDEX_TIMESTAMP),
};

/* Adjust channel real bits based on report descriptor */
static void incl_3d_adjust_channel_bit_mask(struct iio_chan_spec *chan,
						int size)
{
	chan->scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	chan->scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is u32 */
	chan->scan_type.storagebits = sizeof(u32) * 8;
}

/* Channel read_raw handler */
static int incl_3d_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct incl_3d_state *incl_state = iio_priv(indio_dev);
	int report_id = -1;
	u32 address;
	int ret_type;
	s32 min;

	*val = 0;
	*val2 = 0;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		hid_sensor_power_state(&incl_state->common_attributes, true);
		report_id = incl_state->incl[chan->scan_index].report_id;
		min = incl_state->incl[chan->scan_index].logical_minimum;
		address = incl_3d_addresses[chan->scan_index];
		if (report_id >= 0)
			*val = sensor_hub_input_attr_get_raw_value(
				incl_state->common_attributes.hsdev,
				HID_USAGE_SENSOR_INCLINOMETER_3D, address,
				report_id,
				SENSOR_HUB_SYNC,
				min < 0);
		else {
			hid_sensor_power_state(&incl_state->common_attributes,
						false);
			return -EINVAL;
		}
		hid_sensor_power_state(&incl_state->common_attributes, false);
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = incl_state->scale_pre_decml;
		*val2 = incl_state->scale_post_decml;
		ret_type = incl_state->scale_precision;
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = incl_state->value_offset;
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret_type = hid_sensor_read_samp_freq_value(
			&incl_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret_type = hid_sensor_read_raw_hyst_value(
			&incl_state->common_attributes, val, val2);
		break;
	default:
		ret_type = -EINVAL;
		break;
	}

	return ret_type;
}

/* Channel write_raw handler */
static int incl_3d_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct incl_3d_state *incl_state = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hid_sensor_write_samp_freq_value(
				&incl_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret = hid_sensor_write_raw_hyst_value(
				&incl_state->common_attributes, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info incl_3d_info = {
	.read_raw = &incl_3d_read_raw,
	.write_raw = &incl_3d_write_raw,
};

/* Callback handler to send event after all samples are received and captured */
static int incl_3d_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct incl_3d_state *incl_state = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "incl_3d_proc_event\n");
	if (atomic_read(&incl_state->common_attributes.data_ready)) {
		if (!incl_state->timestamp)
			incl_state->timestamp = iio_get_time_ns(indio_dev);

		iio_push_to_buffers_with_timestamp(indio_dev,
						   &incl_state->scan,
						   incl_state->timestamp);

		incl_state->timestamp = 0;
	}

	return 0;
}

/* Capture samples in local storage */
static int incl_3d_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				size_t raw_len, char *raw_data,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct incl_3d_state *incl_state = iio_priv(indio_dev);
	int ret = 0;

	switch (usage_id) {
	case HID_USAGE_SENSOR_ORIENT_TILT_X:
		incl_state->scan.incl_val[CHANNEL_SCAN_INDEX_X] = *(u32 *)raw_data;
	break;
	case HID_USAGE_SENSOR_ORIENT_TILT_Y:
		incl_state->scan.incl_val[CHANNEL_SCAN_INDEX_Y] = *(u32 *)raw_data;
	break;
	case HID_USAGE_SENSOR_ORIENT_TILT_Z:
		incl_state->scan.incl_val[CHANNEL_SCAN_INDEX_Z] = *(u32 *)raw_data;
	break;
	case HID_USAGE_SENSOR_TIME_TIMESTAMP:
		incl_state->timestamp =
			hid_sensor_convert_timestamp(&incl_state->common_attributes,
						     *(s64 *)raw_data);
	break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* Parse report which is specific to an usage id*/
static int incl_3d_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec *channels,
				unsigned usage_id,
				struct incl_3d_state *st)
{
	int ret;

	ret = sensor_hub_input_get_attribute_info(hsdev,
				HID_INPUT_REPORT,
				usage_id,
				HID_USAGE_SENSOR_ORIENT_TILT_X,
				&st->incl[CHANNEL_SCAN_INDEX_X]);
	if (ret)
		return ret;
	incl_3d_adjust_channel_bit_mask(&channels[CHANNEL_SCAN_INDEX_X],
				st->incl[CHANNEL_SCAN_INDEX_X].size);

	ret = sensor_hub_input_get_attribute_info(hsdev,
				HID_INPUT_REPORT,
				usage_id,
				HID_USAGE_SENSOR_ORIENT_TILT_Y,
				&st->incl[CHANNEL_SCAN_INDEX_Y]);
	if (ret)
		return ret;
	incl_3d_adjust_channel_bit_mask(&channels[CHANNEL_SCAN_INDEX_Y],
				st->incl[CHANNEL_SCAN_INDEX_Y].size);

	ret = sensor_hub_input_get_attribute_info(hsdev,
				HID_INPUT_REPORT,
				usage_id,
				HID_USAGE_SENSOR_ORIENT_TILT_Z,
				&st->incl[CHANNEL_SCAN_INDEX_Z]);
	if (ret)
		return ret;
	incl_3d_adjust_channel_bit_mask(&channels[CHANNEL_SCAN_INDEX_Z],
				st->incl[CHANNEL_SCAN_INDEX_Z].size);

	dev_dbg(&pdev->dev, "incl_3d %x:%x, %x:%x, %x:%x\n",
			st->incl[0].index,
			st->incl[0].report_id,
			st->incl[1].index, st->incl[1].report_id,
			st->incl[2].index, st->incl[2].report_id);

	st->scale_precision = hid_sensor_format_scale(
				HID_USAGE_SENSOR_INCLINOMETER_3D,
				&st->incl[CHANNEL_SCAN_INDEX_X],
				&st->scale_pre_decml, &st->scale_post_decml);

	/* Set Sensitivity field ids, when there is no individual modifier */
	if (st->common_attributes.sensitivity.index < 0) {
		sensor_hub_input_get_attribute_info(hsdev,
			HID_FEATURE_REPORT, usage_id,
			HID_USAGE_SENSOR_DATA_MOD_CHANGE_SENSITIVITY_ABS |
			HID_USAGE_SENSOR_DATA_ORIENTATION,
			&st->common_attributes.sensitivity);
		dev_dbg(&pdev->dev, "Sensitivity index:report %d:%d\n",
			st->common_attributes.sensitivity.index,
			st->common_attributes.sensitivity.report_id);
	}
	return ret;
}

/* Function to initialize the processing for usage id */
static int hid_incl_3d_probe(struct platform_device *pdev)
{
	int ret;
	static char *name = "incli_3d";
	struct iio_dev *indio_dev;
	struct incl_3d_state *incl_state;
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					  sizeof(struct incl_3d_state));
	if (indio_dev == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);

	incl_state = iio_priv(indio_dev);
	incl_state->common_attributes.hsdev = hsdev;
	incl_state->common_attributes.pdev = pdev;

	ret = hid_sensor_parse_common_attributes(hsdev,
				HID_USAGE_SENSOR_INCLINOMETER_3D,
				&incl_state->common_attributes);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}

	indio_dev->channels = kmemdup(incl_3d_channels,
				      sizeof(incl_3d_channels), GFP_KERNEL);
	if (!indio_dev->channels) {
		dev_err(&pdev->dev, "failed to duplicate channels\n");
		return -ENOMEM;
	}

	ret = incl_3d_parse_report(pdev, hsdev,
				   (struct iio_chan_spec *)indio_dev->channels,
				   HID_USAGE_SENSOR_INCLINOMETER_3D,
				   incl_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes\n");
		goto error_free_dev_mem;
	}

	indio_dev->num_channels = ARRAY_SIZE(incl_3d_channels);
	indio_dev->info = &incl_3d_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&incl_state->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
					&incl_state->common_attributes);
	if (ret) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		goto error_free_dev_mem;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_trigger;
	}

	incl_state->callbacks.send_event = incl_3d_proc_event;
	incl_state->callbacks.capture_sample = incl_3d_capture_sample;
	incl_state->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev,
					HID_USAGE_SENSOR_INCLINOMETER_3D,
					&incl_state->callbacks);
	if (ret) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_iio_unreg;
	}

	return 0;

error_iio_unreg:
	iio_device_unregister(indio_dev);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &incl_state->common_attributes);
error_free_dev_mem:
	kfree(indio_dev->channels);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_incl_3d_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct incl_3d_state *incl_state = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_INCLINOMETER_3D);
	iio_device_unregister(indio_dev);
	hid_sensor_remove_trigger(indio_dev, &incl_state->common_attributes);
	kfree(indio_dev->channels);

	return 0;
}

static const struct platform_device_id hid_incl_3d_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200086",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_incl_3d_ids);

static struct platform_driver hid_incl_3d_platform_driver = {
	.id_table = hid_incl_3d_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_incl_3d_probe,
	.remove		= hid_incl_3d_remove,
};
module_platform_driver(hid_incl_3d_platform_driver);

MODULE_DESCRIPTION("HID Sensor Inclinometer 3D");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL");

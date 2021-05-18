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

enum magn_3d_channel {
	CHANNEL_SCAN_INDEX_X,
	CHANNEL_SCAN_INDEX_Y,
	CHANNEL_SCAN_INDEX_Z,
	CHANNEL_SCAN_INDEX_NORTH_MAGN_TILT_COMP,
	CHANNEL_SCAN_INDEX_NORTH_TRUE_TILT_COMP,
	CHANNEL_SCAN_INDEX_NORTH_MAGN,
	CHANNEL_SCAN_INDEX_NORTH_TRUE,
	CHANNEL_SCAN_INDEX_TIMESTAMP,
	MAGN_3D_CHANNEL_MAX,
};

struct common_attributes {
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	int value_offset;
};

struct magn_3d_state {
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common magn_flux_attributes;
	struct hid_sensor_common rot_attributes;
	struct hid_sensor_hub_attribute_info magn[MAGN_3D_CHANNEL_MAX];

	/* dynamically sized array to hold sensor values */
	u32 *iio_vals;
	/* array of pointers to sensor value */
	u32 *magn_val_addr[MAGN_3D_CHANNEL_MAX];

	struct common_attributes magn_flux_attr;
	struct common_attributes rot_attr;
	s64 timestamp;
};

static const u32 magn_3d_addresses[MAGN_3D_CHANNEL_MAX] = {
	HID_USAGE_SENSOR_ORIENT_MAGN_FLUX_X_AXIS,
	HID_USAGE_SENSOR_ORIENT_MAGN_FLUX_Y_AXIS,
	HID_USAGE_SENSOR_ORIENT_MAGN_FLUX_Z_AXIS,
	HID_USAGE_SENSOR_ORIENT_COMP_MAGN_NORTH,
	HID_USAGE_SENSOR_ORIENT_COMP_TRUE_NORTH,
	HID_USAGE_SENSOR_ORIENT_MAGN_NORTH,
	HID_USAGE_SENSOR_ORIENT_TRUE_NORTH,
	HID_USAGE_SENSOR_TIME_TIMESTAMP,
};

static const u32 magn_3d_sensitivity_addresses[] = {
	HID_USAGE_SENSOR_DATA_ORIENTATION,
	HID_USAGE_SENSOR_ORIENT_MAGN_FLUX,
};

/* Channel definitions */
static const struct iio_chan_spec magn_3d_channels[] = {
	{
		.type = IIO_MAGN,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
	}, {
		.type = IIO_MAGN,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
	}, {
		.type = IIO_MAGN,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
	}, {
		.type = IIO_ROT,
		.modified = 1,
		.channel2 = IIO_MOD_NORTH_MAGN_TILT_COMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
	}, {
		.type = IIO_ROT,
		.modified = 1,
		.channel2 = IIO_MOD_NORTH_TRUE_TILT_COMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
	}, {
		.type = IIO_ROT,
		.modified = 1,
		.channel2 = IIO_MOD_NORTH_MAGN,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
	}, {
		.type = IIO_ROT,
		.modified = 1,
		.channel2 = IIO_MOD_NORTH_TRUE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_HYSTERESIS),
	},
	IIO_CHAN_SOFT_TIMESTAMP(7)
};

/* Adjust channel real bits based on report descriptor */
static void magn_3d_adjust_channel_bit_mask(struct iio_chan_spec *channels,
						int channel, int size)
{
	channels[channel].scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	channels[channel].scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is u32 */
	channels[channel].scan_type.storagebits = sizeof(u32) * 8;
}

/* Channel read_raw handler */
static int magn_3d_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct magn_3d_state *magn_state = iio_priv(indio_dev);
	int report_id = -1;
	u32 address;
	int ret_type;
	s32 min;

	*val = 0;
	*val2 = 0;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		hid_sensor_power_state(&magn_state->magn_flux_attributes, true);
		report_id = magn_state->magn[chan->address].report_id;
		min = magn_state->magn[chan->address].logical_minimum;
		address = magn_3d_addresses[chan->address];
		if (report_id >= 0)
			*val = sensor_hub_input_attr_get_raw_value(
				magn_state->magn_flux_attributes.hsdev,
				HID_USAGE_SENSOR_COMPASS_3D, address,
				report_id,
				SENSOR_HUB_SYNC,
				min < 0);
		else {
			*val = 0;
			hid_sensor_power_state(
				&magn_state->magn_flux_attributes,
				false);
			return -EINVAL;
		}
		hid_sensor_power_state(&magn_state->magn_flux_attributes,
					false);
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_MAGN:
			*val = magn_state->magn_flux_attr.scale_pre_decml;
			*val2 = magn_state->magn_flux_attr.scale_post_decml;
			ret_type = magn_state->magn_flux_attr.scale_precision;
			break;
		case IIO_ROT:
			*val = magn_state->rot_attr.scale_pre_decml;
			*val2 = magn_state->rot_attr.scale_post_decml;
			ret_type = magn_state->rot_attr.scale_precision;
			break;
		default:
			ret_type = -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_MAGN:
			*val = magn_state->magn_flux_attr.value_offset;
			ret_type = IIO_VAL_INT;
			break;
		case IIO_ROT:
			*val = magn_state->rot_attr.value_offset;
			ret_type = IIO_VAL_INT;
			break;
		default:
			ret_type = -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret_type = hid_sensor_read_samp_freq_value(
			&magn_state->magn_flux_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		switch (chan->type) {
		case IIO_MAGN:
			ret_type = hid_sensor_read_raw_hyst_value(
				&magn_state->magn_flux_attributes, val, val2);
			break;
		case IIO_ROT:
			ret_type = hid_sensor_read_raw_hyst_value(
				&magn_state->rot_attributes, val, val2);
			break;
		default:
			ret_type = -EINVAL;
		}
		break;
	default:
		ret_type = -EINVAL;
		break;
	}

	return ret_type;
}

/* Channel write_raw handler */
static int magn_3d_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct magn_3d_state *magn_state = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hid_sensor_write_samp_freq_value(
				&magn_state->magn_flux_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		switch (chan->type) {
		case IIO_MAGN:
			ret = hid_sensor_write_raw_hyst_value(
				&magn_state->magn_flux_attributes, val, val2);
			break;
		case IIO_ROT:
			ret = hid_sensor_write_raw_hyst_value(
				&magn_state->rot_attributes, val, val2);
			break;
		default:
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info magn_3d_info = {
	.read_raw = &magn_3d_read_raw,
	.write_raw = &magn_3d_write_raw,
};

/* Callback handler to send event after all samples are received and captured */
static int magn_3d_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct magn_3d_state *magn_state = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "magn_3d_proc_event\n");
	if (atomic_read(&magn_state->magn_flux_attributes.data_ready)) {
		if (!magn_state->timestamp)
			magn_state->timestamp = iio_get_time_ns(indio_dev);

		iio_push_to_buffers_with_timestamp(indio_dev,
						   magn_state->iio_vals,
						   magn_state->timestamp);
		magn_state->timestamp = 0;
	}

	return 0;
}

/* Capture samples in local storage */
static int magn_3d_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				size_t raw_len, char *raw_data,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct magn_3d_state *magn_state = iio_priv(indio_dev);
	int offset;
	int ret = 0;
	u32 *iio_val = NULL;

	switch (usage_id) {
	case HID_USAGE_SENSOR_ORIENT_MAGN_FLUX_X_AXIS:
	case HID_USAGE_SENSOR_ORIENT_MAGN_FLUX_Y_AXIS:
	case HID_USAGE_SENSOR_ORIENT_MAGN_FLUX_Z_AXIS:
		offset = (usage_id - HID_USAGE_SENSOR_ORIENT_MAGN_FLUX_X_AXIS)
				+ CHANNEL_SCAN_INDEX_X;
	break;
	case HID_USAGE_SENSOR_ORIENT_COMP_MAGN_NORTH:
	case HID_USAGE_SENSOR_ORIENT_COMP_TRUE_NORTH:
	case HID_USAGE_SENSOR_ORIENT_MAGN_NORTH:
	case HID_USAGE_SENSOR_ORIENT_TRUE_NORTH:
		offset = (usage_id - HID_USAGE_SENSOR_ORIENT_COMP_MAGN_NORTH)
				+ CHANNEL_SCAN_INDEX_NORTH_MAGN_TILT_COMP;
	break;
	case HID_USAGE_SENSOR_TIME_TIMESTAMP:
		magn_state->timestamp =
			hid_sensor_convert_timestamp(&magn_state->magn_flux_attributes,
						     *(s64 *)raw_data);
		return ret;
	default:
		return -EINVAL;
	}

	iio_val = magn_state->magn_val_addr[offset];

	if (iio_val != NULL)
		*iio_val = *((u32 *)raw_data);
	else
		ret = -EINVAL;

	return ret;
}

/* Parse report which is specific to an usage id*/
static int magn_3d_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct iio_chan_spec **channels,
				int *chan_count,
				unsigned usage_id,
				struct magn_3d_state *st)
{
	int i;
	int attr_count = 0;
	struct iio_chan_spec *_channels;

	/* Scan for each usage attribute supported */
	for (i = 0; i < MAGN_3D_CHANNEL_MAX; i++) {
		int status;
		u32 address = magn_3d_addresses[i];

		/* Check if usage attribute exists in the sensor hub device */
		status = sensor_hub_input_get_attribute_info(hsdev,
			HID_INPUT_REPORT,
			usage_id,
			address,
			&(st->magn[i]));
		if (!status)
			attr_count++;
	}

	if (attr_count <= 0) {
		dev_err(&pdev->dev,
			"failed to find any supported usage attributes in report\n");
		return  -EINVAL;
	}

	dev_dbg(&pdev->dev, "magn_3d Found %d usage attributes\n",
			attr_count);
	dev_dbg(&pdev->dev, "magn_3d X: %x:%x Y: %x:%x Z: %x:%x\n",
			st->magn[0].index,
			st->magn[0].report_id,
			st->magn[1].index, st->magn[1].report_id,
			st->magn[2].index, st->magn[2].report_id);

	/* Setup IIO channel array */
	_channels = devm_kcalloc(&pdev->dev, attr_count,
				sizeof(struct iio_chan_spec),
				GFP_KERNEL);
	if (!_channels) {
		dev_err(&pdev->dev,
			"failed to allocate space for iio channels\n");
		return -ENOMEM;
	}

	/* attr_count include timestamp channel, and the iio_vals should be aligned to 8byte */
	st->iio_vals = devm_kcalloc(&pdev->dev,
				    ((attr_count + 1) % 2 + (attr_count + 1) / 2) * 2,
				    sizeof(u32), GFP_KERNEL);
	if (!st->iio_vals) {
		dev_err(&pdev->dev,
			"failed to allocate space for iio values array\n");
		return -ENOMEM;
	}

	for (i = 0, *chan_count = 0;
	i < MAGN_3D_CHANNEL_MAX && *chan_count < attr_count;
	i++){
		if (st->magn[i].index >= 0) {
			/* Setup IIO channel struct */
			(_channels[*chan_count]) = magn_3d_channels[i];
			(_channels[*chan_count]).scan_index = *chan_count;
			(_channels[*chan_count]).address = i;

			if (i != CHANNEL_SCAN_INDEX_TIMESTAMP) {
				/* Set magn_val_addr to iio value address */
				st->magn_val_addr[i] = &st->iio_vals[*chan_count];
				magn_3d_adjust_channel_bit_mask(_channels,
								*chan_count,
								st->magn[i].size);
			}
			(*chan_count)++;
		}
	}

	if (*chan_count <= 0) {
		dev_err(&pdev->dev,
			"failed to find any magnetic channels setup\n");
		return -EINVAL;
	}

	*channels = _channels;

	dev_dbg(&pdev->dev, "magn_3d Setup %d IIO channels\n",
			*chan_count);

	st->magn_flux_attr.scale_precision = hid_sensor_format_scale(
				HID_USAGE_SENSOR_COMPASS_3D,
				&st->magn[CHANNEL_SCAN_INDEX_X],
				&st->magn_flux_attr.scale_pre_decml,
				&st->magn_flux_attr.scale_post_decml);
	st->rot_attr.scale_precision
		= hid_sensor_format_scale(
			HID_USAGE_SENSOR_ORIENT_COMP_MAGN_NORTH,
			&st->magn[CHANNEL_SCAN_INDEX_NORTH_MAGN_TILT_COMP],
			&st->rot_attr.scale_pre_decml,
			&st->rot_attr.scale_post_decml);

	if (st->rot_attributes.sensitivity.index < 0) {
		sensor_hub_input_get_attribute_info(hsdev,
			HID_FEATURE_REPORT, usage_id,
			HID_USAGE_SENSOR_DATA_MOD_CHANGE_SENSITIVITY_ABS |
			HID_USAGE_SENSOR_ORIENT_COMP_MAGN_NORTH,
			&st->rot_attributes.sensitivity);
		dev_dbg(&pdev->dev, "Sensitivity index:report %d:%d\n",
			st->rot_attributes.sensitivity.index,
			st->rot_attributes.sensitivity.report_id);
	}

	return 0;
}

/* Function to initialize the processing for usage id */
static int hid_magn_3d_probe(struct platform_device *pdev)
{
	int ret = 0;
	static char *name = "magn_3d";
	struct iio_dev *indio_dev;
	struct magn_3d_state *magn_state;
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	struct iio_chan_spec *channels;
	int chan_count = 0;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					  sizeof(struct magn_3d_state));
	if (indio_dev == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);

	magn_state = iio_priv(indio_dev);
	magn_state->magn_flux_attributes.hsdev = hsdev;
	magn_state->magn_flux_attributes.pdev = pdev;

	ret = hid_sensor_parse_common_attributes(hsdev,
				HID_USAGE_SENSOR_COMPASS_3D,
				&magn_state->magn_flux_attributes,
				magn_3d_sensitivity_addresses,
				ARRAY_SIZE(magn_3d_sensitivity_addresses));
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}
	magn_state->rot_attributes = magn_state->magn_flux_attributes;
	/* sensitivity of rot_attribute is not the same as magn_flux_attributes */
	magn_state->rot_attributes.sensitivity.index = -1;

	ret = magn_3d_parse_report(pdev, hsdev,
				&channels, &chan_count,
				HID_USAGE_SENSOR_COMPASS_3D, magn_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to parse report\n");
		return ret;
	}

	indio_dev->channels = channels;
	indio_dev->num_channels = chan_count;
	indio_dev->info = &magn_3d_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&magn_state->magn_flux_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
					&magn_state->magn_flux_attributes);
	if (ret < 0) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_trigger;
	}

	magn_state->callbacks.send_event = magn_3d_proc_event;
	magn_state->callbacks.capture_sample = magn_3d_capture_sample;
	magn_state->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, HID_USAGE_SENSOR_COMPASS_3D,
					&magn_state->callbacks);
	if (ret < 0) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_iio_unreg;
	}

	return ret;

error_iio_unreg:
	iio_device_unregister(indio_dev);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &magn_state->magn_flux_attributes);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_magn_3d_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct magn_3d_state *magn_state = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_COMPASS_3D);
	iio_device_unregister(indio_dev);
	hid_sensor_remove_trigger(indio_dev, &magn_state->magn_flux_attributes);

	return 0;
}

static const struct platform_device_id hid_magn_3d_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200083",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_magn_3d_ids);

static struct platform_driver hid_magn_3d_platform_driver = {
	.id_table = hid_magn_3d_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_magn_3d_probe,
	.remove		= hid_magn_3d_remove,
};
module_platform_driver(hid_magn_3d_platform_driver);

MODULE_DESCRIPTION("HID Sensor Magnetometer 3D");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Sensors Driver
 * Copyright (c) 2014, Intel Corporation.
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

static const u32 prox_usage_ids[] = {
	HID_USAGE_SENSOR_HUMAN_PRESENCE,
	HID_USAGE_SENSOR_HUMAN_PROXIMITY,
	HID_USAGE_SENSOR_HUMAN_ATTENTION,
};

#define MAX_CHANNELS ARRAY_SIZE(prox_usage_ids)

enum {
	HID_HUMAN_PRESENCE,
	HID_HUMAN_PROXIMITY,
	HID_HUMAN_ATTENTION,
};

struct prox_state {
	struct hid_sensor_hub_callbacks callbacks;
	struct hid_sensor_common common_attributes;
	struct hid_sensor_hub_attribute_info prox_attr[MAX_CHANNELS];
	struct iio_chan_spec channels[MAX_CHANNELS];
	u32 channel2usage[MAX_CHANNELS];
	u32 human_presence[MAX_CHANNELS];
	int scale_pre_decml;
	int scale_post_decml;
	int scale_precision;
	unsigned long scan_mask[2]; /* One entry plus one terminator. */
	int num_channels;
};

static const u32 prox_sensitivity_addresses[] = {
	HID_USAGE_SENSOR_HUMAN_PRESENCE,
	HID_USAGE_SENSOR_DATA_PRESENCE,
};

#define PROX_CHANNEL(_is_proximity, _channel) \
	{\
		.type = _is_proximity ? IIO_PROXIMITY : IIO_ATTENTION,\
		.info_mask_separate = \
		(_is_proximity ? BIT(IIO_CHAN_INFO_RAW) :\
				BIT(IIO_CHAN_INFO_PROCESSED)) |\
		BIT(IIO_CHAN_INFO_OFFSET) |\
		BIT(IIO_CHAN_INFO_SCALE) |\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |\
		BIT(IIO_CHAN_INFO_HYSTERESIS),\
		.indexed = _is_proximity,\
		.channel = _channel,\
	}

/* Channel definitions (same order as prox_usage_ids) */
static const struct iio_chan_spec prox_channels[] = {
	PROX_CHANNEL(true, HID_HUMAN_PRESENCE),
	PROX_CHANNEL(true, HID_HUMAN_PROXIMITY),
	PROX_CHANNEL(false, 0),
};

/* Adjust channel real bits based on report descriptor */
static void prox_adjust_channel_bit_mask(struct iio_chan_spec *channels,
					int channel, int size)
{
	channels[channel].scan_type.sign = 's';
	/* Real storage bits will change based on the report desc. */
	channels[channel].scan_type.realbits = size * 8;
	/* Maximum size of a sample to capture is u32 */
	channels[channel].scan_type.storagebits = sizeof(u32) * 8;
}

/* Channel read_raw handler */
static int prox_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	struct prox_state *prox_state = iio_priv(indio_dev);
	struct hid_sensor_hub_device *hsdev;
	int report_id;
	u32 address;
	int ret_type;
	s32 min;

	*val = 0;
	*val2 = 0;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		if (chan->scan_index >= prox_state->num_channels)
			return -EINVAL;
		address = prox_state->channel2usage[chan->scan_index];
		report_id = prox_state->prox_attr[chan->scan_index].report_id;
		hsdev = prox_state->common_attributes.hsdev;
		min = prox_state->prox_attr[chan->scan_index].logical_minimum;
		hid_sensor_power_state(&prox_state->common_attributes, true);
		*val = sensor_hub_input_attr_get_raw_value(hsdev,
							   hsdev->usage,
							   address,
							   report_id,
							   SENSOR_HUB_SYNC,
							   min < 0);
		if (prox_state->channel2usage[chan->scan_index] ==
		    HID_USAGE_SENSOR_HUMAN_ATTENTION)
			*val *= 100;
		hid_sensor_power_state(&prox_state->common_attributes, false);
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = prox_state->scale_pre_decml;
		*val2 = prox_state->scale_post_decml;
		ret_type = prox_state->scale_precision;
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = hid_sensor_convert_exponent(
			prox_state->prox_attr[chan->scan_index].unit_expo);
		ret_type = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret_type = hid_sensor_read_samp_freq_value(
				&prox_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret_type = hid_sensor_read_raw_hyst_value(
				&prox_state->common_attributes, val, val2);
		break;
	default:
		ret_type = -EINVAL;
		break;
	}

	return ret_type;
}

/* Channel write_raw handler */
static int prox_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct prox_state *prox_state = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = hid_sensor_write_samp_freq_value(
				&prox_state->common_attributes, val, val2);
		break;
	case IIO_CHAN_INFO_HYSTERESIS:
		ret = hid_sensor_write_raw_hyst_value(
				&prox_state->common_attributes, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info prox_info = {
	.read_raw = &prox_read_raw,
	.write_raw = &prox_write_raw,
};

/* Callback handler to send event after all samples are received and captured */
static int prox_proc_event(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct prox_state *prox_state = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "prox_proc_event\n");
	if (atomic_read(&prox_state->common_attributes.data_ready)) {
		dev_dbg(&indio_dev->dev, "hid_sensor_push_data\n");
		iio_push_to_buffers(indio_dev, &prox_state->human_presence);
	}

	return 0;
}

/* Capture samples in local storage */
static int prox_capture_sample(struct hid_sensor_hub_device *hsdev,
				unsigned usage_id,
				size_t raw_len, char *raw_data,
				void *priv)
{
	struct iio_dev *indio_dev = platform_get_drvdata(priv);
	struct prox_state *prox_state = iio_priv(indio_dev);
	int multiplier = 1;
	int chan;

	for (chan = 0; chan < prox_state->num_channels; chan++)
		if (prox_state->channel2usage[chan] == usage_id)
			break;
	if (chan == prox_state->num_channels)
		return -EINVAL;

	if (usage_id == HID_USAGE_SENSOR_HUMAN_ATTENTION)
		multiplier = 100;

	switch (raw_len) {
	case 1:
		prox_state->human_presence[chan] = *(u8 *)raw_data * multiplier;
		return 0;
	case 4:
		prox_state->human_presence[chan] = *(u32 *)raw_data * multiplier;
		return 0;
	}

	return -EINVAL;
}

/* Parse report which is specific to an usage id*/
static int prox_parse_report(struct platform_device *pdev,
				struct hid_sensor_hub_device *hsdev,
				struct prox_state *st)
{
	struct iio_chan_spec *channels = st->channels;
	int index = 0;
	int ret;
	int i;

	for (i = 0; i < MAX_CHANNELS; i++) {
		u32 usage_id = prox_usage_ids[i];

		ret = sensor_hub_input_get_attribute_info(hsdev,
							  HID_INPUT_REPORT,
							  hsdev->usage,
							  usage_id,
							  &st->prox_attr[index]);
		if (ret < 0)
			continue;
		st->channel2usage[index] = usage_id;
		st->scan_mask[0] |= BIT(index);
		channels[index] = prox_channels[i];
		channels[index].scan_index = index;
		prox_adjust_channel_bit_mask(channels, index,
					     st->prox_attr[index].size);
		dev_dbg(&pdev->dev, "prox %x:%x\n", st->prox_attr[index].index,
			st->prox_attr[index].report_id);
		index++;
	}

	if (!index)
		return ret;

	st->num_channels = index;

	return 0;
}

/* Function to initialize the processing for usage id */
static int hid_prox_probe(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	int ret = 0;
	static const char *name = "prox";
	struct iio_dev *indio_dev;
	struct prox_state *prox_state;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
				sizeof(struct prox_state));
	if (!indio_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, indio_dev);

	prox_state = iio_priv(indio_dev);
	prox_state->common_attributes.hsdev = hsdev;
	prox_state->common_attributes.pdev = pdev;

	ret = hid_sensor_parse_common_attributes(hsdev, hsdev->usage,
					&prox_state->common_attributes,
					prox_sensitivity_addresses,
					ARRAY_SIZE(prox_sensitivity_addresses));
	if (ret) {
		dev_err(&pdev->dev, "failed to setup common attributes\n");
		return ret;
	}

	ret = prox_parse_report(pdev, hsdev, prox_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes\n");
		return ret;
	}

	indio_dev->num_channels = prox_state->num_channels;
	indio_dev->channels = prox_state->channels;
	indio_dev->available_scan_masks = prox_state->scan_mask;
	indio_dev->info = &prox_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	atomic_set(&prox_state->common_attributes.data_ready, 0);

	ret = hid_sensor_setup_trigger(indio_dev, name,
				&prox_state->common_attributes);
	if (ret) {
		dev_err(&pdev->dev, "trigger setup failed\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "device register failed\n");
		goto error_remove_trigger;
	}

	prox_state->callbacks.send_event = prox_proc_event;
	prox_state->callbacks.capture_sample = prox_capture_sample;
	prox_state->callbacks.pdev = pdev;
	ret = sensor_hub_register_callback(hsdev, hsdev->usage,
					   &prox_state->callbacks);
	if (ret < 0) {
		dev_err(&pdev->dev, "callback reg failed\n");
		goto error_iio_unreg;
	}

	return ret;

error_iio_unreg:
	iio_device_unregister(indio_dev);
error_remove_trigger:
	hid_sensor_remove_trigger(indio_dev, &prox_state->common_attributes);
	return ret;
}

/* Function to deinitialize the processing for usage id */
static void hid_prox_remove(struct platform_device *pdev)
{
	struct hid_sensor_hub_device *hsdev = dev_get_platdata(&pdev->dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct prox_state *prox_state = iio_priv(indio_dev);

	sensor_hub_remove_callback(hsdev, hsdev->usage);
	iio_device_unregister(indio_dev);
	hid_sensor_remove_trigger(indio_dev, &prox_state->common_attributes);
}

static const struct platform_device_id hid_prox_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200011",
	},
	{
		/* Format: HID-SENSOR-tag-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-LISS-0226",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_prox_ids);

static struct platform_driver hid_prox_platform_driver = {
	.id_table = hid_prox_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &hid_sensor_pm_ops,
	},
	.probe		= hid_prox_probe,
	.remove		= hid_prox_remove,
};
module_platform_driver(hid_prox_platform_driver);

MODULE_DESCRIPTION("HID Sensor Proximity");
MODULE_AUTHOR("Archana Patni <archana.patni@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_HID");

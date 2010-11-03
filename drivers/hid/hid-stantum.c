/*
 *  HID driver for Stantum multitouch panels
 *
 *  Copyright (c) 2009 Stephane Chatty <chatty@enac.fr>
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_AUTHOR("Stephane Chatty <chatty@enac.fr>");
MODULE_DESCRIPTION("Stantum HID multitouch panels");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

struct stantum_data {
	__s32 x, y, z, w, h;	/* x, y, pressure, width, height */
	__u16 id;		/* touch id */
	bool valid;		/* valid finger data, or just placeholder? */
	bool first;		/* first finger in the HID packet? */
	bool activity;		/* at least one active finger so far? */
};

static int stantum_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	switch (usage->hid & HID_USAGE_PAGE) {

	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
			/* touchscreen emulation */
			input_set_abs_params(hi->input, ABS_X,
						field->logical_minimum,
						field->logical_maximum, 0, 0);
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
			/* touchscreen emulation */
			input_set_abs_params(hi->input, ABS_Y,
						field->logical_minimum,
						field->logical_maximum, 0, 0);
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		case HID_DG_INRANGE:
		case HID_DG_CONFIDENCE:
		case HID_DG_INPUTMODE:
		case HID_DG_DEVICEINDEX:
		case HID_DG_CONTACTCOUNT:
		case HID_DG_CONTACTMAX:
			return -1;

		case HID_DG_TIPSWITCH:
			/* touchscreen emulation */
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			return 1;

		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MAJOR);
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MINOR);
			input_set_abs_params(hi->input, ABS_MT_ORIENTATION,
					1, 1, 0, 0);
			return 1;
		case HID_DG_TIPPRESSURE:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_PRESSURE);
			return 1;

		case HID_DG_CONTACTID:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TRACKING_ID);
			return 1;

		}
		return 0;

	case 0xff000000:
		/* no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int stantum_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if (usage->type == EV_KEY || usage->type == EV_ABS)
		clear_bit(usage->code, *bit);

	return 0;
}

/*
 * this function is called when a whole finger has been parsed,
 * so that it can decide what to send to the input layer.
 */
static void stantum_filter_event(struct stantum_data *sd,
					struct input_dev *input)
{
	bool wide;

	if (!sd->valid) {
		/*
		 * touchscreen emulation: if the first finger is not valid and
		 * there previously was finger activity, this is a release
		 */
		if (sd->first && sd->activity) {
			input_event(input, EV_KEY, BTN_TOUCH, 0);
			sd->activity = false;
		}
		return;
	}

	input_event(input, EV_ABS, ABS_MT_TRACKING_ID, sd->id);
	input_event(input, EV_ABS, ABS_MT_POSITION_X, sd->x);
	input_event(input, EV_ABS, ABS_MT_POSITION_Y, sd->y);

	wide = (sd->w > sd->h);
	input_event(input, EV_ABS, ABS_MT_ORIENTATION, wide);
	input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, wide ? sd->w : sd->h);
	input_event(input, EV_ABS, ABS_MT_TOUCH_MINOR, wide ? sd->h : sd->w);

	input_event(input, EV_ABS, ABS_MT_PRESSURE, sd->z);

	input_mt_sync(input);
	sd->valid = false;

	/* touchscreen emulation */
	if (sd->first) {
		if (!sd->activity) {
			input_event(input, EV_KEY, BTN_TOUCH, 1);
			sd->activity = true;
		}
		input_event(input, EV_ABS, ABS_X, sd->x);
		input_event(input, EV_ABS, ABS_Y, sd->y);
	}
	sd->first = false;
}


static int stantum_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct stantum_data *sd = hid_get_drvdata(hid);

	if (hid->claimed & HID_CLAIMED_INPUT) {
		struct input_dev *input = field->hidinput->input;

		switch (usage->hid) {
		case HID_DG_INRANGE:
			/* this is the last field in a finger */
			stantum_filter_event(sd, input);
			break;
		case HID_DG_WIDTH:
			sd->w = value;
			break;
		case HID_DG_HEIGHT:
			sd->h = value;
			break;
		case HID_GD_X:
			sd->x = value;
			break;
		case HID_GD_Y:
			sd->y = value;
			break;
		case HID_DG_TIPPRESSURE:
			sd->z = value;
			break;
		case HID_DG_CONTACTID:
			sd->id = value;
			break;
		case HID_DG_CONFIDENCE:
			sd->valid = !!value;
			break;
		case 0xff000002:
			/* this comes only before the first finger */
			sd->first = true;
			break;

		default:
			/* ignore the others */
			return 1;
		}
	}

	/* we have handled the hidinput part, now remains hiddev */
	if (hid->claimed & HID_CLAIMED_HIDDEV && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int stantum_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	int ret;
	struct stantum_data *sd;

	sd = kmalloc(sizeof(struct stantum_data), GFP_KERNEL);
	if (!sd) {
		dev_err(&hdev->dev, "cannot allocate Stantum data\n");
		return -ENOMEM;
	}
	sd->valid = false;
	sd->first = false;
	sd->activity = false;
	hid_set_drvdata(hdev, sd);

	ret = hid_parse(hdev);
	if (!ret)
		ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	if (ret)
		kfree(sd);

	return ret;
}

static void stantum_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id stantum_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_STANTUM, USB_DEVICE_ID_MTP) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_STANTUM_STM, USB_DEVICE_ID_MTP_STM) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_STANTUM_SITRONIX, USB_DEVICE_ID_MTP_SITRONIX) },
	{ }
};
MODULE_DEVICE_TABLE(hid, stantum_devices);

static const struct hid_usage_id stantum_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver stantum_driver = {
	.name = "stantum",
	.id_table = stantum_devices,
	.probe = stantum_probe,
	.remove = stantum_remove,
	.input_mapping = stantum_input_mapping,
	.input_mapped = stantum_input_mapped,
	.usage_table = stantum_grabbed_usages,
	.event = stantum_event,
};

static int __init stantum_init(void)
{
	return hid_register_driver(&stantum_driver);
}

static void __exit stantum_exit(void)
{
	hid_unregister_driver(&stantum_driver);
}

module_init(stantum_init);
module_exit(stantum_exit);


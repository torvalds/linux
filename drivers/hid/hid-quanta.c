/*
 *  HID driver for Quanta Optical Touch dual-touch panels
 *
 *  Copyright (c) 2009-2010 Stephane Chatty <chatty@enac.fr>
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

MODULE_AUTHOR("Stephane Chatty <chatty@enac.fr>");
MODULE_DESCRIPTION("Quanta dual-touch panel");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

struct quanta_data {
	__u16 x, y;
	__u8 id;
	bool valid;		/* valid finger data, or just placeholder? */
	bool first;		/* is this the first finger in this frame? */
	bool activity_now;	/* at least one active finger in this frame? */
	bool activity;		/* at least one active finger previously? */
};

static int quanta_input_mapping(struct hid_device *hdev, struct hid_input *hi,
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
		case HID_DG_CONFIDENCE:
		case HID_DG_TIPSWITCH:
		case HID_DG_INPUTMODE:
		case HID_DG_DEVICEINDEX:
		case HID_DG_CONTACTCOUNT:
		case HID_DG_CONTACTMAX:
		case HID_DG_TIPPRESSURE:
		case HID_DG_WIDTH:
		case HID_DG_HEIGHT:
			return -1;
		case HID_DG_INRANGE:
			/* touchscreen emulation */
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			return 1;
		case HID_DG_CONTACTID:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TRACKING_ID);
			return 1;
		}
		return 0;

	case 0xff000000:
		/* ignore vendor-specific features */
		return -1;
	}

	return 0;
}

static int quanta_input_mapped(struct hid_device *hdev, struct hid_input *hi,
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
static void quanta_filter_event(struct quanta_data *td, struct input_dev *input)
{
	
	td->first = !td->first; /* touchscreen emulation */

	if (!td->valid) {
		/*
		 * touchscreen emulation: if no finger in this frame is valid
		 * and there previously was finger activity, this is a release
		 */ 
		if (!td->first && !td->activity_now && td->activity) {
			input_event(input, EV_KEY, BTN_TOUCH, 0);
			td->activity = false;
		}
		return;
	}

	input_event(input, EV_ABS, ABS_MT_TRACKING_ID, td->id);
	input_event(input, EV_ABS, ABS_MT_POSITION_X, td->x);
	input_event(input, EV_ABS, ABS_MT_POSITION_Y, td->y);

	input_mt_sync(input);
	td->valid = false;

	/* touchscreen emulation: if first active finger in this frame... */
	if (!td->activity_now) {
		/* if there was no previous activity, emit touch event */
		if (!td->activity) {
			input_event(input, EV_KEY, BTN_TOUCH, 1);
			td->activity = true;
		}
		td->activity_now = true;
		/* and in any case this is our preferred finger */
		input_event(input, EV_ABS, ABS_X, td->x);
		input_event(input, EV_ABS, ABS_Y, td->y);
	}
}


static int quanta_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct quanta_data *td = hid_get_drvdata(hid);

	if (hid->claimed & HID_CLAIMED_INPUT) {
		struct input_dev *input = field->hidinput->input;

		switch (usage->hid) {
		case HID_DG_INRANGE:
			td->valid = !!value;
			break;
		case HID_GD_X:
			td->x = value;
			break;
		case HID_GD_Y:
			td->y = value;
			quanta_filter_event(td, input);
			break;
		case HID_DG_CONTACTID:
			td->id = value;
			break;
		case HID_DG_CONTACTCOUNT:
			/* touch emulation: this is the last field in a frame */
			td->first = false;
			td->activity_now = false;
			break;
		case HID_DG_CONFIDENCE:
		case HID_DG_TIPSWITCH:
			/* avoid interference from generic hidinput handling */
			break;

		default:
			/* fallback to the generic hidinput handling */
			return 0;
		}
	}

	/* we have handled the hidinput part, now remains hiddev */
	if (hid->claimed & HID_CLAIMED_HIDDEV && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int quanta_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct quanta_data *td;

	td = kmalloc(sizeof(struct quanta_data), GFP_KERNEL);
	if (!td) {
		dev_err(&hdev->dev, "cannot allocate Quanta Touch data\n");
		return -ENOMEM;
	}
	td->valid = false;
	td->activity = false;
	td->activity_now = false;
	td->first = false;
	hid_set_drvdata(hdev, td);

	ret = hid_parse(hdev);
	if (!ret)
		ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	if (ret)
		kfree(td);

	return ret;
}

static void quanta_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id quanta_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_QUANTA,
			USB_DEVICE_ID_QUANTA_OPTICAL_TOUCH) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_QUANTA,
			USB_DEVICE_ID_PIXART_IMAGING_INC_OPTICAL_TOUCH_SCREEN) },
	{ }
};
MODULE_DEVICE_TABLE(hid, quanta_devices);

static const struct hid_usage_id quanta_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver quanta_driver = {
	.name = "quanta-touch",
	.id_table = quanta_devices,
	.probe = quanta_probe,
	.remove = quanta_remove,
	.input_mapping = quanta_input_mapping,
	.input_mapped = quanta_input_mapped,
	.usage_table = quanta_grabbed_usages,
	.event = quanta_event,
};

static int __init quanta_init(void)
{
	return hid_register_driver(&quanta_driver);
}

static void __exit quanta_exit(void)
{
	hid_unregister_driver(&quanta_driver);
}

module_init(quanta_init);
module_exit(quanta_exit);


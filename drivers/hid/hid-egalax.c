/*
 *  HID driver for eGalax dual-touch panels
 *
 *  Copyright (c) 2010 Stephane Chatty <chatty@enac.fr>
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
#include <linux/usb.h>
#include <linux/slab.h>
#include "usbhid/usbhid.h"

MODULE_AUTHOR("Stephane Chatty <chatty@enac.fr>");
MODULE_DESCRIPTION("eGalax dual-touch panel");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

struct egalax_data {
	__u16 x, y, z;
	__u8 id;
	bool first;		/* is this the first finger in the frame? */
	bool valid;		/* valid finger data, or just placeholder? */
	bool activity;		/* at least one active finger previously? */
	__u16 lastx, lasty;	/* latest valid (x, y) in the frame */
};

static int egalax_input_mapping(struct hid_device *hdev, struct hid_input *hi,
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
		case HID_DG_TIPSWITCH:
			/* touchscreen emulation */
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			return 1;
		case HID_DG_INRANGE:
		case HID_DG_CONFIDENCE:
		case HID_DG_CONTACTCOUNT:
		case HID_DG_CONTACTMAX:
			return -1;
		case HID_DG_CONTACTID:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TRACKING_ID);
			return 1;
		case HID_DG_TIPPRESSURE:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_PRESSURE);
			return 1;
		}
		return 0;
	}

	/* ignore others (from other reports we won't get anyway) */
	return -1;
}

static int egalax_input_mapped(struct hid_device *hdev, struct hid_input *hi,
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
static void egalax_filter_event(struct egalax_data *td, struct input_dev *input)
{
	td->first = !td->first; /* touchscreen emulation */

	if (td->valid) {
		/* emit multitouch events */
		input_event(input, EV_ABS, ABS_MT_TRACKING_ID, td->id);
		input_event(input, EV_ABS, ABS_MT_POSITION_X, td->x);
		input_event(input, EV_ABS, ABS_MT_POSITION_Y, td->y);
		input_event(input, EV_ABS, ABS_MT_PRESSURE, td->z);

		input_mt_sync(input);

		/*
		 * touchscreen emulation: store (x, y) as
		 * the last valid values in this frame
		 */
		td->lastx = td->x;
		td->lasty = td->y;
	}

	/*
	 * touchscreen emulation: if this is the second finger and at least
	 * one in this frame is valid, the latest valid in the frame is
	 * the oldest on the panel, the one we want for single touch
	 */
	if (!td->first && td->activity) {
		input_event(input, EV_ABS, ABS_X, td->lastx);
		input_event(input, EV_ABS, ABS_Y, td->lasty);
	}

	if (!td->valid) {
		/*
		 * touchscreen emulation: if the first finger is invalid
		 * and there previously was finger activity, this is a release
		 */ 
		if (td->first && td->activity) {
			input_event(input, EV_KEY, BTN_TOUCH, 0);
			td->activity = false;
		}
		return;
	}


	/* touchscreen emulation: if no previous activity, emit touch event */
	if (!td->activity) {
		input_event(input, EV_KEY, BTN_TOUCH, 1);
		td->activity = true;
	}
}


static int egalax_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct egalax_data *td = hid_get_drvdata(hid);

	if (hid->claimed & HID_CLAIMED_INPUT) {
		struct input_dev *input = field->hidinput->input;

		switch (usage->hid) {
		case HID_DG_INRANGE:
		case HID_DG_CONFIDENCE:
			/* avoid interference from generic hidinput handling */
			break;
		case HID_DG_TIPSWITCH:
			td->valid = value;
			break;
		case HID_DG_TIPPRESSURE:
			td->z = value;
			break;
		case HID_DG_CONTACTID:
			td->id = value;
			break;
		case HID_GD_X:
			td->x = value;
			break;
		case HID_GD_Y:
			td->y = value;
			/* this is the last field in a finger */
			egalax_filter_event(td, input);
			break;
		case HID_DG_CONTACTCOUNT:
			/* touch emulation: this is the last field in a frame */
			td->first = false;
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

static int egalax_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct egalax_data *td;
	struct hid_report *report;

	td = kmalloc(sizeof(struct egalax_data), GFP_KERNEL);
	if (!td) {
		dev_err(&hdev->dev, "cannot allocate eGalax data\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, td);

	ret = hid_parse(hdev);
	if (ret)
		goto end;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		goto end;

	report = hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[5]; 
	if (report) {
		report->field[0]->value[0] = 2;
		usbhid_submit_report(hdev, report, USB_DIR_OUT);
	}

end:
	if (ret)
		kfree(td);

	return ret;
}

static void egalax_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id egalax_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH) },
	{ }
};
MODULE_DEVICE_TABLE(hid, egalax_devices);

static const struct hid_usage_id egalax_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver egalax_driver = {
	.name = "egalax-touch",
	.id_table = egalax_devices,
	.probe = egalax_probe,
	.remove = egalax_remove,
	.input_mapping = egalax_input_mapping,
	.input_mapped = egalax_input_mapped,
	.usage_table = egalax_grabbed_usages,
	.event = egalax_event,
};

static int __init egalax_init(void)
{
	return hid_register_driver(&egalax_driver);
}

static void __exit egalax_exit(void)
{
	hid_unregister_driver(&egalax_driver);
}

module_init(egalax_init);
module_exit(egalax_exit);


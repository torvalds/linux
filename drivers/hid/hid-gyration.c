/*
 *  HID driver for some gyration "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007 Paul Walmsley
 *  Copyright (c) 2008 Jiri Slaby
 *  Copyright (c) 2006-2008 Jiri Kosina
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define gy_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))
static int gyration_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	set_bit(EV_REP, hi->input->evbit);
	switch (usage->hid & HID_USAGE) {
	/* Reported on Gyration MCE Remote */
	case 0x00d: gy_map_key_clear(KEY_HOME);		break;
	case 0x024: gy_map_key_clear(KEY_DVD);		break;
	case 0x025: gy_map_key_clear(KEY_PVR);		break;
	case 0x046: gy_map_key_clear(KEY_MEDIA);	break;
	case 0x047: gy_map_key_clear(KEY_MP3);		break;
	case 0x048: gy_map_key_clear(KEY_MEDIA);	break;
	case 0x049: gy_map_key_clear(KEY_CAMERA);	break;
	case 0x04a: gy_map_key_clear(KEY_VIDEO);	break;

	default:
		return 0;
	}
	return 1;
}

static int gyration_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput)
		return 0;

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_GENDESK &&
			(usage->hid & 0xff) == 0x82) {
		struct input_dev *input = field->hidinput->input;
		input_event(input, usage->type, usage->code, 1);
		input_sync(input);
		input_event(input, usage->type, usage->code, 0);
		input_sync(input);
		return 1;
	}

	return 0;
}

static const struct hid_device_id gyration_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GYRATION, USB_DEVICE_ID_GYRATION_REMOTE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_GYRATION, USB_DEVICE_ID_GYRATION_REMOTE_2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, gyration_devices);

static struct hid_driver gyration_driver = {
	.name = "gyration",
	.id_table = gyration_devices,
	.input_mapping = gyration_input_mapping,
	.event = gyration_event,
};

static int __init gyration_init(void)
{
	return hid_register_driver(&gyration_driver);
}

static void __exit gyration_exit(void)
{
	hid_unregister_driver(&gyration_driver);
}

module_init(gyration_init);
module_exit(gyration_exit);
MODULE_LICENSE("GPL");

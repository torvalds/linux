/*
 *  HID driver for some ezkey "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2007 Paul Walmsley
 *  Copyright (c) 2008 Jiri Slaby
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

#define ez_map_rel(c)	hid_map_usage(hi, usage, bit, max, EV_REL, (c))
#define ez_map_key(c)	hid_map_usage(hi, usage, bit, max, EV_KEY, (c))

static int ez_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0x230: ez_map_key(BTN_MOUSE);	break;
	case 0x231: ez_map_rel(REL_WHEEL);	break;
	/*
	 * this keyboard has a scrollwheel implemented in
	 * totally broken way. We map this usage temporarily
	 * to HWHEEL and handle it in the event quirk handler
	 */
	case 0x232: ez_map_rel(REL_HWHEEL);	break;
	default:
		return 0;
	}
	return 1;
}

static int ez_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput ||
			!usage->type)
		return 0;

	/* handle the temporary quirky mapping to HWHEEL */
	if (usage->type == EV_REL && usage->code == REL_HWHEEL) {
		struct input_dev *input = field->hidinput->input;
		input_event(input, usage->type, REL_WHEEL, -value);
		return 1;
	}

	return 0;
}

static const struct hid_device_id ez_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_EZKEY, USB_DEVICE_ID_BTC_8193) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ez_devices);

static struct hid_driver ez_driver = {
	.name = "ezkey",
	.id_table = ez_devices,
	.input_mapping = ez_input_mapping,
	.event = ez_event,
};

static int ez_init(void)
{
	return hid_register_driver(&ez_driver);
}

static void ez_exit(void)
{
	hid_unregister_driver(&ez_driver);
}

module_init(ez_init);
module_exit(ez_exit);
MODULE_LICENSE("GPL");

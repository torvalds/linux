/*
 *  HID driver for some chicony "special" devices
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

#define ch_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))
static int ch_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	set_bit(EV_REP, hi->input->evbit);
	switch (usage->hid & HID_USAGE) {
	case 0xff01: ch_map_key_clear(BTN_1);	break;
	case 0xff02: ch_map_key_clear(BTN_2);	break;
	case 0xff03: ch_map_key_clear(BTN_3);	break;
	case 0xff04: ch_map_key_clear(BTN_4);	break;
	case 0xff05: ch_map_key_clear(BTN_5);	break;
	case 0xff06: ch_map_key_clear(BTN_6);	break;
	case 0xff07: ch_map_key_clear(BTN_7);	break;
	case 0xff08: ch_map_key_clear(BTN_8);	break;
	case 0xff09: ch_map_key_clear(BTN_9);	break;
	case 0xff0a: ch_map_key_clear(BTN_A);	break;
	case 0xff0b: ch_map_key_clear(BTN_B);	break;
	case 0x00f1: ch_map_key_clear(KEY_WLAN);	break;
	case 0x00f2: ch_map_key_clear(KEY_BRIGHTNESSDOWN);	break;
	case 0x00f3: ch_map_key_clear(KEY_BRIGHTNESSUP);	break;
	case 0x00f4: ch_map_key_clear(KEY_DISPLAY_OFF);	break;
	default:
		return 0;
	}
	return 1;
}

static const struct hid_device_id ch_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_CHICONY_TACTICAL_PAD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_CHICONY_WIRELESS2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ch_devices);

static struct hid_driver ch_driver = {
	.name = "chicony",
	.id_table = ch_devices,
	.input_mapping = ch_input_mapping,
};

static int __init ch_init(void)
{
	return hid_register_driver(&ch_driver);
}

static void __exit ch_exit(void)
{
	hid_unregister_driver(&ch_driver);
}

module_init(ch_init);
module_exit(ch_exit);
MODULE_LICENSE("GPL");

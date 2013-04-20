/*
 *  HID driver for Kensigton Slimblade Trackball
 *
 *  Copyright (c) 2009 Jiri Kosina
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

#define ks_map_key(c)	hid_map_usage(hi, usage, bit, max, EV_KEY, (c))

static int ks_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0x01: ks_map_key(BTN_MIDDLE);	break;
	case 0x02: ks_map_key(BTN_SIDE);	break;
	default:
		return 0;
	}
	return 1;
}

static const struct hid_device_id ks_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_KENSINGTON, USB_DEVICE_ID_KS_SLIMBLADE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ks_devices);

static struct hid_driver ks_driver = {
	.name = "kensington",
	.id_table = ks_devices,
	.input_mapping = ks_input_mapping,
};
module_hid_driver(ks_driver);

MODULE_LICENSE("GPL");

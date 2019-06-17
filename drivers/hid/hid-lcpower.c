// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for LC Power Model RC1000MCE
 *
 *  Copyright (c) 2011 Chris Schlund 
 *  based on hid-topseed module
 */

/*
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define ts_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))
static int ts_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	switch (usage->hid & HID_USAGE) {
        case 0x046: ts_map_key_clear(KEY_YELLOW);         break;
        case 0x047: ts_map_key_clear(KEY_GREEN);          break;
        case 0x049: ts_map_key_clear(KEY_BLUE);           break;
        case 0x04a: ts_map_key_clear(KEY_RED);		  break;
        case 0x00d: ts_map_key_clear(KEY_HOME);           break;
        case 0x025: ts_map_key_clear(KEY_TV);             break;
        case 0x048: ts_map_key_clear(KEY_VCR);            break;
        case 0x024: ts_map_key_clear(KEY_MENU);           break;
        default:
        return 0;
	}

	return 1;
}

static const struct hid_device_id ts_devices[] = {
	{ HID_USB_DEVICE( USB_VENDOR_ID_LCPOWER, USB_DEVICE_ID_LCPOWER_LC1000) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ts_devices);

static struct hid_driver ts_driver = {
	.name = "LC RC1000MCE",
	.id_table = ts_devices,
	.input_mapping = ts_input_mapping,
};
module_hid_driver(ts_driver);

MODULE_LICENSE("GPL");

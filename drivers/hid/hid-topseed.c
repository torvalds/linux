// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for TopSeed Cyberlink remote
 *
 *  Copyright (c) 2008 Lev Babiev
 *  based on hid-cherry driver
 *
 *  Modified to also support BTC "Emprex 3009URF III Vista MCE Remote" by
 *  Wayne Thomas 2010.
 *
 *  Modified to support Conceptronic CLLRCMCE by
 *  Kees Bakker 2010.
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
	case 0x00c: ts_map_key_clear(KEY_WLAN);		break;
	case 0x00d: ts_map_key_clear(KEY_MEDIA);	break;
	case 0x010: ts_map_key_clear(KEY_ZOOM);		break;
	case 0x024: ts_map_key_clear(KEY_MENU);		break;
	case 0x025: ts_map_key_clear(KEY_TV);		break;
	case 0x027: ts_map_key_clear(KEY_MODE);		break;
	case 0x031: ts_map_key_clear(KEY_AUDIO);	break;
	case 0x032: ts_map_key_clear(KEY_TEXT);		break;
	case 0x033: ts_map_key_clear(KEY_CHANNEL);	break;
	case 0x047: ts_map_key_clear(KEY_MP3);		break;
	case 0x048: ts_map_key_clear(KEY_TV2);		break;
	case 0x049: ts_map_key_clear(KEY_CAMERA);	break;
	case 0x04a: ts_map_key_clear(KEY_VIDEO);	break;
	case 0x04b: ts_map_key_clear(KEY_ANGLE);	break;
	case 0x04c: ts_map_key_clear(KEY_LANGUAGE);	break;
	case 0x04d: ts_map_key_clear(KEY_SUBTITLE);	break;
	case 0x050: ts_map_key_clear(KEY_RADIO);	break;
	case 0x05a: ts_map_key_clear(KEY_TEXT);		break;
	case 0x05b: ts_map_key_clear(KEY_RED);		break;
	case 0x05c: ts_map_key_clear(KEY_GREEN);	break;
	case 0x05d: ts_map_key_clear(KEY_YELLOW);	break;
	case 0x05e: ts_map_key_clear(KEY_BLUE);		break;
	default:
		return 0;
	}

	return 1;
}

static const struct hid_device_id ts_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_TOPSEED, USB_DEVICE_ID_TOPSEED_CYBERLINK) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_BTC, USB_DEVICE_ID_BTC_EMPREX_REMOTE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_BTC, USB_DEVICE_ID_BTC_EMPREX_REMOTE_2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_TOPSEED2, USB_DEVICE_ID_TOPSEED2_RF_COMBO) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_CHICONY_WIRELESS) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_CHICONY_TOSHIBA_WT10A) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ts_devices);

static struct hid_driver ts_driver = {
	.name = "topseed",
	.id_table = ts_devices,
	.input_mapping = ts_input_mapping,
};
module_hid_driver(ts_driver);

MODULE_DESCRIPTION("HID driver for TopSeed Cyberlink remote");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>

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
	case 0x00f7: ch_map_key_clear(KEY_CAMERA);	break;
	case 0x00f8: ch_map_key_clear(KEY_PROG1);	break;
	default:
		return 0;
	}
	return 1;
}

static __u8 *ch_switch12_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	
	if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
		/* Change usage maximum and logical maximum from 0x7fff to
		 * 0x2fff, so they don't exceed HID_MAX_USAGES */
		switch (hdev->product) {
		case USB_DEVICE_ID_CHICONY_ACER_SWITCH12:
			if (*rsize >= 128 && rdesc[64] == 0xff && rdesc[65] == 0x7f
					&& rdesc[69] == 0xff && rdesc[70] == 0x7f) {
				hid_info(hdev, "Fixing up report descriptor\n");
				rdesc[65] = rdesc[70] = 0x2f;
			}
			break;
		}

	}
	return rdesc;
}


static const struct hid_device_id ch_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_CHICONY_TACTICAL_PAD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_CHICONY_WIRELESS2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_CHICONY_ACER_SWITCH12) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ch_devices);

static struct hid_driver ch_driver = {
	.name = "chicony",
	.id_table = ch_devices,
	.report_fixup = ch_switch12_report_fixup,
	.input_mapping = ch_input_mapping,
};
module_hid_driver(ch_driver);

MODULE_LICENSE("GPL");

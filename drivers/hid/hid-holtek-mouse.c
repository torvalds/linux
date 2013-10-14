/*
 * HID driver for Holtek gaming mice
 * Copyright (c) 2013 Christian Ohm
 * Heavily inspired by various other HID drivers that adjust the report
 * descriptor.
*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "hid-ids.h"

/*
 * The report descriptor of some Holtek based gaming mice specifies an
 * excessively large number of consumer usages (2^15), which is more than
 * HID_MAX_USAGES. This prevents proper parsing of the report descriptor.
 *
 * This driver fixes the report descriptor for:
 * - USB ID 04d9:a067, sold as Sharkoon Drakonia and Perixx MX-2000
 * - USB ID 04d9:a04a, sold as Tracer Sniper TRM-503, NOVA Gaming Slider X200
 *   and Zalman ZM-GM1
 * - USB ID 04d9:a081, sold as SHARKOON DarkGlider Gaming mouse
 */

static __u8 *holtek_mouse_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
		/* Change usage maximum and logical maximum from 0x7fff to
		 * 0x2fff, so they don't exceed HID_MAX_USAGES */
		switch (hdev->product) {
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A067:
			if (*rsize >= 122 && rdesc[115] == 0xff && rdesc[116] == 0x7f
					&& rdesc[120] == 0xff && rdesc[121] == 0x7f) {
				hid_info(hdev, "Fixing up report descriptor\n");
				rdesc[116] = rdesc[121] = 0x2f;
			}
			break;
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A04A:
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A081:
			if (*rsize >= 113 && rdesc[106] == 0xff && rdesc[107] == 0x7f
					&& rdesc[111] == 0xff && rdesc[112] == 0x7f) {
				hid_info(hdev, "Fixing up report descriptor\n");
				rdesc[107] = rdesc[112] = 0x2f;
			}
			break;
		}

	}
	return rdesc;
}

static const struct hid_device_id holtek_mouse_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A067) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A04A) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A081) },
	{ }
};
MODULE_DEVICE_TABLE(hid, holtek_mouse_devices);

static struct hid_driver holtek_mouse_driver = {
	.name = "holtek_mouse",
	.id_table = holtek_mouse_devices,
	.report_fixup = holtek_mouse_report_fixup,
};

module_hid_driver(holtek_mouse_driver);
MODULE_LICENSE("GPL");

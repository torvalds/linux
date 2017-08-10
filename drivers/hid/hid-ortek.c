/*
 *  HID driver for various devices which are apparently based on the same chipset
 *  from certain vendor which produces chips that contain wrong LogicalMaximum
 *  value in their HID report descriptor. Currently supported devices are:
 *
 *    Ortek PKB-1700
 *    Ortek WKB-2000
 *    iHome IMAC-A210S
 *    Skycable wireless presenter
 *
 *  Copyright (c) 2010 Johnathon Harris <jmharris@gmail.com>
 *  Copyright (c) 2011 Jiri Kosina
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

#include "hid-ids.h"

static __u8 *ortek_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize >= 56 && rdesc[54] == 0x25 && rdesc[55] == 0x01) {
		hid_info(hdev, "Fixing up logical maximum in report descriptor (Ortek)\n");
		rdesc[55] = 0x92;
	} else if (*rsize >= 54 && rdesc[52] == 0x25 && rdesc[53] == 0x01) {
		hid_info(hdev, "Fixing up logical maximum in report descriptor (Skycable)\n");
		rdesc[53] = 0x65;
	}
	return rdesc;
}

static const struct hid_device_id ortek_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ORTEK, USB_DEVICE_ID_ORTEK_PKB1700) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ORTEK, USB_DEVICE_ID_ORTEK_WKB2000) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ORTEK, USB_DEVICE_ID_ORTEK_IHOME_IMAC_A210S) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SKYCABLE, USB_DEVICE_ID_SKYCABLE_WIRELESS_PRESENTER) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ortek_devices);

static struct hid_driver ortek_driver = {
	.name = "ortek",
	.id_table = ortek_devices,
	.report_fixup = ortek_report_fixup
};
module_hid_driver(ortek_driver);

MODULE_LICENSE("GPL");

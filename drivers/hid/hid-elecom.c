/*
 *  HID driver for ELECOM devices.
 *  Copyright (c) 2010 Richard Nauber <Richard.Nauber@gmail.com>
 *  Copyright (c) 2016 Yuxuan Shui <yshuiv7@gmail.com>
 *  Copyright (c) 2017 Diego Elio Pettenò <flameeyes@flameeyes.eu>
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

static __u8 *elecom_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_ELECOM_BM084:
		/* The BM084 Bluetooth mouse includes a non-existing horizontal
		 * wheel in the HID descriptor. */
		if (*rsize >= 48 && rdesc[46] == 0x05 && rdesc[47] == 0x0c) {
			hid_info(hdev, "Fixing up Elecom BM084 report descriptor\n");
			rdesc[47] = 0x00;
		}
		break;
	case USB_DEVICE_ID_ELECOM_DEFT_WIRED:
	case USB_DEVICE_ID_ELECOM_DEFT_WIRELESS:
		/* The DEFT trackball has eight buttons, but its descriptor only
		 * reports five, disabling the three Fn buttons on the top of
		 * the mouse.
		 *
		 * Apply the following diff to the descriptor:
		 *
		 * Collection (Physical),              Collection (Physical),
		 *     Report ID (1),                      Report ID (1),
		 *     Report Count (5),           ->      Report Count (8),
		 *     Report Size (1),                    Report Size (1),
		 *     Usage Page (Button),                Usage Page (Button),
		 *     Usage Minimum (01h),                Usage Minimum (01h),
		 *     Usage Maximum (05h),        ->      Usage Maximum (08h),
		 *     Logical Minimum (0),                Logical Minimum (0),
		 *     Logical Maximum (1),                Logical Maximum (1),
		 *     Input (Variable),                   Input (Variable),
		 *     Report Count (1),           ->      Report Count (0),
		 *     Report Size (3),                    Report Size (3),
		 *     Input (Constant),                   Input (Constant),
		 *     Report Size (16),                   Report Size (16),
		 *     Report Count (2),                   Report Count (2),
		 *     Usage Page (Desktop),               Usage Page (Desktop),
		 *     Usage (X),                          Usage (X),
		 *     Usage (Y),                          Usage (Y),
		 *     Logical Minimum (-32768),           Logical Minimum (-32768),
		 *     Logical Maximum (32767),            Logical Maximum (32767),
		 *     Input (Variable, Relative),         Input (Variable, Relative),
		 * End Collection,                     End Collection,
		 */
		if (*rsize == 213 && rdesc[13] == 5 && rdesc[21] == 5) {
			hid_info(hdev, "Fixing up Elecom DEFT Fn buttons\n");
			rdesc[13] = 8; /* Button/Variable Report Count */
			rdesc[21] = 8; /* Button/Variable Usage Maximum */
			rdesc[29] = 0; /* Button/Constant Report Count */
		}
		break;
	}
	return rdesc;
}

static const struct hid_device_id elecom_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_BM084) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_DEFT_WIRED) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_DEFT_WIRELESS) },
	{ }
};
MODULE_DEVICE_TABLE(hid, elecom_devices);

static struct hid_driver elecom_driver = {
	.name = "elecom",
	.id_table = elecom_devices,
	.report_fixup = elecom_report_fixup
};
module_hid_driver(elecom_driver);

MODULE_LICENSE("GPL");

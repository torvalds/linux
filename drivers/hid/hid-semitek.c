// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Semitek keyboards
 *
 *  Copyright (c) 2021 Benjamin Moody
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

static const __u8 *semitek_report_fixup(struct hid_device *hdev, __u8 *rdesc,
					unsigned int *rsize)
{
	/* In the report descriptor for interface 2, fix the incorrect
	   description of report ID 0x04 (the report contains a
	   bitmask, not an array of keycodes.) */
	if (*rsize == 0xcb && rdesc[0x83] == 0x81 && rdesc[0x84] == 0x00) {
		hid_info(hdev, "fixing up Semitek report descriptor\n");
		rdesc[0x84] = 0x02;
	}
	return rdesc;
}

static const struct hid_device_id semitek_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SEMITEK, USB_DEVICE_ID_SEMITEK_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, semitek_devices);

static struct hid_driver semitek_driver = {
	.name = "semitek",
	.id_table = semitek_devices,
	.report_fixup = semitek_report_fixup,
};
module_hid_driver(semitek_driver);

MODULE_DESCRIPTION("HID driver for Semitek keyboards");
MODULE_LICENSE("GPL");

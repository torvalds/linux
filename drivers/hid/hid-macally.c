// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for quirky Macally devices
 *
 *  Copyright (c) 2019 Alex Henrie <alexhenrie24@gmail.com>
 */

#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

MODULE_AUTHOR("Alex Henrie <alexhenrie24@gmail.com>");
MODULE_DESCRIPTION("Macally devices");
MODULE_LICENSE("GPL");

/*
 * The Macally ikey keyboard says that its logical and usage maximums are both
 * 101, but the power key is 102 and the equals key is 103
 */
static __u8 *macally_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				 unsigned int *rsize)
{
	if (*rsize >= 60 && rdesc[53] == 0x65 && rdesc[59] == 0x65) {
		hid_info(hdev,
			"fixing up Macally ikey keyboard report descriptor\n");
		rdesc[53] = rdesc[59] = 0x67;
	}
	return rdesc;
}

static struct hid_device_id macally_id_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SOLID_YEAR,
			 USB_DEVICE_ID_MACALLY_IKEY_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, macally_id_table);

static struct hid_driver macally_driver = {
	.name			= "macally",
	.id_table		= macally_id_table,
	.report_fixup		= macally_report_fixup,
};

module_hid_driver(macally_driver);

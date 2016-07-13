/*
 *  HID driver for Asus notebook built-in keyboard.
 *  Fixes small logical maximum to match usage maximum.
 *
 *  Currently supported devices are:
 *    EeeBook X205TA
 *    VivoBook E200HA
 *
 *  Copyright (c) 2016 Yusuke Fujimaki <usk.fujimaki@gmail.com>
 *
 *  This module based on hid-ortek by
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

static __u8 *asus_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize >= 56 && rdesc[54] == 0x25 && rdesc[55] == 0x65) {
		hid_info(hdev, "Fixing up Asus notebook report descriptor\n");
		rdesc[55] = 0xdd;
	}
	return rdesc;
}

static const struct hid_device_id asus_devices[] = {
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_NOTEBOOK_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, asus_devices);

static struct hid_driver asus_driver = {
	.name = "asus",
	.id_table = asus_devices,
	.report_fixup = asus_report_fixup
};
module_hid_driver(asus_driver);

MODULE_LICENSE("GPL");

/*
 *  HID driver for Saitek devices, currently only the PS1000 (USB gamepad).
 *  Fixes the HID report descriptor by removing a non-existent axis and
 *  clearing the constant bit on the input reports for buttons and d-pad.
 *  (This module is based on "hid-ortek".)
 *
 *  Copyright (c) 2012 Andreas Hübner
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
#include <linux/kernel.h>

#include "hid-ids.h"

static __u8 *saitek_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize == 137 && rdesc[20] == 0x09 && rdesc[21] == 0x33
			&& rdesc[94] == 0x81 && rdesc[95] == 0x03
			&& rdesc[110] == 0x81 && rdesc[111] == 0x03) {

		hid_info(hdev, "Fixing up Saitek PS1000 report descriptor\n");

		/* convert spurious axis to a "noop" Logical Minimum (0) */
		rdesc[20] = 0x15;
		rdesc[21] = 0x00;

		/* clear constant bit on buttons and d-pad */
		rdesc[95] = 0x02;
		rdesc[111] = 0x02;

	}
	return rdesc;
}

static const struct hid_device_id saitek_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_PS1000)},
	{ }
};

MODULE_DEVICE_TABLE(hid, saitek_devices);

static struct hid_driver saitek_driver = {
	.name = "saitek",
	.id_table = saitek_devices,
	.report_fixup = saitek_report_fixup
};

static int __init saitek_init(void)
{
	return hid_register_driver(&saitek_driver);
}

static void __exit saitek_exit(void)
{
	hid_unregister_driver(&saitek_driver);
}

module_init(saitek_init);
module_exit(saitek_exit);
MODULE_LICENSE("GPL");

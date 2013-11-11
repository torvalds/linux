/*
 *  HID driver for Elecom BM084 (bluetooth mouse).
 *  Removes a non-existing horizontal wheel from
 *  the HID descriptor.
 *  (This module is based on "hid-ortek".)
 *
 *  Copyright (c) 2010 Richard Nauber <Richard.Nauber@gmail.com>
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
	if (*rsize >= 48 && rdesc[46] == 0x05 && rdesc[47] == 0x0c) {
		hid_info(hdev, "Fixing up Elecom BM084 report descriptor\n");
		rdesc[47] = 0x00;
	}
    return rdesc;
}

static const struct hid_device_id elecom_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_BM084)},
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

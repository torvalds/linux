/*
 *  HID driver for Ortek WKB-2000 (wireless keyboard + mouse trackpad).
 *  Fixes LogicalMaximum error in USB report description, see
 *  http://bugzilla.kernel.org/show_bug.cgi?id=14787
 *
 *  Copyright (c) 2010 Johnathon Harris <jmharris@gmail.com>
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
		dev_info(&hdev->dev, "Fixing up Ortek WKB-2000 "
				"report descriptor.\n");
		rdesc[55] = 0x92;
	}
	return rdesc;
}

static const struct hid_device_id ortek_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ORTEK, USB_DEVICE_ID_ORTEK_WKB2000) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ortek_devices);

static struct hid_driver ortek_driver = {
	.name = "ortek",
	.id_table = ortek_devices,
	.report_fixup = ortek_report_fixup
};

static int __init ortek_init(void)
{
	return hid_register_driver(&ortek_driver);
}

static void __exit ortek_exit(void)
{
	hid_unregister_driver(&ortek_driver);
}

module_init(ortek_init);
module_exit(ortek_exit);
MODULE_LICENSE("GPL");

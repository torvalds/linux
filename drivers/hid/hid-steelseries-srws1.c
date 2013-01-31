/*
 *  HID driver for Steelseries SRW-S1
 *
 *  Copyright (c) 2013 Simon Wood
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

static __u8 *steelseries_srws1_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize >= 115 && rdesc[11] == 0x02 && rdesc[13] == 0xc8
			&& rdesc[29] == 0xbb && rdesc[40] == 0xc5) {
		hid_info(hdev, "Fixing up Steelseries SRW-S1 report descriptor\n");
		rdesc[11] = 0x01;
		rdesc[13] = 0x30;
		rdesc[29] = 0x31;
		rdesc[40] = 0x32;
	}
	return rdesc;
}

static const struct hid_device_id steelseries_srws1_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, USB_DEVICE_ID_STEELSERIES_SRWS1) },
	{ }
};
MODULE_DEVICE_TABLE(hid, steelseries_srws1_devices);

static struct hid_driver steelseries_srws1_driver = {
	.name = "steelseries_srws1",
	.id_table = steelseries_srws1_devices,
	.report_fixup = steelseries_srws1_report_fixup
};

static int __init steelseries_srws1_init(void)
{
	return hid_register_driver(&steelseries_srws1_driver);
}

static void __exit steelseries_srws1_exit(void)
{
	hid_unregister_driver(&steelseries_srws1_driver);
}

module_init(steelseries_srws1_init);
module_exit(steelseries_srws1_exit);
MODULE_LICENSE("GPL");

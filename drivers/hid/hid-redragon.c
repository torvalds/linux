/*
 *  HID driver for Redragon keyboards
 *
 *  Copyright (c) 2017 Robert Munteanu
 *  SPDX-License-Identifier: GPL-2.0+
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


/*
 * The Redragon Asura keyboard sends an incorrect HID descriptor.
 * At byte 100 it contains
 *
 *   0x81, 0x00
 *
 * which is Input (Data, Arr, Abs), but it should be
 *
 *   0x81, 0x02
 *
 * which is Input (Data, Var, Abs), which is consistent with the way
 * key codes are generated.
 */

static __u8 *redragon_report_fixup(struct hid_device *hdev, __u8 *rdesc,
	unsigned int *rsize)
{
	if (*rsize >= 102 && rdesc[100] == 0x81 && rdesc[101] == 0x00) {
		dev_info(&hdev->dev, "Fixing Redragon ASURA report descriptor.\n");
		rdesc[101] = 0x02;
	}

	return rdesc;
}

static const struct hid_device_id redragon_devices[] = {
	{HID_USB_DEVICE(USB_VENDOR_ID_JESS, USB_DEVICE_ID_REDRAGON_ASURA)},
	{}
};

MODULE_DEVICE_TABLE(hid, redragon_devices);

static struct hid_driver redragon_driver = {
	.name = "redragon",
	.id_table = redragon_devices,
	.report_fixup = redragon_report_fixup
};

module_hid_driver(redragon_driver);

MODULE_LICENSE("GPL");

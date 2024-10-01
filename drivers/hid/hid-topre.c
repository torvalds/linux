// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for Topre REALFORCE Keyboards
 *
 *  Copyright (c) 2022 Harry Stern <harry@harrystern.net>
 *
 *  Based on the hid-macally driver
 */

#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

MODULE_AUTHOR("Harry Stern <harry@harrystern.net>");
MODULE_DESCRIPTION("REALFORCE R2 Keyboard driver");
MODULE_LICENSE("GPL");

/*
 * Fix the REALFORCE R2's non-boot interface's report descriptor to match the
 * events it's actually sending. It claims to send array events but is instead
 * sending variable events.
 */
static __u8 *topre_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				 unsigned int *rsize)
{
	if (*rsize >= 119 && rdesc[69] == 0x29 && rdesc[70] == 0xe7 &&
						 rdesc[71] == 0x81 && rdesc[72] == 0x00) {
		hid_info(hdev,
			"fixing up Topre REALFORCE keyboard report descriptor\n");
		rdesc[72] = 0x02;
	}
	return rdesc;
}

static const struct hid_device_id topre_id_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_TOPRE,
			 USB_DEVICE_ID_TOPRE_REALFORCE_R2_108) },
	{ }
};
MODULE_DEVICE_TABLE(hid, topre_id_table);

static struct hid_driver topre_driver = {
	.name			= "topre",
	.id_table		= topre_id_table,
	.report_fixup		= topre_report_fixup,
};

module_hid_driver(topre_driver);

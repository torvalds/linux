// SPDX-License-Identifier: GPL-2.0
/*
 *  HID driver for Rakk devices
 *
 *  Copyright (c) 2026 Karl Cayme
 *
 *  The Rakk Dasig X gaming mouse has a faulty HID report descriptor that
 *  declares USAGE_MAXIMUM = 3 (buttons 1-3) while actually sending 5 button
 *  bits (REPORT_COUNT = 5). This causes the kernel to ignore side buttons
 *  (buttons 4 and 5). This driver fixes the descriptor so all 5 buttons
 *  are properly recognized across 3 modes (wired, dongle, and Bluetooth).
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

/*
 * The faulty byte is at offset 17 in the report descriptor for all three
 * connection modes (USB direct, wireless dongle, and Bluetooth).
 *
 * Bytes 16-17 are: 0x29 0x03 (USAGE_MAXIMUM = 3)
 * The fix changes byte 17 to 0x05 (USAGE_MAXIMUM = 5).
 *
 * Original descriptor bytes 0-17:
 *   05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 03
 *                                                       ^^
 *   Should be 0x05 to declare 5 buttons instead of 3.
 */
#define RAKK_RDESC_USAGE_MAX_OFFSET	17
#define RAKK_RDESC_USAGE_MAX_ORIG	0x03
#define RAKK_RDESC_USAGE_MAX_FIXED	0x05
#define RAKK_RDESC_USB_SIZE		193
#define RAKK_RDESC_DONGLE_SIZE		150
#define RAKK_RDESC_BT_SIZE		89

static const __u8 *rakk_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				     unsigned int *rsize)
{
	if (((*rsize == RAKK_RDESC_USB_SIZE &&
	      hdev->product == USB_DEVICE_ID_TELINK_RAKK_DASIG_X) ||
	     (*rsize == RAKK_RDESC_DONGLE_SIZE &&
	      hdev->product == USB_DEVICE_ID_TELINK_RAKK_DASIG_X_DONGLE) ||
	     (*rsize == RAKK_RDESC_BT_SIZE &&
	      hdev->product == USB_DEVICE_ID_TELINK_RAKK_DASIG_X_BT)) &&
	    rdesc[RAKK_RDESC_USAGE_MAX_OFFSET] == RAKK_RDESC_USAGE_MAX_ORIG) {
		hid_info(hdev, "fixing Rakk Dasig X button count (3 -> 5)\n");
		rdesc[RAKK_RDESC_USAGE_MAX_OFFSET] = RAKK_RDESC_USAGE_MAX_FIXED;
	}

	return rdesc;
}

static const struct hid_device_id rakk_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_TELINK,
			 USB_DEVICE_ID_TELINK_RAKK_DASIG_X) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_TELINK,
			 USB_DEVICE_ID_TELINK_RAKK_DASIG_X_DONGLE) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_TELINK,
			       USB_DEVICE_ID_TELINK_RAKK_DASIG_X_BT) },
	{ }
};
MODULE_DEVICE_TABLE(hid, rakk_devices);

static struct hid_driver rakk_driver = {
	.name = "rakk",
	.id_table = rakk_devices,
	.report_fixup = rakk_report_fixup,
};
module_hid_driver(rakk_driver);

MODULE_DESCRIPTION("HID driver for Rakk Dasig X mouse - fix side button support");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karl Cayme");

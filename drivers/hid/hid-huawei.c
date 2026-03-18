// SPDX-License-Identifier: GPL-2.0
/*
 *  HID driver for some huawei "special" devices
 *
 * Copyright (c) 2026 Miao Li <limiao@kylinos.cn>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "hid-ids.h"

static const __u8 huawei_cd30_kbd_rdesc_fixed[] = {
	0x05, 0x01,		/* Usage Page (Generic Desktop)		*/
	0x09, 0x80,		/* Usage (System Control)		*/
	0xa1, 0x01,		/* Collection (Application)		*/
	0x85, 0x01,		/*   Report ID (1)			*/
	0x19, 0x81,		/*   Usage Minimum (System Power Down)	*/
	0x29, 0x83,		/*   Usage Maximum (System Wake Up)	*/
	0x15, 0x00,		/*   Logical Minimum (0)		*/
	0x25, 0x01,		/*   Logical Maximum (1)		*/
	0x75, 0x01,		/*   Report Size (1 bit)		*/
	0x95, 0x03,		/*   Report Count (3)			*/
	0x81, 0x02,		/*   Input (Data,Var,Abs)		*/
	0x95, 0x05,		/*   Report Count (5)			*/
	0x81, 0x01,		/*   Input (Cnst,Ary,Abs)		*/
	0xc0,			/* End Collection			*/
	0x05, 0x0c,		/* Usage Page (Consumer)		*/
	0x09, 0x01,		/* Usage (Consumer Control)		*/
	0xa1, 0x01,		/* Collection (Application)		*/
	0x85, 0x02,		/*   Report ID (2)			*/
	0x19, 0x00,		/*   Usage Minimum (0)			*/
	0x2a, 0x3c, 0x02,	/*   Usage Maximum (0x023C)		*/
	0x15, 0x00,		/*   Logical Minimum (0)		*/
	0x26, 0x3c, 0x02,	/*   Logical Maximum (0x023C)		*/
	0x95, 0x01,		/*   Report Count (1)			*/
	0x75, 0x10,		/*   Report Size (16 bits)		*/
	0x81, 0x00,		/*   Input (Data,Ary,Abs)		*/
	0xc0			/* End Collection			*/
};

static const __u8 *huawei_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				  unsigned int *rsize)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	switch (hdev->product) {
	case USB_DEVICE_ID_HUAWEI_CD30KBD:
		if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
			if (*rsize != sizeof(huawei_cd30_kbd_rdesc_fixed) ||
				memcmp(huawei_cd30_kbd_rdesc_fixed, rdesc,
					sizeof(huawei_cd30_kbd_rdesc_fixed)) != 0) {
				hid_info(hdev, "Replacing Huawei cd30 keyboard report descriptor.\n");
				*rsize = sizeof(huawei_cd30_kbd_rdesc_fixed);
				return huawei_cd30_kbd_rdesc_fixed;
			}
		}
		break;
	}

	return rdesc;
}

static const struct hid_device_id huawei_devices[] = {
	/* HUAWEI cd30 keyboard */
	{ HID_USB_DEVICE(USB_VENDOR_ID_HUAWEI, USB_DEVICE_ID_HUAWEI_CD30KBD)},
	{ }
};
MODULE_DEVICE_TABLE(hid, huawei_devices);

static struct hid_driver huawei_driver = {
	.name = "huawei",
	.id_table = huawei_devices,
	.report_fixup = huawei_report_fixup,
};
module_hid_driver(huawei_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miao Li <limiao@kylinos.cn>");
MODULE_DESCRIPTION("HID driver for some huawei \"special\" devices");

/*
 *  HID driver for ELECOM devices:
 *  - BM084 Bluetooth Mouse
 *  - EX-G Trackball (Wired and wireless)
 *  - DEFT Trackball (Wired and wireless)
 *  - HUGE Trackball (Wired and wireless)
 *
 *  Copyright (c) 2010 Richard Nauber <Richard.Nauber@gmail.com>
 *  Copyright (c) 2016 Yuxuan Shui <yshuiv7@gmail.com>
 *  Copyright (c) 2017 Diego Elio Pettenò <flameeyes@flameeyes.eu>
 *  Copyright (c) 2017 Alex Manoussakis <amanou@gnu.org>
 *  Copyright (c) 2017 Tomasz Kramkowski <tk@the-tk.com>
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
 * Certain ELECOM mice misreport their button count meaning that they only work
 * correctly with the ELECOM mouse assistant software which is unavailable for
 * Linux. A four extra INPUT reports and a FEATURE report are described by the
 * report descriptor but it does not appear that these enable software to
 * control what the extra buttons map to. The only simple and straightforward
 * solution seems to involve fixing up the report descriptor.
 *
 * Report descriptor format:
 * Positions 13, 15, 21 and 31 store the button bit count, button usage minimum,
 * button usage maximum and padding bit count respectively.
 */
#define MOUSE_BUTTONS_MAX 8
static void mouse_button_fixup(struct hid_device *hdev,
			       __u8 *rdesc, unsigned int rsize,
			       int nbuttons)
{
	if (rsize < 32 || rdesc[12] != 0x95 ||
	    rdesc[14] != 0x75 || rdesc[15] != 0x01 ||
	    rdesc[20] != 0x29 || rdesc[30] != 0x75)
		return;
	hid_info(hdev, "Fixing up Elecom mouse button count\n");
	nbuttons = clamp(nbuttons, 0, MOUSE_BUTTONS_MAX);
	rdesc[13] = nbuttons;
	rdesc[21] = nbuttons;
	rdesc[31] = MOUSE_BUTTONS_MAX - nbuttons;
}

static __u8 *elecom_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_ELECOM_BM084:
		/* The BM084 Bluetooth mouse includes a non-existing horizontal
		 * wheel in the HID descriptor. */
		if (*rsize >= 48 && rdesc[46] == 0x05 && rdesc[47] == 0x0c) {
			hid_info(hdev, "Fixing up Elecom BM084 report descriptor\n");
			rdesc[47] = 0x00;
		}
		break;
	case USB_DEVICE_ID_ELECOM_EX_G_WIRED:
	case USB_DEVICE_ID_ELECOM_EX_G_WIRELESS:
		mouse_button_fixup(hdev, rdesc, *rsize, 6);
		break;
	case USB_DEVICE_ID_ELECOM_DEFT_WIRED:
	case USB_DEVICE_ID_ELECOM_DEFT_WIRELESS:
	case USB_DEVICE_ID_ELECOM_HUGE_WIRED:
	case USB_DEVICE_ID_ELECOM_HUGE_WIRELESS:
		mouse_button_fixup(hdev, rdesc, *rsize, 8);
		break;
	}
	return rdesc;
}

static const struct hid_device_id elecom_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_BM084) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_EX_G_WIRED) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_EX_G_WIRELESS) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_DEFT_WIRED) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_DEFT_WIRELESS) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_HUGE_WIRED) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELECOM, USB_DEVICE_ID_ELECOM_HUGE_WIRELESS) },
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

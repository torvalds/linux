// SPDX-License-Identifier: GPL-2.0
/*
 * HID driver for Xiaomi Mi Dual Mode Wireless Mouse Silent Edition
 *
 * Copyright (c) 2021 Ilya Skriblovsky
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/hid.h>

#include "hid-ids.h"

/* Fixed Mi Silent Mouse report descriptor */
/* Button's Usage Maximum changed from 3 to 5 to make side buttons work */
#define MI_SILENT_MOUSE_ORIG_RDESC_LENGTH   87
static const __u8 mi_silent_mouse_rdesc_fixed[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x02,         /*  Usage (Mouse),                      */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x03,         /*      Report ID (3),                  */
	0x09, 0x01,         /*      Usage (Pointer),                */
	0xA1, 0x00,         /*      Collection (Physical),          */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x19, 0x01,         /*          Usage Minimum (01h),        */
	0x29, 0x05, /* X */ /*          Usage Maximum (05h),        */
	0x15, 0x00,         /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x75, 0x03,         /*          Report Size (3),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x15, 0x81,         /*          Logical Minimum (-127),     */
	0x25, 0x7F,         /*          Logical Maximum (127),      */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x15, 0x81,         /*          Logical Minimum (-127),     */
	0x25, 0x7F,         /*          Logical Maximum (127),      */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0xC0,               /*      End Collection,                 */
	0xC0,               /*  End Collection,                     */
	0x06, 0x01, 0xFF,   /*  Usage Page (FF01h),                 */
	0x09, 0x01,         /*  Usage (01h),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x05,         /*      Report ID (5),                  */
	0x09, 0x05,         /*      Usage (05h),                    */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x04,         /*      Report Count (4),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0xC0                /*  End Collection                      */
};

static const __u8 *xiaomi_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				       unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_MI_SILENT_MOUSE:
		if (*rsize == MI_SILENT_MOUSE_ORIG_RDESC_LENGTH) {
			hid_info(hdev, "fixing up Mi Silent Mouse report descriptor\n");
			*rsize = sizeof(mi_silent_mouse_rdesc_fixed);
			return mi_silent_mouse_rdesc_fixed;
		}
		break;
	}
	return rdesc;
}

static const struct hid_device_id xiaomi_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_XIAOMI, USB_DEVICE_ID_MI_SILENT_MOUSE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, xiaomi_devices);

static struct hid_driver xiaomi_driver = {
	.name = "xiaomi",
	.id_table = xiaomi_devices,
	.report_fixup = xiaomi_report_fixup,
};
module_hid_driver(xiaomi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilya Skriblovsky <IlyaSkriblovsky@gmail.com>");
MODULE_DESCRIPTION("Fixing side buttons of Xiaomi Mi Silent Mouse");

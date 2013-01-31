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

/* Fixed report descriptor for Steelseries SRW-S1 wheel controller
 *
 * The original descriptor hides the sensitivity and assists dials
 * a custom vendor usage page. This inserts a patch to make them
 * appear in the 'Generic Desktop' usage.
 */

static __u8 steelseries_srws1_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop)                */
0x09, 0x08,         /*  Usage (MultiAxis), Changed          */
0xA1, 0x01,         /*  Collection (Application),           */
0xA1, 0x02,         /*      Collection (Logical),           */
0x95, 0x01,         /*          Report Count (1),           */
0x05, 0x01,         /* Changed  Usage Page (Desktop),       */
0x09, 0x30,         /* Changed  Usage (X),                  */
0x16, 0xF8, 0xF8,   /*          Logical Minimum (-1800),    */
0x26, 0x08, 0x07,   /*          Logical Maximum (1800),     */
0x65, 0x14,         /*          Unit (Degrees),             */
0x55, 0x0F,         /*          Unit Exponent (15),         */
0x75, 0x10,         /*          Report Size (16),           */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x31,         /* Changed  Usage (Y),                  */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x75, 0x0C,         /*          Report Size (12),           */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x32,         /* Changed  Usage (Z),                  */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x75, 0x0C,         /*          Report Size (12),           */
0x81, 0x02,         /*          Input (Variable),           */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x09, 0x39,         /*          Usage (Hat Switch),         */
0x25, 0x07,         /*          Logical Maximum (7),        */
0x35, 0x00,         /*          Physical Minimum (0),       */
0x46, 0x3B, 0x01,   /*          Physical Maximum (315),     */
0x65, 0x14,         /*          Unit (Degrees),             */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x01,         /*          Report Count (1),           */
0x81, 0x02,         /*          Input (Variable),           */
0x25, 0x01,         /*          Logical Maximum (1),        */
0x45, 0x01,         /*          Physical Maximum (1),       */
0x65, 0x00,         /*          Unit,                       */
0x75, 0x01,         /*          Report Size (1),            */
0x95, 0x03,         /*          Report Count (3),           */
0x81, 0x01,         /*          Input (Constant),           */
0x05, 0x09,         /*          Usage Page (Button),        */
0x19, 0x01,         /*          Usage Minimum (01h),        */
0x29, 0x11,         /*          Usage Maximum (11h),        */
0x95, 0x11,         /*          Report Count (17),          */
0x81, 0x02,         /*          Input (Variable),           */
                    /*   ---- Dial patch starts here ----   */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x09, 0x33,         /*          Usage (RX),                 */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x02,         /*          Report Count (2),           */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x25, 0x0b,         /*          Logical Maximum (b),        */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x35,         /*          Usage (RZ),                 */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x01,         /*          Report Count (1),           */
0x25, 0x03,         /*          Logical Maximum (3),        */
0x81, 0x02,         /*          Input (Variable),           */
                    /*    ---- Dial patch ends here ----    */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),         */
0x09, 0x01,         /*          Usage (01h),                */
0x75, 0x04,         /* Changed  Report Size (4),            */
0x95, 0x0D,         /* Changed  Report Count (13),          */
0x81, 0x02,         /*          Input (Variable),           */
0xC0,               /*      End Collection,                 */
0xA1, 0x02,         /*      Collection (Logical),           */
0x09, 0x02,         /*          Usage (02h),                */
0x75, 0x08,         /*          Report Size (8),            */
0x95, 0x10,         /*          Report Count (16),          */
0x91, 0x02,         /*          Output (Variable),          */
0xC0,               /*      End Collection,                 */
0xC0                /*  End Collection                      */
};

static __u8 *steelseries_srws1_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize >= 115 && rdesc[11] == 0x02 && rdesc[13] == 0xc8
			&& rdesc[29] == 0xbb && rdesc[40] == 0xc5) {
		hid_info(hdev, "Fixing up Steelseries SRW-S1 report descriptor\n");
		rdesc = steelseries_srws1_rdesc_fixed;
		*rsize = sizeof(steelseries_srws1_rdesc_fixed);
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

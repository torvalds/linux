// SPDX-License-Identifier: GPL-2.0
/*
 * HID driver for Maltron L90
 *
 * Copyright (c) 1999 Andreas Gal
 * Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 * Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 * Copyright (c) 2008 Jiri Slaby
 * Copyright (c) 2012 David Dillow <dave@thedillows.org>
 * Copyright (c) 2006-2013 Jiri Kosina
 * Copyright (c) 2013 Colin Leitner <colin.leitner@gmail.com>
 * Copyright (c) 2014-2016 Frank Praznik <frank.praznik@gmail.com>
 * Copyright (c) 2010 Richard Nauber <Richard.Nauber@gmail.com>
 * Copyright (c) 2016 Yuxuan Shui <yshuiv7@gmail.com>
 * Copyright (c) 2018 William Whistler <wtbw@wtbw.co.uk>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

/* The original buggy USB descriptor */
static u8 maltron_rdesc_o[] = {
	0x05, 0x01,        /* Usage Page (Generic Desktop Ctrls) */
	0x09, 0x80,        /* Usage (Sys Control)                */
	0xA1, 0x01,        /* Collection (Application)           */
	0x85, 0x02,        /*   Report ID (2)                    */
	0x75, 0x01,        /*   Report Size (1)                  */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x15, 0x00,        /*   Logical Minimum (0)              */
	0x25, 0x01,        /*   Logical Maximum (1)              */
	0x09, 0x82,        /*   Usage (Sys Sleep)                */
	0x81, 0x06,        /*   Input (Data,Var,Rel)             */
	0x09, 0x82,        /*   Usage (Sys Sleep)                */
	0x81, 0x06,        /*   Input (Data,Var,Rel)             */
	0x09, 0x83,        /*   Usage (Sys Wake Up)              */
	0x81, 0x06,        /*   Input (Data,Var,Rel)             */
	0x75, 0x05,        /*   Report Size (5)                  */
	0x81, 0x01,        /*   Input (Const,Array,Abs)          */
	0xC0,              /* End Collection                     */
	0x05, 0x0C,        /* Usage Page (Consumer)              */
	0x09, 0x01,        /* Usage (Consumer Control)           */
	0xA1, 0x01,        /* Collection (Application)           */
	0x85, 0x03,        /*   Report ID (3)                    */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x75, 0x10,        /*   Report Size (16)                 */
	0x19, 0x00,        /*   Usage Minimum (Unassigned)       */
	0x2A, 0xFF, 0x7F,  /*   Usage Maximum (0x7FFF)           */
	0x81, 0x00,        /*   Input (Data,Array,Abs)           */
	0xC0,              /* End Collection                     */
	0x06, 0x7F, 0xFF,  /* Usage Page (Vendor Defined 0xFF7F) */
	0x09, 0x01,        /* Usage (0x01)                       */
	0xA1, 0x01,        /* Collection (Application)           */
	0x85, 0x04,        /*   Report ID (4)                    */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x75, 0x10,        /*   Report Size (16)                 */
	0x19, 0x00,        /*   Usage Minimum (0x00)             */
	0x2A, 0xFF, 0x7F,  /*   Usage Maximum (0x7FFF)           */
	0x81, 0x00,        /*   Input (Data,Array,Abs)           */
	0x75, 0x02,        /*   Report Size (2)                  */
	0x25, 0x02,        /*   Logical Maximum (2)              */
	0x09, 0x90,        /*   Usage (0x90)                     */
	0xB1, 0x02,        /*   Feature (Data,Var,Abs)           */
	0x75, 0x06,        /*   Report Size (6)                  */
	0xB1, 0x01,        /*   Feature (Const,Array,Abs)        */
	0x75, 0x01,        /*   Report Size (1)                  */
	0x25, 0x01,        /*   Logical Maximum (1)              */
	0x05, 0x08,        /*   Usage Page (LEDs)                */
	0x09, 0x2A,        /*   Usage (On-Line)                  */
	0x91, 0x02,        /*   Output (Data,Var,Abs)            */
	0x09, 0x4B,        /*   Usage (Generic Indicator)        */
	0x91, 0x02,        /*   Output (Data,Var,Abs)            */
	0x75, 0x06,        /*   Report Size (6)                  */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x91, 0x01,        /*   Output (Const,Array,Abs)         */
	0xC0               /* End Collection                     */
};

/* The patched descriptor, allowing media key events to be accepted as valid */
static u8 maltron_rdesc[] = {
	0x05, 0x01,        /* Usage Page (Generic Desktop Ctrls) */
	0x09, 0x80,        /* Usage (Sys Control)                */
	0xA1, 0x01,        /* Collection (Application)           */
	0x85, 0x02,        /*   Report ID (2)                    */
	0x75, 0x01,        /*   Report Size (1)                  */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x15, 0x00,        /*   Logical Minimum (0)              */
	0x25, 0x01,        /*   Logical Maximum (1)              */
	0x09, 0x82,        /*   Usage (Sys Sleep)                */
	0x81, 0x06,        /*   Input (Data,Var,Rel)             */
	0x09, 0x82,        /*   Usage (Sys Sleep)                */
	0x81, 0x06,        /*   Input (Data,Var,Rel)             */
	0x09, 0x83,        /*   Usage (Sys Wake Up)              */
	0x81, 0x06,        /*   Input (Data,Var,Rel)             */
	0x75, 0x05,        /*   Report Size (5)                  */
	0x81, 0x01,        /*   Input (Const,Array,Abs)          */
	0xC0,              /* End Collection                     */
	0x05, 0x0C,        /* Usage Page (Consumer)              */
	0x09, 0x01,        /* Usage (Consumer Control)           */
	0xA1, 0x01,        /* Collection (Application)           */
	0x85, 0x03,        /*   Report ID (3)                    */
	0x15, 0x00,        /*   Logical Minimum (0)              - changed */
	0x26, 0xFF, 0x7F,  /*   Logical Maximum (32767)          - changed */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x75, 0x10,        /*   Report Size (16)                 */
	0x19, 0x00,        /*   Usage Minimum (Unassigned)       */
	0x2A, 0xFF, 0x7F,  /*   Usage Maximum (0x7FFF)           */
	0x81, 0x00,        /*   Input (Data,Array,Abs)           */
	0xC0,              /* End Collection                     */
	0x06, 0x7F, 0xFF,  /* Usage Page (Vendor Defined 0xFF7F) */
	0x09, 0x01,        /* Usage (0x01)                       */
	0xA1, 0x01,        /* Collection (Application)           */
	0x85, 0x04,        /*   Report ID (4)                    */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x75, 0x10,        /*   Report Size (16)                 */
	0x19, 0x00,        /*   Usage Minimum (0x00)             */
	0x2A, 0xFF, 0x7F,  /*   Usage Maximum (0x7FFF)           */
	0x81, 0x00,        /*   Input (Data,Array,Abs)           */
	0x75, 0x02,        /*   Report Size (2)                  */
	0x25, 0x02,        /*   Logical Maximum (2)              */
	0x09, 0x90,        /*   Usage (0x90)                     */
	0xB1, 0x02,        /*   Feature (Data,Var,Abs)           */
	0x75, 0x06,        /*   Report Size (6)                  */
	0xB1, 0x01,        /*   Feature (Const,Array,Abs)        */
	0x75, 0x01,        /*   Report Size (1)                  */
	0x25, 0x01,        /*   Logical Maximum (1)              */
	0x05, 0x08,        /*   Usage Page (LEDs)                */
	0x09, 0x2A,        /*   Usage (On-Line)                  */
	0x91, 0x02,        /*   Output (Data,Var,Abs)            */
	0x09, 0x4B,        /*   Usage (Generic Indicator)        */
	0x91, 0x02,        /*   Output (Data,Var,Abs)            */
	0x75, 0x06,        /*   Report Size (6)                  */
	0x95, 0x01,        /*   Report Count (1)                 */
	0x91, 0x01,        /*   Output (Const,Array,Abs)         */
	0xC0               /* End Collection                     */
};

static __u8 *maltron_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				  unsigned int *rsize)
{
	if (*rsize == sizeof(maltron_rdesc_o) &&
	    !memcmp(maltron_rdesc_o, rdesc, sizeof(maltron_rdesc_o))) {
		hid_info(hdev, "Replacing Maltron L90 keyboard report descriptor\n");
		*rsize = sizeof(maltron_rdesc);
		return maltron_rdesc;
	}
	return rdesc;
}

static const struct hid_device_id maltron_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ALCOR, USB_DEVICE_ID_ALCOR_MALTRON_KB)},
	{ }
};
MODULE_DEVICE_TABLE(hid, maltron_devices);

static struct hid_driver maltron_driver = {
	.name = "maltron",
	.id_table = maltron_devices,
	.report_fixup = maltron_report_fixup
};
module_hid_driver(maltron_driver);

MODULE_LICENSE("GPL");

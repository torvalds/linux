// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for SiGma Micro-based keyboards
 *
 * Copyright (c) 2016 Kinglong Mee
 * Copyright (c) 2021 Desmond Lim
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

static const __u8 sm_0059_rdesc[] = {
	0x05, 0x0c,              /* Usage Page (Consumer Devices)       0   */
	0x09, 0x01,              /* Usage (Consumer Control)            2   */
	0xa1, 0x01,              /* Collection (Application)            4   */
	0x85, 0x01,              /*  Report ID (1)                      6   */
	0x19, 0x00,              /*  Usage Minimum (0)                  8   */
	0x2a, 0x3c, 0x02,        /*  Usage Maximum (572)                10  */
	0x15, 0x00,              /*  Logical Minimum (0)                13  */
	0x26, 0x3c, 0x02,        /*  Logical Maximum (572)              15  */
	0x95, 0x01,              /*  Report Count (1)                   18  */
	0x75, 0x10,              /*  Report Size (16)                   20  */
	0x81, 0x00,              /*  Input (Data,Arr,Abs)               22  */
	0xc0,                    /* End Collection                      24  */
	0x05, 0x01,              /* Usage Page (Generic Desktop)        25  */
	0x09, 0x80,              /* Usage (System Control)              27  */
	0xa1, 0x01,              /* Collection (Application)            29  */
	0x85, 0x02,              /*  Report ID (2)                      31  */
	0x19, 0x81,              /*  Usage Minimum (129)                33  */
	0x29, 0x83,              /*  Usage Maximum (131)                35  */
	0x25, 0x01,              /*  Logical Maximum (1)                37  */
	0x75, 0x01,              /*  Report Size (1)                    39  */
	0x95, 0x03,              /*  Report Count (3)                   41  */
	0x81, 0x02,              /*  Input (Data,Var,Abs)               43  */
	0x95, 0x05,              /*  Report Count (5)                   45  */
	0x81, 0x01,              /*  Input (Cnst,Arr,Abs)               47  */
	0xc0,                    /* End Collection                      49  */
	0x06, 0x00, 0xff,        /* Usage Page (Vendor Defined Page 1)  50  */
	0x09, 0x01,              /* Usage (Vendor Usage 1)              53  */
	0xa1, 0x01,              /* Collection (Application)            55  */
	0x85, 0x03,              /*  Report ID (3)                      57  */
	0x1a, 0xf1, 0x00,        /*  Usage Minimum (241)                59  */
	0x2a, 0xf8, 0x00,        /*  Usage Maximum (248)                62  */
	0x15, 0x00,              /*  Logical Minimum (0)                65  */
	0x25, 0x01,              /*  Logical Maximum (1)                67  */
	0x75, 0x01,              /*  Report Size (1)                    69  */
	0x95, 0x08,              /*  Report Count (8)                   71  */
	0x81, 0x02,              /*  Input (Data,Var,Abs)               73  */
	0xc0,                    /* End Collection                      75  */
	0x05, 0x01,              /* Usage Page (Generic Desktop)        76  */
	0x09, 0x06,              /* Usage (Keyboard)                    78  */
	0xa1, 0x01,              /* Collection (Application)            80  */
	0x85, 0x04,              /*  Report ID (4)                      82  */
	0x05, 0x07,              /*  Usage Page (Keyboard)              84  */
	0x19, 0xe0,              /*  Usage Minimum (224)                86  */
	0x29, 0xe7,              /*  Usage Maximum (231)                88  */
	0x15, 0x00,              /*  Logical Minimum (0)                90  */
	0x25, 0x01,              /*  Logical Maximum (1)                92  */
	0x75, 0x01,              /*  Report Size (1)                    94  */
	0x95, 0x08,              /*  Report Count (8)                   96  */
	0x81, 0x00,              /*  Input (Data,Arr,Abs)               98  */
	0x95, 0x30,              /*  Report Count (48)                  100 */
	0x75, 0x01,              /*  Report Size (1)                    102 */
	0x15, 0x00,              /*  Logical Minimum (0)                104 */
	0x25, 0x01,              /*  Logical Maximum (1)                106 */
	0x05, 0x07,              /*  Usage Page (Keyboard)              108 */
	0x19, 0x00,              /*  Usage Minimum (0)                  110 */
	0x29, 0x2f,              /*  Usage Maximum (47)                 112 */
	0x81, 0x02,              /*  Input (Data,Var,Abs)               114 */
	0xc0,                    /* End Collection                      116 */
	0x05, 0x01,              /* Usage Page (Generic Desktop)        117 */
	0x09, 0x06,              /* Usage (Keyboard)                    119 */
	0xa1, 0x01,              /* Collection (Application)            121 */
	0x85, 0x05,              /*  Report ID (5)                      123 */
	0x95, 0x38,              /*  Report Count (56)                  125 */
	0x75, 0x01,              /*  Report Size (1)                    127 */
	0x15, 0x00,              /*  Logical Minimum (0)                129 */
	0x25, 0x01,              /*  Logical Maximum (1)                131 */
	0x05, 0x07,              /*  Usage Page (Keyboard)              133 */
	0x19, 0x30,              /*  Usage Minimum (48)                 135 */
	0x29, 0x67,              /*  Usage Maximum (103)                137 */
	0x81, 0x02,              /*  Input (Data,Var,Abs)               139 */
	0xc0,                    /* End Collection                      141 */
	0x05, 0x01,              /* Usage Page (Generic Desktop)        142 */
	0x09, 0x06,              /* Usage (Keyboard)                    144 */
	0xa1, 0x01,              /* Collection (Application)            146 */
	0x85, 0x06,              /*  Report ID (6)                      148 */
	0x95, 0x38,              /*  Report Count (56)                  150 */
	0x75, 0x01,              /*  Report Size (1)                    152 */
	0x15, 0x00,              /*  Logical Minimum (0)                154 */
	0x25, 0x01,              /*  Logical Maximum (1)                156 */
	0x05, 0x07,              /*  Usage Page (Keyboard)              158 */
	0x19, 0x68,              /*  Usage Minimum (104)                160 */
	0x29, 0x9f,              /*  Usage Maximum (159)                162 */
	0x81, 0x02,              /*  Input (Data,Var,Abs)               164 */
	0xc0,                    /* End Collection                      166 */
};

static __u8 *sm_report_fixup(struct hid_device *hdev, __u8 *rdesc,
			     unsigned int *rsize)
{
	if (*rsize == sizeof(sm_0059_rdesc) &&
	    !memcmp(sm_0059_rdesc, rdesc, *rsize)) {
		hid_info(hdev, "Fixing up SiGma Micro report descriptor\n");
		rdesc[99] = 0x02;
	}
	return rdesc;
}

static const struct hid_device_id sm_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SIGMA_MICRO,
			 USB_DEVICE_ID_SIGMA_MICRO_KEYBOARD2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, sm_devices);

static struct hid_driver sm_driver = {
	.name = "sigmamicro",
	.id_table = sm_devices,
	.report_fixup = sm_report_fixup,
};
module_hid_driver(sm_driver);

MODULE_AUTHOR("Kinglong Mee <kinglongmee@gmail.com>");
MODULE_AUTHOR("Desmond Lim <peckishrine@gmail.com>");
MODULE_DESCRIPTION("SiGma Micro HID driver");
MODULE_LICENSE("GPL");

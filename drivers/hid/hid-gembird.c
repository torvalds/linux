// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Gembird Joypad, "PC Game Controller"
 *
 *  Copyright (c) 2015 Red Hat, Inc
 *  Copyright (c) 2015 Benjamin Tissoires
 */

/*
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define GEMBIRD_START_FAULTY_RDESC	8

static const __u8 gembird_jpd_faulty_rdesc[] = {
	0x75, 0x08,			/*   Report Size (8)		*/
	0x95, 0x05,			/*   Report Count (5)		*/
	0x15, 0x00,			/*   Logical Minimum (0)	*/
	0x26, 0xff, 0x00,		/*   Logical Maximum (255)	*/
	0x35, 0x00,			/*   Physical Minimum (0)	*/
	0x46, 0xff, 0x00,		/*   Physical Maximum (255)	*/
	0x09, 0x30,			/*   Usage (X)			*/
	0x09, 0x31,			/*   Usage (Y)			*/
	0x09, 0x32,			/*   Usage (Z)			*/
	0x09, 0x32,			/*   Usage (Z)			*/
	0x09, 0x35,			/*   Usage (Rz)			*/
	0x81, 0x02,			/*   Input (Data,Var,Abs)	*/
};

/*
 * we fix the report descriptor by:
 * - marking the first Z axis as constant (so it is ignored by HID)
 * - assign the original second Z to Rx
 * - assign the original Rz to Ry
 */
static const __u8 gembird_jpd_fixed_rdesc[] = {
	0x75, 0x08,			/*   Report Size (8)		*/
	0x95, 0x02,			/*   Report Count (2)		*/
	0x15, 0x00,			/*   Logical Minimum (0)	*/
	0x26, 0xff, 0x00,		/*   Logical Maximum (255)	*/
	0x35, 0x00,			/*   Physical Minimum (0)	*/
	0x46, 0xff, 0x00,		/*   Physical Maximum (255)	*/
	0x09, 0x30,			/*   Usage (X)			*/
	0x09, 0x31,			/*   Usage (Y)			*/
	0x81, 0x02,			/*   Input (Data,Var,Abs)	*/
	0x95, 0x01,			/*   Report Count (1)		*/
	0x09, 0x32,			/*   Usage (Z)			*/
	0x81, 0x01,			/*   Input (Cnst,Arr,Abs)	*/
	0x95, 0x02,			/*   Report Count (2)		*/
	0x09, 0x33,			/*   Usage (Rx)			*/
	0x09, 0x34,			/*   Usage (Ry)			*/
	0x81, 0x02,			/*   Input (Data,Var,Abs)	*/
};

static const __u8 *gembird_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	__u8 *new_rdesc;
	/* delta_size is > 0 */
	size_t delta_size = sizeof(gembird_jpd_fixed_rdesc) -
			    sizeof(gembird_jpd_faulty_rdesc);
	size_t new_size = *rsize + delta_size;

	if (*rsize >= 31 && !memcmp(&rdesc[GEMBIRD_START_FAULTY_RDESC],
				    gembird_jpd_faulty_rdesc,
				    sizeof(gembird_jpd_faulty_rdesc))) {
		new_rdesc = devm_kzalloc(&hdev->dev, new_size, GFP_KERNEL);
		if (new_rdesc == NULL)
			return rdesc;

		dev_info(&hdev->dev,
			 "fixing Gembird JPD-DualForce 2 report descriptor.\n");

		/* start by copying the end of the rdesc */
		memcpy(new_rdesc + delta_size, rdesc, *rsize);

		/* add the correct beginning */
		memcpy(new_rdesc, rdesc, GEMBIRD_START_FAULTY_RDESC);

		/* replace the faulty part with the fixed one */
		memcpy(new_rdesc + GEMBIRD_START_FAULTY_RDESC,
		       gembird_jpd_fixed_rdesc,
		       sizeof(gembird_jpd_fixed_rdesc));

		*rsize = new_size;
		rdesc = new_rdesc;
	}

	return rdesc;
}

static const struct hid_device_id gembird_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GEMBIRD,
			 USB_DEVICE_ID_GEMBIRD_JPD_DUALFORCE2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, gembird_devices);

static struct hid_driver gembird_driver = {
	.name = "gembird",
	.id_table = gembird_devices,
	.report_fixup = gembird_report_fixup,
};
module_hid_driver(gembird_driver);

MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_DESCRIPTION("HID Gembird joypad driver");
MODULE_LICENSE("GPL");

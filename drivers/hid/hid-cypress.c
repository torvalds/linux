// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for some cypress "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby
 */

/*
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>

#include "hid-ids.h"

#define CP_RDESC_SWAPPED_MIN_MAX	0x01
#define CP_2WHEEL_MOUSE_HACK		0x02
#define CP_2WHEEL_MOUSE_HACK_ON		0x04

#define VA_INVAL_LOGICAL_BOUNDARY	0x08

/*
 * Some USB barcode readers from cypress have usage min and usage max in
 * the wrong order
 */
static __u8 *cp_rdesc_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	unsigned int i;

	if (*rsize < 4)
		return rdesc;

	for (i = 0; i < *rsize - 4; i++)
		if (rdesc[i] == 0x29 && rdesc[i + 2] == 0x19) {
			rdesc[i] = 0x19;
			rdesc[i + 2] = 0x29;
			swap(rdesc[i + 3], rdesc[i + 1]);
		}
	return rdesc;
}

static __u8 *va_logical_boundary_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	/*
	 * Varmilo VA104M (with VID Cypress and device ID 07B1) incorrectly
	 * reports Logical Minimum of its Consumer Control device as 572
	 * (0x02 0x3c). Fix this by setting its Logical Minimum to zero.
	 */
	if (*rsize == 25 &&
			rdesc[0] == 0x05 && rdesc[1] == 0x0c &&
			rdesc[2] == 0x09 && rdesc[3] == 0x01 &&
			rdesc[6] == 0x19 && rdesc[7] == 0x00 &&
			rdesc[11] == 0x16 && rdesc[12] == 0x3c && rdesc[13] == 0x02) {
		hid_info(hdev,
			 "fixing up varmilo VA104M consumer control report descriptor\n");
		rdesc[12] = 0x00;
		rdesc[13] = 0x00;
	}
	return rdesc;
}

static __u8 *cp_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if (quirks & CP_RDESC_SWAPPED_MIN_MAX)
		rdesc = cp_rdesc_fixup(hdev, rdesc, rsize);
	if (quirks & VA_INVAL_LOGICAL_BOUNDARY)
		rdesc = va_logical_boundary_fixup(hdev, rdesc, rsize);

	return rdesc;
}

static int cp_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if (!(quirks & CP_2WHEEL_MOUSE_HACK))
		return 0;

	if (usage->type == EV_REL && usage->code == REL_WHEEL)
		set_bit(REL_HWHEEL, *bit);
	if (usage->hid == 0x00090005)
		return -1;

	return 0;
}

static int cp_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput ||
			!usage->type || !(quirks & CP_2WHEEL_MOUSE_HACK))
		return 0;

	if (usage->hid == 0x00090005) {
		if (value)
			quirks |=  CP_2WHEEL_MOUSE_HACK_ON;
		else
			quirks &= ~CP_2WHEEL_MOUSE_HACK_ON;
		hid_set_drvdata(hdev, (void *)quirks);
		return 1;
	}

	if (usage->code == REL_WHEEL && (quirks & CP_2WHEEL_MOUSE_HACK_ON)) {
		struct input_dev *input = field->hidinput->input;

		input_event(input, usage->type, REL_HWHEEL, value);
		return 1;
	}

	return 0;
}

static int cp_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	int ret;

	hid_set_drvdata(hdev, (void *)quirks);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	return 0;
err_free:
	return ret;
}

static const struct hid_device_id cp_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_BARCODE_1),
		.driver_data = CP_RDESC_SWAPPED_MIN_MAX },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_BARCODE_2),
		.driver_data = CP_RDESC_SWAPPED_MIN_MAX },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_BARCODE_3),
		.driver_data = CP_RDESC_SWAPPED_MIN_MAX },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_BARCODE_4),
		.driver_data = CP_RDESC_SWAPPED_MIN_MAX },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_MOUSE),
		.driver_data = CP_2WHEEL_MOUSE_HACK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_VARMILO_VA104M_07B1),
		.driver_data = VA_INVAL_LOGICAL_BOUNDARY },
	{ }
};
MODULE_DEVICE_TABLE(hid, cp_devices);

static struct hid_driver cp_driver = {
	.name = "cypress",
	.id_table = cp_devices,
	.report_fixup = cp_report_fixup,
	.input_mapped = cp_input_mapped,
	.event = cp_event,
	.probe = cp_probe,
};
module_hid_driver(cp_driver);

MODULE_DESCRIPTION("HID driver for some cypress \"special\" devices");
MODULE_LICENSE("GPL");

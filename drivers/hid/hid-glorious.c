// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  USB HID driver for Glorious PC Gaming Race
 *  Glorious Model O, O- and D mice.
 *
 *  Copyright (c) 2020 Samuel Čavoj <sammko@sammserver.com>
 */

/*
 */

#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

MODULE_AUTHOR("Samuel Čavoj <sammko@sammserver.com>");
MODULE_DESCRIPTION("HID driver for Glorious PC Gaming Race mice");

/*
 * Glorious Model O and O- specify the const flag in the consumer input
 * report descriptor, which leads to inputs being ignored. Fix this
 * by patching the descriptor.
 *
 * Glorious Model I incorrectly specifes the Usage Minimum for its
 * keyboard HID report, causing keycodes to be misinterpreted.
 * Fix this by setting Usage Minimum to 0 in that report.
 */
static const __u8 *glorious_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize == 213 &&
		rdesc[84] == 129 && rdesc[112] == 129 && rdesc[140] == 129 &&
		rdesc[85] == 3   && rdesc[113] == 3   && rdesc[141] == 3) {
		hid_info(hdev, "patching Glorious Model O consumer control report descriptor\n");
		rdesc[85] = rdesc[113] = rdesc[141] = \
			HID_MAIN_ITEM_VARIABLE | HID_MAIN_ITEM_RELATIVE;
	}
	if (*rsize == 156 && rdesc[41] == 1) {
		hid_info(hdev, "patching Glorious Model I keyboard report descriptor\n");
		rdesc[41] = 0;
	}
	return rdesc;
}

static void glorious_update_name(struct hid_device *hdev)
{
	const char *model = "Device";

	switch (hdev->product) {
	case USB_DEVICE_ID_GLORIOUS_MODEL_O:
		model = "Model O"; break;
	case USB_DEVICE_ID_GLORIOUS_MODEL_D:
		model = "Model D"; break;
	case USB_DEVICE_ID_GLORIOUS_MODEL_I:
		model = "Model I"; break;
	}

	snprintf(hdev->name, sizeof(hdev->name), "%s %s", "Glorious", model);
}

static int glorious_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;

	hdev->quirks |= HID_QUIRK_INPUT_PER_APP;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	glorious_update_name(hdev);

	return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
}

static const struct hid_device_id glorious_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SINOWEALTH,
		USB_DEVICE_ID_GLORIOUS_MODEL_O) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SINOWEALTH,
		USB_DEVICE_ID_GLORIOUS_MODEL_D) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LAVIEW,
		USB_DEVICE_ID_GLORIOUS_MODEL_I) },
	{ }
};
MODULE_DEVICE_TABLE(hid, glorious_devices);

static struct hid_driver glorious_driver = {
	.name = "glorious",
	.id_table = glorious_devices,
	.probe = glorious_probe,
	.report_fixup = glorious_report_fixup
};

module_hid_driver(glorious_driver);

MODULE_LICENSE("GPL");

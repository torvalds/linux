/*
 *  Force feedback support for PantherLord/GreenAsia based devices
 *
 *  The devices are distributed under various names and the same USB device ID
 *  can be used in both adapters and actual game controllers.
 *
 *  0810:0001 "Twin USB Joystick"
 *   - tested with PantherLord USB/PS2 2in1 Adapter
 *   - contains two reports, one for each port (HID_QUIRK_MULTI_INPUT)
 *
 *  0e8f:0003 "GreenAsia Inc.    USB Joystick     "
 *   - tested with König Gaming gamepad
 *
 *  0e8f:0003 "GASIA USB Gamepad"
 *   - another version of the König gamepad
 *
 *  0f30:0111 "Saitek Color Rumble Pad"
 *
 *  Copyright (c) 2007, 2009 Anssi Hannula <anssi.hannula@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


/* #define DEBUG */

#define debug(format, arg...) pr_debug("hid-plff: " format "\n" , ## arg)

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hid.h>

#include "hid-ids.h"

#ifdef CONFIG_PANTHERLORD_FF
#include "usbhid/usbhid.h"

struct plff_device {
	struct hid_report *report;
	s32 maxval;
	s32 *strong;
	s32 *weak;
};

static int hid_plff_play(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct plff_device *plff = data;
	int left, right;

	left = effect->u.rumble.strong_magnitude;
	right = effect->u.rumble.weak_magnitude;
	debug("called with 0x%04x 0x%04x", left, right);

	left = left * plff->maxval / 0xffff;
	right = right * plff->maxval / 0xffff;

	*plff->strong = left;
	*plff->weak = right;
	debug("running with 0x%02x 0x%02x", left, right);
	usbhid_submit_report(hid, plff->report, USB_DIR_OUT);

	return 0;
}

static int plff_init(struct hid_device *hid)
{
	struct plff_device *plff;
	struct hid_report *report;
	struct hid_input *hidinput;
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct list_head *report_ptr = report_list;
	struct input_dev *dev;
	int error;
	s32 maxval;
	s32 *strong;
	s32 *weak;

	/* The device contains one output report per physical device, all
	   containing 1 field, which contains 4 ff00.0002 usages and 4 16bit
	   absolute values.

	   The input reports also contain a field which contains
	   8 ff00.0001 usages and 8 boolean values. Their meaning is
	   currently unknown.
	   
	   A version of the 0e8f:0003 exists that has all the values in
	   separate fields and misses the extra input field, thus resembling
	   Zeroplus (hid-zpff) devices.
	*/

	if (list_empty(report_list)) {
		hid_err(hid, "no output reports found\n");
		return -ENODEV;
	}

	list_for_each_entry(hidinput, &hid->inputs, list) {

		report_ptr = report_ptr->next;

		if (report_ptr == report_list) {
			hid_err(hid, "required output report is missing\n");
			return -ENODEV;
		}

		report = list_entry(report_ptr, struct hid_report, list);
		if (report->maxfield < 1) {
			hid_err(hid, "no fields in the report\n");
			return -ENODEV;
		}

		maxval = 0x7f;
		if (report->field[0]->report_count >= 4) {
			report->field[0]->value[0] = 0x00;
			report->field[0]->value[1] = 0x00;
			strong = &report->field[0]->value[2];
			weak = &report->field[0]->value[3];
			debug("detected single-field device");
		} else if (report->maxfield >= 4 && report->field[0]->maxusage == 1 &&
				report->field[0]->usage[0].hid == (HID_UP_LED | 0x43)) {
			report->field[0]->value[0] = 0x00;
			report->field[1]->value[0] = 0x00;
			strong = &report->field[2]->value[0];
			weak = &report->field[3]->value[0];
			if (hid->vendor == USB_VENDOR_ID_JESS2)
				maxval = 0xff;
			debug("detected 4-field device");
		} else {
			hid_err(hid, "not enough fields or values\n");
			return -ENODEV;
		}

		plff = kzalloc(sizeof(struct plff_device), GFP_KERNEL);
		if (!plff)
			return -ENOMEM;

		dev = hidinput->input;

		set_bit(FF_RUMBLE, dev->ffbit);

		error = input_ff_create_memless(dev, plff, hid_plff_play);
		if (error) {
			kfree(plff);
			return error;
		}

		plff->report = report;
		plff->strong = strong;
		plff->weak = weak;
		plff->maxval = maxval;

		*strong = 0x00;
		*weak = 0x00;
		usbhid_submit_report(hid, plff->report, USB_DIR_OUT);
	}

	hid_info(hid, "Force feedback for PantherLord/GreenAsia devices by Anssi Hannula <anssi.hannula@gmail.com>\n");

	return 0;
}
#else
static inline int plff_init(struct hid_device *hid)
{
	return 0;
}
#endif

static int pl_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	if (id->driver_data)
		hdev->quirks |= HID_QUIRK_MULTI_INPUT;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err;
	}

	plff_init(hdev);

	return 0;
err:
	return ret;
}

static const struct hid_device_id pl_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GAMERON, USB_DEVICE_ID_GAMERON_DUAL_PSX_ADAPTOR),
		.driver_data = 1 }, /* Twin USB Joystick */
	{ HID_USB_DEVICE(USB_VENDOR_ID_GAMERON, USB_DEVICE_ID_GAMERON_DUAL_PCS_ADAPTOR),
		.driver_data = 1 }, /* Twin USB Joystick */
	{ HID_USB_DEVICE(USB_VENDOR_ID_GREENASIA, 0x0003), },
	{ HID_USB_DEVICE(USB_VENDOR_ID_JESS2, USB_DEVICE_ID_JESS2_COLOR_RUMBLE_PAD), },
	{ }
};
MODULE_DEVICE_TABLE(hid, pl_devices);

static struct hid_driver pl_driver = {
	.name = "pantherlord",
	.id_table = pl_devices,
	.probe = pl_probe,
};
module_hid_driver(pl_driver);

MODULE_LICENSE("GPL");

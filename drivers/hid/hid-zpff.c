/*
 *  Force feedback support for Zeroplus based devices
 *
 *  Copyright (c) 2005, 2006 Anssi Hannula <anssi.hannula@gmail.com>
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


#include <linux/hid.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "hid-ids.h"

#ifdef CONFIG_ZEROPLUS_FF

struct zpff_device {
	struct hid_report *report;
};

static int zpff_play(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct zpff_device *zpff = data;
	int left, right;

	/*
	 * The following is specified the other way around in the Zeroplus
	 * datasheet but the order below is correct for the XFX Executioner;
	 * however it is possible that the XFX Executioner is an exception
	 */

	left = effect->u.rumble.strong_magnitude;
	right = effect->u.rumble.weak_magnitude;
	dbg_hid("called with 0x%04x 0x%04x\n", left, right);

	left = left * 0x7f / 0xffff;
	right = right * 0x7f / 0xffff;

	zpff->report->field[2]->value[0] = left;
	zpff->report->field[3]->value[0] = right;
	dbg_hid("running with 0x%02x 0x%02x\n", left, right);
	hid_hw_request(hid, zpff->report, HID_REQ_SET_REPORT);

	return 0;
}

static int zpff_init(struct hid_device *hid)
{
	struct zpff_device *zpff;
	struct hid_report *report;
	struct hid_input *hidinput;
	struct input_dev *dev;
	int i, error;

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		return -ENODEV;
	}
	hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	dev = hidinput->input;

	for (i = 0; i < 4; i++) {
		report = hid_validate_values(hid, HID_OUTPUT_REPORT, 0, i, 1);
		if (!report)
			return -ENODEV;
	}

	zpff = kzalloc(sizeof(struct zpff_device), GFP_KERNEL);
	if (!zpff)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, zpff, zpff_play);
	if (error) {
		kfree(zpff);
		return error;
	}

	zpff->report = report;
	zpff->report->field[0]->value[0] = 0x00;
	zpff->report->field[1]->value[0] = 0x02;
	zpff->report->field[2]->value[0] = 0x00;
	zpff->report->field[3]->value[0] = 0x00;
	hid_hw_request(hid, zpff->report, HID_REQ_SET_REPORT);

	hid_info(hid, "force feedback for Zeroplus based devices by Anssi Hannula <anssi.hannula@gmail.com>\n");

	return 0;
}
#else
static inline int zpff_init(struct hid_device *hid)
{
	return 0;
}
#endif

static int zp_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

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

	zpff_init(hdev);

	return 0;
err:
	return ret;
}

static const struct hid_device_id zp_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ZEROPLUS, 0x0005) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ZEROPLUS, 0x0030) },
	{ }
};
MODULE_DEVICE_TABLE(hid, zp_devices);

static struct hid_driver zp_driver = {
	.name = "zeroplus",
	.id_table = zp_devices,
	.probe = zp_probe,
};
module_hid_driver(zp_driver);

MODULE_LICENSE("GPL");

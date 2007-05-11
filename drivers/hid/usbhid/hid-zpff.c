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


/* #define DEBUG */

#define debug(format, arg...) pr_debug("hid-zpff: " format "\n" , ## arg)

#include <linux/input.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include "usbhid.h"

struct zpff_device {
	struct hid_report *report;
};

static int hid_zpff_play(struct input_dev *dev, void *data,
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
	debug("called with 0x%04x 0x%04x", left, right);

	left = left * 0x7f / 0xffff;
	right = right * 0x7f / 0xffff;

	zpff->report->field[2]->value[0] = left;
	zpff->report->field[3]->value[0] = right;
	debug("running with 0x%02x 0x%02x", left, right);
	usbhid_submit_report(hid, zpff->report, USB_DIR_OUT);

	return 0;
}

int hid_zpff_init(struct hid_device *hid)
{
	struct zpff_device *zpff;
	struct hid_report *report;
	struct hid_input *hidinput = list_entry(hid->inputs.next,
						struct hid_input, list);
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev = hidinput->input;
	int error;

	if (list_empty(report_list)) {
		printk(KERN_ERR "hid-zpff: no output report found\n");
		return -ENODEV;
	}

	report = list_entry(report_list->next, struct hid_report, list);

	if (report->maxfield < 4) {
		printk(KERN_ERR "hid-zpff: not enough fields in report\n");
		return -ENODEV;
	}

	zpff = kzalloc(sizeof(struct zpff_device), GFP_KERNEL);
	if (!zpff)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, zpff, hid_zpff_play);
	if (error) {
		kfree(zpff);
		return error;
	}

	zpff->report = report;
	zpff->report->field[0]->value[0] = 0x00;
	zpff->report->field[1]->value[0] = 0x02;
	zpff->report->field[2]->value[0] = 0x00;
	zpff->report->field[3]->value[0] = 0x00;
	usbhid_submit_report(hid, zpff->report, USB_DIR_OUT);

	printk(KERN_INFO "Force feedback for Zeroplus based devices by "
	       "Anssi Hannula <anssi.hannula@gmail.com>\n");

	return 0;
}

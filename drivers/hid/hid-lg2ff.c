/*
 *  Force feedback support for Logitech RumblePad and Rumblepad 2
 *
 *  Copyright (c) 2008 Anssi Hannula <anssi.hannula@gmail.com>
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


#include <linux/input.h>
#include <linux/slab.h>
#include <linux/hid.h>

#include "hid-lg.h"

struct lg2ff_device {
	struct hid_report *report;
};

static int play_effect(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct lg2ff_device *lg2ff = data;
	int weak, strong;

	strong = effect->u.rumble.strong_magnitude;
	weak = effect->u.rumble.weak_magnitude;

	if (weak || strong) {
		weak = weak * 0xff / 0xffff;
		strong = strong * 0xff / 0xffff;

		lg2ff->report->field[0]->value[0] = 0x51;
		lg2ff->report->field[0]->value[2] = weak;
		lg2ff->report->field[0]->value[4] = strong;
	} else {
		lg2ff->report->field[0]->value[0] = 0xf3;
		lg2ff->report->field[0]->value[2] = 0x00;
		lg2ff->report->field[0]->value[4] = 0x00;
	}

	hid_hw_request(hid, lg2ff->report, HID_REQ_SET_REPORT);
	return 0;
}

int lg2ff_init(struct hid_device *hid)
{
	struct lg2ff_device *lg2ff;
	struct hid_report *report;
	struct hid_input *hidinput = list_entry(hid->inputs.next,
						struct hid_input, list);
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev = hidinput->input;
	int error;

	if (list_empty(report_list)) {
		hid_err(hid, "no output report found\n");
		return -ENODEV;
	}

	report = list_entry(report_list->next, struct hid_report, list);

	if (report->maxfield < 1) {
		hid_err(hid, "output report is empty\n");
		return -ENODEV;
	}
	if (report->field[0]->report_count < 7) {
		hid_err(hid, "not enough values in the field\n");
		return -ENODEV;
	}

	lg2ff = kmalloc(sizeof(struct lg2ff_device), GFP_KERNEL);
	if (!lg2ff)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, lg2ff, play_effect);
	if (error) {
		kfree(lg2ff);
		return error;
	}

	lg2ff->report = report;
	report->field[0]->value[0] = 0xf3;
	report->field[0]->value[1] = 0x00;
	report->field[0]->value[2] = 0x00;
	report->field[0]->value[3] = 0x00;
	report->field[0]->value[4] = 0x00;
	report->field[0]->value[5] = 0x00;
	report->field[0]->value[6] = 0x00;

	hid_hw_request(hid, report, HID_REQ_SET_REPORT);

	hid_info(hid, "Force feedback for Logitech RumblePad/Rumblepad 2 by Anssi Hannula <anssi.hannula@gmail.com>\n");

	return 0;
}

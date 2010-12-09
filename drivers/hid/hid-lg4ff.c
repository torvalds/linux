/*
 *  Force feedback support for Logitech Speed Force Wireless
 *
 *  http://wiibrew.org/wiki/Logitech_USB_steering_wheel
 *
 *  Copyright (c) 2010 Simon Wood <simon@mungewell.org>
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
#include <linux/usb.h>
#include <linux/hid.h>

#include "usbhid/usbhid.h"
#include "hid-lg.h"

struct lg4ff_device {
	struct hid_report *report;
};

static const signed short ff4_wheel_ac[] = {
	FF_CONSTANT,
	FF_AUTOCENTER,
	-1
};

static int hid_lg4ff_play(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	int x;

#define CLAMP(x) if (x < 0) x = 0; if (x > 0xff) x = 0xff

	switch (effect->type) {
	case FF_CONSTANT:
		x = effect->u.ramp.start_level + 0x80;	/* 0x80 is no force */
		CLAMP(x);
		report->field[0]->value[0] = 0x11;	/* Slot 1 */
		report->field[0]->value[1] = 0x10;
		report->field[0]->value[2] = x;
		report->field[0]->value[3] = 0x00;
		report->field[0]->value[4] = 0x00;
		report->field[0]->value[5] = 0x08;
		report->field[0]->value[6] = 0x00;
		dbg_hid("Autocenter, x=0x%02X\n", x);

		usbhid_submit_report(hid, report, USB_DIR_OUT);
		break;
	}
	return 0;
}

static void hid_lg4ff_set_autocenter(struct input_dev *dev, u16 magnitude)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;

	*value++ = 0xfe;
	*value++ = 0x0d;
	*value++ = 0x07;
	*value++ = 0x07;
	*value++ = (magnitude >> 8) & 0xff;
	*value++ = 0x00;
	*value = 0x00;

	usbhid_submit_report(hid, report, USB_DIR_OUT);
}


int lg4ff_init(struct hid_device *hid)
{
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev = hidinput->input;
	struct hid_report *report;
	struct hid_field *field;
	const signed short *ff_bits = ff4_wheel_ac;
	int error;
	int i;

	/* Find the report to use */
	if (list_empty(report_list)) {
		err_hid("No output report found");
		return -1;
	}

	/* Check that the report looks ok */
	report = list_entry(report_list->next, struct hid_report, list);
	if (!report) {
		err_hid("NULL output report");
		return -1;
	}

	field = report->field[0];
	if (!field) {
		err_hid("NULL field");
		return -1;
	}

	for (i = 0; ff_bits[i] >= 0; i++)
		set_bit(ff_bits[i], dev->ffbit);

	error = input_ff_create_memless(dev, NULL, hid_lg4ff_play);

	if (error)
		return error;

	if (test_bit(FF_AUTOCENTER, dev->ffbit))
		dev->ff->set_autocenter = hid_lg4ff_set_autocenter;

	dev_info(&hid->dev, "Force feedback for Logitech Speed Force Wireless by "
			"Simon Wood <simon@mungewell.org>\n");
	return 0;
}


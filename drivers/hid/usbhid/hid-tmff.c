/*
 * Force feedback support for various HID compliant devices by ThrustMaster:
 *    ThrustMaster FireStorm Dual Power 2
 * and possibly others whose device ids haven't been added.
 *
 *  Modified to support ThrustMaster devices by Zinx Verituse
 *  on 2003-01-25 from the Logitech force feedback driver,
 *  which is by Johann Deneux.
 *
 *  Copyright (c) 2003 Zinx Verituse <zinx@epicsol.org>
 *  Copyright (c) 2002 Johann Deneux
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

#undef DEBUG
#include <linux/usb.h>

#include <linux/hid.h>
#include "usbhid.h"

/* Usages for thrustmaster devices I know about */
#define THRUSTMASTER_USAGE_FF	(HID_UP_GENDESK | 0xbb)

struct dev_type {
	u16 idVendor;
	u16 idProduct;
	const signed short *ff;
};

static const signed short ff_rumble[] = {
	FF_RUMBLE,
	-1
};

static const signed short ff_joystick[] = {
	FF_CONSTANT,
	-1
};

static const struct dev_type devices[] = {
	{ 0x44f, 0xb300, ff_rumble },
	{ 0x44f, 0xb304, ff_rumble },
	{ 0x44f, 0xb651, ff_rumble },	/* FGT Rumble Force Wheel */
	{ 0x44f, 0xb654, ff_joystick },	/* FGT Force Feedback Wheel */
};

struct tmff_device {
	struct hid_report *report;
	struct hid_field *ff_field;
};

/* Changes values from 0 to 0xffff into values from minimum to maximum */
static inline int hid_tmff_scale_u16(unsigned int in,
				int minimum, int maximum)
{
	int ret;

	ret = (in * (maximum - minimum) / 0xffff) + minimum;
	if (ret < minimum)
		return minimum;
	if (ret > maximum)
		return maximum;
	return ret;
}

/* Changes values from -0x80 to 0x7f into values from minimum to maximum */
static inline int hid_tmff_scale_s8(int in,
				    int minimum, int maximum)
{
	int ret;

	ret = (((in + 0x80) * (maximum - minimum)) / 0xff) + minimum;
	if (ret < minimum)
		return minimum;
	if (ret > maximum)
		return maximum;
	return ret;
}

static int hid_tmff_play(struct input_dev *dev, void *data, struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct tmff_device *tmff = data;
	struct hid_field *ff_field = tmff->ff_field;
	int x, y;
	int left, right;	/* Rumbling */

	switch (effect->type) {
	case FF_CONSTANT:
		x = hid_tmff_scale_s8(effect->u.ramp.start_level,
					ff_field->logical_minimum,
					ff_field->logical_maximum);
		y = hid_tmff_scale_s8(effect->u.ramp.end_level,
					ff_field->logical_minimum,
					ff_field->logical_maximum);

		dbg_hid("(x, y)=(%04x, %04x)\n", x, y);
		ff_field->value[0] = x;
		ff_field->value[1] = y;
		usbhid_submit_report(hid, tmff->report, USB_DIR_OUT);
		break;

	case FF_RUMBLE:
		left = hid_tmff_scale_u16(effect->u.rumble.weak_magnitude,
					ff_field->logical_minimum,
					ff_field->logical_maximum);
		right = hid_tmff_scale_u16(effect->u.rumble.strong_magnitude,
					ff_field->logical_minimum,
					ff_field->logical_maximum);

		dbg_hid("(left,right)=(%08x, %08x)\n", left, right);
		ff_field->value[0] = left;
		ff_field->value[1] = right;
		usbhid_submit_report(hid, tmff->report, USB_DIR_OUT);
		break;
	}
	return 0;
}

int hid_tmff_init(struct hid_device *hid)
{
	struct tmff_device *tmff;
	struct hid_report *report;
	struct list_head *report_list;
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;
	const signed short *ff_bits = ff_joystick;
	int error;
	int i;

	tmff = kzalloc(sizeof(struct tmff_device), GFP_KERNEL);
	if (!tmff)
		return -ENOMEM;

	/* Find the report to use */
	report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	list_for_each_entry(report, report_list, list) {
		int fieldnum;

		for (fieldnum = 0; fieldnum < report->maxfield; ++fieldnum) {
			struct hid_field *field = report->field[fieldnum];

			if (field->maxusage <= 0)
				continue;

			switch (field->usage[0].hid) {
			case THRUSTMASTER_USAGE_FF:
				if (field->report_count < 2) {
					warn("ignoring FF field with report_count < 2");
					continue;
				}

				if (field->logical_maximum == field->logical_minimum) {
					warn("ignoring FF field with logical_maximum == logical_minimum");
					continue;
				}

				if (tmff->report && tmff->report != report) {
					warn("ignoring FF field in other report");
					continue;
				}

				if (tmff->ff_field && tmff->ff_field != field) {
					warn("ignoring duplicate FF field");
					continue;
				}

				tmff->report = report;
				tmff->ff_field = field;

				for (i = 0; i < ARRAY_SIZE(devices); i++) {
					if (input_dev->id.vendor == devices[i].idVendor &&
					    input_dev->id.product == devices[i].idProduct) {
						ff_bits = devices[i].ff;
						break;
					}
				}

				for (i = 0; ff_bits[i] >= 0; i++)
					set_bit(ff_bits[i], input_dev->ffbit);

				break;

			default:
				warn("ignoring unknown output usage %08x", field->usage[0].hid);
				continue;
			}
		}
	}

	if (!tmff->report) {
		err("cant find FF field in output reports\n");
		error = -ENODEV;
		goto fail;
	}

	error = input_ff_create_memless(input_dev, tmff, hid_tmff_play);
	if (error)
		goto fail;

	info("Force feedback for ThrustMaster devices by Zinx Verituse <zinx@epicsol.org>");
	return 0;

 fail:
	kfree(tmff);
	return error;
}


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
#define THRUSTMASTER_USAGE_RUMBLE_LR	(HID_UP_GENDESK | 0xbb)


struct tmff_device {
	struct hid_report *report;
	struct hid_field *rumble;
};

/* Changes values from 0 to 0xffff into values from minimum to maximum */
static inline int hid_tmff_scale(unsigned int in, int minimum, int maximum)
{
	int ret;

	ret = (in * (maximum - minimum) / 0xffff) + minimum;
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
	int left, right;	/* Rumbling */

	left = hid_tmff_scale(effect->u.rumble.weak_magnitude,
		tmff->rumble->logical_minimum, tmff->rumble->logical_maximum);
	right = hid_tmff_scale(effect->u.rumble.strong_magnitude,
		tmff->rumble->logical_minimum, tmff->rumble->logical_maximum);

	tmff->rumble->value[0] = left;
	tmff->rumble->value[1] = right;
	dbg("(left,right)=(%08x, %08x)", left, right);
	usbhid_submit_report(hid, tmff->report, USB_DIR_OUT);

	return 0;
}

int hid_tmff_init(struct hid_device *hid)
{
	struct tmff_device *tmff;
	struct list_head *pos;
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;
	int error;

	tmff = kzalloc(sizeof(struct tmff_device), GFP_KERNEL);
	if (!tmff)
		return -ENOMEM;

	/* Find the report to use */
	__list_for_each(pos, &hid->report_enum[HID_OUTPUT_REPORT].report_list) {
		struct hid_report *report = (struct hid_report *)pos;
		int fieldnum;

		for (fieldnum = 0; fieldnum < report->maxfield; ++fieldnum) {
			struct hid_field *field = report->field[fieldnum];

			if (field->maxusage <= 0)
				continue;

			switch (field->usage[0].hid) {
				case THRUSTMASTER_USAGE_RUMBLE_LR:
					if (field->report_count < 2) {
						warn("ignoring THRUSTMASTER_USAGE_RUMBLE_LR with report_count < 2");
						continue;
					}

					if (field->logical_maximum == field->logical_minimum) {
						warn("ignoring THRUSTMASTER_USAGE_RUMBLE_LR with logical_maximum == logical_minimum");
						continue;
					}

					if (tmff->report && tmff->report != report) {
						warn("ignoring THRUSTMASTER_USAGE_RUMBLE_LR in other report");
						continue;
					}

					if (tmff->rumble && tmff->rumble != field) {
						warn("ignoring duplicate THRUSTMASTER_USAGE_RUMBLE_LR");
						continue;
					}

					tmff->report = report;
					tmff->rumble = field;

					set_bit(FF_RUMBLE, input_dev->ffbit);
					break;

				default:
					warn("ignoring unknown output usage %08x", field->usage[0].hid);
					continue;
			}
		}
	}

	error = input_ff_create_memless(input_dev, tmff, hid_tmff_play);
	if (error) {
		kfree(tmff);
		return error;
	}

	info("Force feedback for ThrustMaster rumble pad devices by Zinx Verituse <zinx@epicsol.org>");

	return 0;
}


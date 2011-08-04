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
#include "hid-ids.h"

#define DFGT_REV_MAJ 0x13
#define DFGT_REV_MIN 0x22
#define DFP_REV_MAJ 0x11
#define DFP_REV_MIN 0x06
#define FFEX_REV_MAJ 0x21
#define FFEX_REV_MIN 0x00
#define G25_REV_MAJ 0x12
#define G25_REV_MIN 0x22
#define G27_REV_MAJ 0x12
#define G27_REV_MIN 0x38

static const signed short lg4ff_wheel_effects[] = {
	FF_CONSTANT,
	FF_AUTOCENTER,
	-1
};

struct lg4ff_wheel {
	const __u32 product_id;
	const signed short *ff_effects;
	const __u16 min_range;
	const __u16 max_range;
};

static const struct lg4ff_wheel lg4ff_devices[] = {
	{USB_DEVICE_ID_LOGITECH_WHEEL,       lg4ff_wheel_effects, 40, 270},
	{USB_DEVICE_ID_LOGITECH_MOMO_WHEEL,  lg4ff_wheel_effects, 40, 270},
	{USB_DEVICE_ID_LOGITECH_DFP_WHEEL,   lg4ff_wheel_effects, 40, 900},
	{USB_DEVICE_ID_LOGITECH_G25_WHEEL,   lg4ff_wheel_effects, 40, 900},
	{USB_DEVICE_ID_LOGITECH_DFGT_WHEEL,  lg4ff_wheel_effects, 40, 900},
	{USB_DEVICE_ID_LOGITECH_G27_WHEEL,   lg4ff_wheel_effects, 40, 900},
	{USB_DEVICE_ID_LOGITECH_MOMO_WHEEL2, lg4ff_wheel_effects, 40, 270},
	{USB_DEVICE_ID_LOGITECH_WII_WHEEL,   lg4ff_wheel_effects, 40, 270}
};

struct lg4ff_native_cmd {
	const __u8 cmd_num;	/* Number of commands to send */
	const __u8 cmd[];
};

struct lg4ff_usb_revision {
	const __u16 rev_maj;
	const __u16 rev_min;
	const struct lg4ff_native_cmd *command;
};

static const struct lg4ff_native_cmd native_dfp = {
	1,
	{0xf8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static const struct lg4ff_native_cmd native_dfgt = {
	2,
	{0xf8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 1st command */
	 0xf8, 0x09, 0x03, 0x01, 0x00, 0x00, 0x00}	/* 2nd command */
};

static const struct lg4ff_native_cmd native_g25 = {
	1,
	{0xf8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static const struct lg4ff_native_cmd native_g27 = {
	2,
	{0xf8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 1st command */
	 0xf8, 0x09, 0x04, 0x01, 0x00, 0x00, 0x00}	/* 2nd command */
};

static const struct lg4ff_usb_revision lg4ff_revs[] = {
	{DFGT_REV_MAJ, DFGT_REV_MIN, &native_dfgt},	/* Driving Force GT */
	{DFP_REV_MAJ,  DFP_REV_MIN,  &native_dfp},	/* Driving Force Pro */
	{G25_REV_MAJ,  G25_REV_MIN,  &native_g25},	/* G25 */
	{G27_REV_MAJ,  G27_REV_MIN,  &native_g27},	/* G27 */
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
		report->field[0]->value[1] = 0x08;
		report->field[0]->value[2] = x;
		report->field[0]->value[3] = 0x80;
		report->field[0]->value[4] = 0x00;
		report->field[0]->value[5] = 0x00;
		report->field[0]->value[6] = 0x00;

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

	report->field[0]->value[0] = 0xfe;
	report->field[0]->value[1] = 0x0d;
	report->field[0]->value[2] = magnitude >> 13;
	report->field[0]->value[3] = magnitude >> 13;
	report->field[0]->value[4] = magnitude >> 8;
	report->field[0]->value[5] = 0x00;
	report->field[0]->value[6] = 0x00;

	usbhid_submit_report(hid, report, USB_DIR_OUT);
}

static void hid_lg4ff_switch_native(struct hid_device *hid, const struct lg4ff_native_cmd *cmd)
{
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__u8 i, j;

	j = 0;
	while (j < 7*cmd->cmd_num) {
		for (i = 0; i < 7; i++)
			report->field[0]->value[i] = cmd->cmd[j++];

		usbhid_submit_report(hid, report, USB_DIR_OUT);
	}
}

int lg4ff_init(struct hid_device *hid)
{
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev = hidinput->input;
	struct hid_report *report;
	struct hid_field *field;
	struct usb_device_descriptor *udesc = 0;
	int error, i, j;
	__u16 bcdDevice, rev_maj, rev_min;

	/* Find the report to use */
	if (list_empty(report_list)) {
		hid_err(hid, "No output report found\n");
		return -1;
	}

	/* Check that the report looks ok */
	report = list_entry(report_list->next, struct hid_report, list);
	if (!report) {
		hid_err(hid, "NULL output report\n");
		return -1;
	}

	field = report->field[0];
	if (!field) {
		hid_err(hid, "NULL field\n");
		return -1;
	}
	
	/* Check what wheel has been connected */
	for (i = 0; i < ARRAY_SIZE(lg4ff_devices); i++) {
		if (hid->product == lg4ff_devices[i].product_id) {
			dbg_hid("Found compatible device, product ID %04X\n", lg4ff_devices[i].product_id);
			break;
		}
	}

	if (i == ARRAY_SIZE(lg4ff_devices)) {
		hid_err(hid, "Device is not supported by lg4ff driver. If you think it should be, consider reporting a bug to"
			     "LKML, Simon Wood <simon@mungewell.org> or Michal Maly <madcatxster@gmail.com>\n");
		return -1;
	}

	/* Attempt to switch wheel to native mode when applicable */
	udesc = &(hid_to_usb_dev(hid)->descriptor);
	if (!udesc) {
		hid_err(hid, "NULL USB device descriptor\n");
		return -1;
	}
	bcdDevice = le16_to_cpu(udesc->bcdDevice);
	rev_maj = bcdDevice >> 8;
	rev_min = bcdDevice & 0xff;

	if (lg4ff_devices[i].product_id == USB_DEVICE_ID_LOGITECH_WHEEL) {
		dbg_hid("Generic wheel detected, can it do native?\n");
		dbg_hid("USB revision: %2x.%02x\n", rev_maj, rev_min);

		for (j = 0; j < ARRAY_SIZE(lg4ff_revs); j++) {
			if (lg4ff_revs[j].rev_maj == rev_maj && lg4ff_revs[j].rev_min == rev_min) {
				hid_lg4ff_switch_native(hid, lg4ff_revs[j].command);
				hid_info(hid, "Switched to native mode\n");
			}
		}
	}

	/* Set supported force feedback capabilities */
	for (j = 0; lg4ff_devices[i].ff_effects[j] >= 0; j++)
		set_bit(lg4ff_devices[i].ff_effects[j], dev->ffbit);

	error = input_ff_create_memless(dev, NULL, hid_lg4ff_play);

	if (error)
		return error;

	if (test_bit(FF_AUTOCENTER, dev->ffbit))
		dev->ff->set_autocenter = hid_lg4ff_set_autocenter;

	hid_info(hid, "Force feedback for Logitech Speed Force Wireless by Simon Wood <simon@mungewell.org>\n");
	return 0;
}


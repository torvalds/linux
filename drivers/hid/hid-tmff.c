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

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "hid-ids.h"

#define THRUSTMASTER_DEVICE_ID_2_IN_1_DT	0xb320

static const signed short ff_rumble[] = {
	FF_RUMBLE,
	-1
};

static const signed short ff_joystick[] = {
	FF_CONSTANT,
	-1
};

#ifdef CONFIG_THRUSTMASTER_FF

/* Usages for thrustmaster devices I know about */
#define THRUSTMASTER_USAGE_FF	(HID_UP_GENDESK | 0xbb)

struct tmff_device {
	struct hid_report *report;
	struct hid_field *ff_field;
};

/* Changes values from 0 to 0xffff into values from minimum to maximum */
static inline int tmff_scale_u16(unsigned int in, int minimum, int maximum)
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
static inline int tmff_scale_s8(int in, int minimum, int maximum)
{
	int ret;

	ret = (((in + 0x80) * (maximum - minimum)) / 0xff) + minimum;
	if (ret < minimum)
		return minimum;
	if (ret > maximum)
		return maximum;
	return ret;
}

static int tmff_play(struct input_dev *dev, void *data,
		struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct tmff_device *tmff = data;
	struct hid_field *ff_field = tmff->ff_field;
	int x, y;
	int left, right;	/* Rumbling */
	int motor_swap;

	switch (effect->type) {
	case FF_CONSTANT:
		x = tmff_scale_s8(effect->u.ramp.start_level,
					ff_field->logical_minimum,
					ff_field->logical_maximum);
		y = tmff_scale_s8(effect->u.ramp.end_level,
					ff_field->logical_minimum,
					ff_field->logical_maximum);

		dbg_hid("(x, y)=(%04x, %04x)\n", x, y);
		ff_field->value[0] = x;
		ff_field->value[1] = y;
		hid_hw_request(hid, tmff->report, HID_REQ_SET_REPORT);
		break;

	case FF_RUMBLE:
		left = tmff_scale_u16(effect->u.rumble.weak_magnitude,
					ff_field->logical_minimum,
					ff_field->logical_maximum);
		right = tmff_scale_u16(effect->u.rumble.strong_magnitude,
					ff_field->logical_minimum,
					ff_field->logical_maximum);

		/* 2-in-1 strong motor is left */
		if (hid->product == THRUSTMASTER_DEVICE_ID_2_IN_1_DT) {
			motor_swap = left;
			left = right;
			right = motor_swap;
		}

		dbg_hid("(left,right)=(%08x, %08x)\n", left, right);
		ff_field->value[0] = left;
		ff_field->value[1] = right;
		hid_hw_request(hid, tmff->report, HID_REQ_SET_REPORT);
		break;
	}
	return 0;
}

static int tmff_init(struct hid_device *hid, const signed short *ff_bits)
{
	struct tmff_device *tmff;
	struct hid_report *report;
	struct list_head *report_list;
	struct hid_input *hidinput;
	struct input_dev *input_dev;
	int error;
	int i;

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		return -ENODEV;
	}
	hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	input_dev = hidinput->input;

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
					hid_warn(hid, "ignoring FF field with report_count < 2\n");
					continue;
				}

				if (field->logical_maximum ==
						field->logical_minimum) {
					hid_warn(hid, "ignoring FF field with logical_maximum == logical_minimum\n");
					continue;
				}

				if (tmff->report && tmff->report != report) {
					hid_warn(hid, "ignoring FF field in other report\n");
					continue;
				}

				if (tmff->ff_field && tmff->ff_field != field) {
					hid_warn(hid, "ignoring duplicate FF field\n");
					continue;
				}

				tmff->report = report;
				tmff->ff_field = field;

				for (i = 0; ff_bits[i] >= 0; i++)
					set_bit(ff_bits[i], input_dev->ffbit);

				break;

			default:
				hid_warn(hid, "ignoring unknown output usage %08x\n",
					 field->usage[0].hid);
				continue;
			}
		}
	}

	if (!tmff->report) {
		hid_err(hid, "can't find FF field in output reports\n");
		error = -ENODEV;
		goto fail;
	}

	error = input_ff_create_memless(input_dev, tmff, tmff_play);
	if (error)
		goto fail;

	hid_info(hid, "force feedback for ThrustMaster devices by Zinx Verituse <zinx@epicsol.org>\n");
	return 0;

fail:
	kfree(tmff);
	return error;
}
#else
static inline int tmff_init(struct hid_device *hid, const signed short *ff_bits)
{
	return 0;
}
#endif

static int tm_probe(struct hid_device *hdev, const struct hid_device_id *id)
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

	tmff_init(hdev, (void *)id->driver_data);

	return 0;
err:
	return ret;
}

static const struct hid_device_id tm_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb300),
		.driver_data = (unsigned long)ff_rumble },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb304),   /* FireStorm Dual Power 2 (and 3) */
		.driver_data = (unsigned long)ff_rumble },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, THRUSTMASTER_DEVICE_ID_2_IN_1_DT),   /* Dual Trigger 2-in-1 */
		.driver_data = (unsigned long)ff_rumble },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb323),   /* Dual Trigger 3-in-1 (PC Mode) */
		.driver_data = (unsigned long)ff_rumble },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb324),   /* Dual Trigger 3-in-1 (PS3 Mode) */
		.driver_data = (unsigned long)ff_rumble },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb605),   /* NASCAR PRO FF2 Wheel */
		.driver_data = (unsigned long)ff_joystick },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb651),	/* FGT Rumble Force Wheel */
		.driver_data = (unsigned long)ff_rumble },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb653),	/* RGT Force Feedback CLUTCH Raging Wheel */
		.driver_data = (unsigned long)ff_joystick },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb654),	/* FGT Force Feedback Wheel */
		.driver_data = (unsigned long)ff_joystick },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb65a),	/* F430 Force Feedback Wheel */
		.driver_data = (unsigned long)ff_joystick },
	{ }
};
MODULE_DEVICE_TABLE(hid, tm_devices);

static struct hid_driver tm_driver = {
	.name = "thrustmaster",
	.id_table = tm_devices,
	.probe = tm_probe,
};
module_hid_driver(tm_driver);

MODULE_LICENSE("GPL");

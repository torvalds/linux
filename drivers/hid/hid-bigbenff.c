// SPDX-License-Identifier: GPL-2.0+

/*
 *  LED & force feedback support for BigBen Interactive
 *
 *  0x146b:0x0902 "Bigben Interactive Bigben Game Pad"
 *  "Kid-friendly Wired Controller" PS3OFMINIPAD SONY
 *  sold for use with the PS3
 *
 *  Copyright (c) 2018 Hanno Zulla <kontakt@hanno.de>
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/hid.h>

#include "hid-ids.h"


/*
 * The original descriptor for 0x146b:0x0902
 *
 *   0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
 *   0x09, 0x05,        // Usage (Game Pad)
 *   0xA1, 0x01,        // Collection (Application)
 *   0x15, 0x00,        //   Logical Minimum (0)
 *   0x25, 0x01,        //   Logical Maximum (1)
 *   0x35, 0x00,        //   Physical Minimum (0)
 *   0x45, 0x01,        //   Physical Maximum (1)
 *   0x75, 0x01,        //   Report Size (1)
 *   0x95, 0x0D,        //   Report Count (13)
 *   0x05, 0x09,        //   Usage Page (Button)
 *   0x19, 0x01,        //   Usage Minimum (0x01)
 *   0x29, 0x0D,        //   Usage Maximum (0x0D)
 *   0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
 *   0x95, 0x03,        //   Report Count (3)
 *   0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
 *   0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
 *   0x25, 0x07,        //   Logical Maximum (7)
 *   0x46, 0x3B, 0x01,  //   Physical Maximum (315)
 *   0x75, 0x04,        //   Report Size (4)
 *   0x95, 0x01,        //   Report Count (1)
 *   0x65, 0x14,        //   Unit (System: English Rotation, Length: Centimeter)
 *   0x09, 0x39,        //   Usage (Hat switch)
 *   0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
 *   0x65, 0x00,        //   Unit (None)
 *   0x95, 0x01,        //   Report Count (1)
 *   0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
 *   0x26, 0xFF, 0x00,  //   Logical Maximum (255)
 *   0x46, 0xFF, 0x00,  //   Physical Maximum (255)
 *   0x09, 0x30,        //   Usage (X)
 *   0x09, 0x31,        //   Usage (Y)
 *   0x09, 0x32,        //   Usage (Z)
 *   0x09, 0x35,        //   Usage (Rz)
 *   0x75, 0x08,        //   Report Size (8)
 *   0x95, 0x04,        //   Report Count (4)
 *   0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
 *   0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
 *   0x09, 0x20,        //   Usage (0x20)
 *   0x09, 0x21,        //   Usage (0x21)
 *   0x09, 0x22,        //   Usage (0x22)
 *   0x09, 0x23,        //   Usage (0x23)
 *   0x09, 0x24,        //   Usage (0x24)
 *   0x09, 0x25,        //   Usage (0x25)
 *   0x09, 0x26,        //   Usage (0x26)
 *   0x09, 0x27,        //   Usage (0x27)
 *   0x09, 0x28,        //   Usage (0x28)
 *   0x09, 0x29,        //   Usage (0x29)
 *   0x09, 0x2A,        //   Usage (0x2A)
 *   0x09, 0x2B,        //   Usage (0x2B)
 *   0x95, 0x0C,        //   Report Count (12)
 *   0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
 *   0x0A, 0x21, 0x26,  //   Usage (0x2621)
 *   0x95, 0x08,        //   Report Count (8)
 *   0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
 *   0x0A, 0x21, 0x26,  //   Usage (0x2621)
 *   0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
 *   0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
 *   0x46, 0xFF, 0x03,  //   Physical Maximum (1023)
 *   0x09, 0x2C,        //   Usage (0x2C)
 *   0x09, 0x2D,        //   Usage (0x2D)
 *   0x09, 0x2E,        //   Usage (0x2E)
 *   0x09, 0x2F,        //   Usage (0x2F)
 *   0x75, 0x10,        //   Report Size (16)
 *   0x95, 0x04,        //   Report Count (4)
 *   0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
 *   0xC0,              // End Collection
 */

#define PID0902_RDESC_ORIG_SIZE 137

/*
 * The fixed descriptor for 0x146b:0x0902
 *
 * - map buttons according to gamepad.rst
 * - assign right stick from Z/Rz to Rx/Ry
 * - map previously unused analog trigger data to Z/RZ
 * - simplify feature and output descriptor
 */
static __u8 pid0902_rdesc_fixed[] = {
	0x05, 0x01,        /* Usage Page (Generic Desktop Ctrls) */
	0x09, 0x05,        /* Usage (Game Pad) */
	0xA1, 0x01,        /* Collection (Application) */
	0x15, 0x00,        /*   Logical Minimum (0) */
	0x25, 0x01,        /*   Logical Maximum (1) */
	0x35, 0x00,        /*   Physical Minimum (0) */
	0x45, 0x01,        /*   Physical Maximum (1) */
	0x75, 0x01,        /*   Report Size (1) */
	0x95, 0x0D,        /*   Report Count (13) */
	0x05, 0x09,        /*   Usage Page (Button) */
	0x09, 0x05,        /*   Usage (BTN_WEST) */
	0x09, 0x01,        /*   Usage (BTN_SOUTH) */
	0x09, 0x02,        /*   Usage (BTN_EAST) */
	0x09, 0x04,        /*   Usage (BTN_NORTH) */
	0x09, 0x07,        /*   Usage (BTN_TL) */
	0x09, 0x08,        /*   Usage (BTN_TR) */
	0x09, 0x09,        /*   Usage (BTN_TL2) */
	0x09, 0x0A,        /*   Usage (BTN_TR2) */
	0x09, 0x0B,        /*   Usage (BTN_SELECT) */
	0x09, 0x0C,        /*   Usage (BTN_START) */
	0x09, 0x0E,        /*   Usage (BTN_THUMBL) */
	0x09, 0x0F,        /*   Usage (BTN_THUMBR) */
	0x09, 0x0D,        /*   Usage (BTN_MODE) */
	0x81, 0x02,        /*   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x75, 0x01,        /*   Report Size (1) */
	0x95, 0x03,        /*   Report Count (3) */
	0x81, 0x01,        /*   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x05, 0x01,        /*   Usage Page (Generic Desktop Ctrls) */
	0x25, 0x07,        /*   Logical Maximum (7) */
	0x46, 0x3B, 0x01,  /*   Physical Maximum (315) */
	0x75, 0x04,        /*   Report Size (4) */
	0x95, 0x01,        /*   Report Count (1) */
	0x65, 0x14,        /*   Unit (System: English Rotation, Length: Centimeter) */
	0x09, 0x39,        /*   Usage (Hat switch) */
	0x81, 0x42,        /*   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State) */
	0x65, 0x00,        /*   Unit (None) */
	0x95, 0x01,        /*   Report Count (1) */
	0x81, 0x01,        /*   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
	0x46, 0xFF, 0x00,  /*   Physical Maximum (255) */
	0x09, 0x30,        /*   Usage (X) */
	0x09, 0x31,        /*   Usage (Y) */
	0x09, 0x33,        /*   Usage (Rx) */
	0x09, 0x34,        /*   Usage (Ry) */
	0x75, 0x08,        /*   Report Size (8) */
	0x95, 0x04,        /*   Report Count (4) */
	0x81, 0x02,        /*   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x95, 0x0A,        /*   Report Count (10) */
	0x81, 0x01,        /*   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x05, 0x01,        /*   Usage Page (Generic Desktop Ctrls) */
	0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
	0x46, 0xFF, 0x00,  /*   Physical Maximum (255) */
	0x09, 0x32,        /*   Usage (Z) */
	0x09, 0x35,        /*   Usage (Rz) */
	0x95, 0x02,        /*   Report Count (2) */
	0x81, 0x02,        /*   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x95, 0x08,        /*   Report Count (8) */
	0x81, 0x01,        /*   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x06, 0x00, 0xFF,  /*   Usage Page (Vendor Defined 0xFF00) */
	0xB1, 0x02,        /*   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile) */
	0x0A, 0x21, 0x26,  /*   Usage (0x2621) */
	0x95, 0x08,        /*   Report Count (8) */
	0x91, 0x02,        /*   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile) */
	0x0A, 0x21, 0x26,  /*   Usage (0x2621) */
	0x95, 0x08,        /*   Report Count (8) */
	0x81, 0x02,        /*   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0xC0,              /* End Collection */
};

#define NUM_LEDS 4

struct bigben_device {
	struct hid_device *hid;
	struct hid_report *report;
	bool removed;
	u8 led_state;         /* LED1 = 1 .. LED4 = 8 */
	u8 right_motor_on;    /* right motor off/on 0/1 */
	u8 left_motor_force;  /* left motor force 0-255 */
	struct led_classdev *leds[NUM_LEDS];
	bool work_led;
	bool work_ff;
	struct work_struct worker;
};


static void bigben_worker(struct work_struct *work)
{
	struct bigben_device *bigben = container_of(work,
		struct bigben_device, worker);
	struct hid_field *report_field = bigben->report->field[0];

	if (bigben->removed || !report_field)
		return;

	if (bigben->work_led) {
		bigben->work_led = false;
		report_field->value[0] = 0x01; /* 1 = led message */
		report_field->value[1] = 0x08; /* reserved value, always 8 */
		report_field->value[2] = bigben->led_state;
		report_field->value[3] = 0x00; /* padding */
		report_field->value[4] = 0x00; /* padding */
		report_field->value[5] = 0x00; /* padding */
		report_field->value[6] = 0x00; /* padding */
		report_field->value[7] = 0x00; /* padding */
		hid_hw_request(bigben->hid, bigben->report, HID_REQ_SET_REPORT);
	}

	if (bigben->work_ff) {
		bigben->work_ff = false;
		report_field->value[0] = 0x02; /* 2 = rumble effect message */
		report_field->value[1] = 0x08; /* reserved value, always 8 */
		report_field->value[2] = bigben->right_motor_on;
		report_field->value[3] = bigben->left_motor_force;
		report_field->value[4] = 0xff; /* duration 0-254 (255 = nonstop) */
		report_field->value[5] = 0x00; /* padding */
		report_field->value[6] = 0x00; /* padding */
		report_field->value[7] = 0x00; /* padding */
		hid_hw_request(bigben->hid, bigben->report, HID_REQ_SET_REPORT);
	}
}

static int hid_bigben_play_effect(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct bigben_device *bigben = hid_get_drvdata(hid);
	u8 right_motor_on;
	u8 left_motor_force;

	if (!bigben) {
		hid_err(hid, "no device data\n");
		return 0;
	}

	if (effect->type != FF_RUMBLE)
		return 0;

	right_motor_on   = effect->u.rumble.weak_magnitude ? 1 : 0;
	left_motor_force = effect->u.rumble.strong_magnitude / 256;

	if (right_motor_on != bigben->right_motor_on ||
			left_motor_force != bigben->left_motor_force) {
		bigben->right_motor_on   = right_motor_on;
		bigben->left_motor_force = left_motor_force;
		bigben->work_ff = true;
		schedule_work(&bigben->worker);
	}

	return 0;
}

static void bigben_set_led(struct led_classdev *led,
	enum led_brightness value)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct bigben_device *bigben = hid_get_drvdata(hid);
	int n;
	bool work;

	if (!bigben) {
		hid_err(hid, "no device data\n");
		return;
	}

	for (n = 0; n < NUM_LEDS; n++) {
		if (led == bigben->leds[n]) {
			if (value == LED_OFF) {
				work = (bigben->led_state & BIT(n));
				bigben->led_state &= ~BIT(n);
			} else {
				work = !(bigben->led_state & BIT(n));
				bigben->led_state |= BIT(n);
			}

			if (work) {
				bigben->work_led = true;
				schedule_work(&bigben->worker);
			}
			return;
		}
	}
}

static enum led_brightness bigben_get_led(struct led_classdev *led)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct bigben_device *bigben = hid_get_drvdata(hid);
	int n;

	if (!bigben) {
		hid_err(hid, "no device data\n");
		return LED_OFF;
	}

	for (n = 0; n < NUM_LEDS; n++) {
		if (led == bigben->leds[n])
			return (bigben->led_state & BIT(n)) ? LED_ON : LED_OFF;
	}

	return LED_OFF;
}

static void bigben_remove(struct hid_device *hid)
{
	struct bigben_device *bigben = hid_get_drvdata(hid);

	bigben->removed = true;
	cancel_work_sync(&bigben->worker);
	hid_hw_stop(hid);
}

static int bigben_probe(struct hid_device *hid,
	const struct hid_device_id *id)
{
	struct bigben_device *bigben;
	struct hid_input *hidinput;
	struct list_head *report_list;
	struct led_classdev *led;
	char *name;
	size_t name_sz;
	int n, error;

	bigben = devm_kzalloc(&hid->dev, sizeof(*bigben), GFP_KERNEL);
	if (!bigben)
		return -ENOMEM;
	hid_set_drvdata(hid, bigben);
	bigben->hid = hid;
	bigben->removed = false;

	error = hid_parse(hid);
	if (error) {
		hid_err(hid, "parse failed\n");
		return error;
	}

	error = hid_hw_start(hid, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (error) {
		hid_err(hid, "hw start failed\n");
		return error;
	}

	report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	if (list_empty(report_list)) {
		hid_err(hid, "no output report found\n");
		error = -ENODEV;
		goto error_hw_stop;
	}
	bigben->report = list_entry(report_list->next,
		struct hid_report, list);

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		error = -ENODEV;
		goto error_hw_stop;
	}

	hidinput = list_first_entry(&hid->inputs, struct hid_input, list);
	set_bit(FF_RUMBLE, hidinput->input->ffbit);

	INIT_WORK(&bigben->worker, bigben_worker);

	error = input_ff_create_memless(hidinput->input, NULL,
		hid_bigben_play_effect);
	if (error)
		goto error_hw_stop;

	name_sz = strlen(dev_name(&hid->dev)) + strlen(":red:bigben#") + 1;

	for (n = 0; n < NUM_LEDS; n++) {
		led = devm_kzalloc(
			&hid->dev,
			sizeof(struct led_classdev) + name_sz,
			GFP_KERNEL
		);
		if (!led) {
			error = -ENOMEM;
			goto error_hw_stop;
		}
		name = (void *)(&led[1]);
		snprintf(name, name_sz,
			"%s:red:bigben%d",
			dev_name(&hid->dev), n + 1
		);
		led->name = name;
		led->brightness = (n == 0) ? LED_ON : LED_OFF;
		led->max_brightness = 1;
		led->brightness_get = bigben_get_led;
		led->brightness_set = bigben_set_led;
		bigben->leds[n] = led;
		error = devm_led_classdev_register(&hid->dev, led);
		if (error)
			goto error_hw_stop;
	}

	/* initial state: LED1 is on, no rumble effect */
	bigben->led_state = BIT(0);
	bigben->right_motor_on = 0;
	bigben->left_motor_force = 0;
	bigben->work_led = true;
	bigben->work_ff = true;
	schedule_work(&bigben->worker);

	hid_info(hid, "LED and force feedback support for BigBen gamepad\n");

	return 0;

error_hw_stop:
	hid_hw_stop(hid);
	return error;
}

static __u8 *bigben_report_fixup(struct hid_device *hid, __u8 *rdesc,
	unsigned int *rsize)
{
	if (*rsize == PID0902_RDESC_ORIG_SIZE) {
		rdesc = pid0902_rdesc_fixed;
		*rsize = sizeof(pid0902_rdesc_fixed);
	} else
		hid_warn(hid, "unexpected rdesc, please submit for review\n");
	return rdesc;
}

static const struct hid_device_id bigben_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_BIGBEN, USB_DEVICE_ID_BIGBEN_PS3OFMINIPAD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, bigben_devices);

static struct hid_driver bigben_driver = {
	.name = "bigben",
	.id_table = bigben_devices,
	.probe = bigben_probe,
	.report_fixup = bigben_report_fixup,
	.remove = bigben_remove,
};
module_hid_driver(bigben_driver);

MODULE_LICENSE("GPL");

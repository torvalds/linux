/*
 * HID Driver for ELAN Touchpad
 *
 * Currently only supports touchpad found on HP Pavilion X2 10
 *
 * Copyright (c) 2016 Alexandrov Stanislav <neko@nya.ai>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/input/mt.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "hid-ids.h"

#define ELAN_MT_I2C		0x5d
#define ELAN_SINGLE_FINGER	0x81
#define ELAN_MT_FIRST_FINGER	0x82
#define ELAN_MT_SECOND_FINGER	0x83
#define ELAN_INPUT_REPORT_SIZE	8
#define ELAN_I2C_REPORT_SIZE	32
#define ELAN_FINGER_DATA_LEN	5
#define ELAN_MAX_FINGERS	5
#define ELAN_MAX_PRESSURE	255
#define ELAN_TP_USB_INTF	1

#define ELAN_FEATURE_REPORT	0x0d
#define ELAN_FEATURE_SIZE	5
#define ELAN_PARAM_MAX_X	6
#define ELAN_PARAM_MAX_Y	7
#define ELAN_PARAM_RES		8

#define ELAN_MUTE_LED_REPORT	0xBC
#define ELAN_LED_REPORT_SIZE	8

#define ELAN_HAS_LED		BIT(0)

struct elan_drvdata {
	struct input_dev *input;
	u8 prev_report[ELAN_INPUT_REPORT_SIZE];
	struct led_classdev mute_led;
	u8 mute_led_state;
	u16 max_x;
	u16 max_y;
	u16 res_x;
	u16 res_y;
};

static int is_not_elan_touchpad(struct hid_device *hdev)
{
	if (hdev->bus == BUS_USB) {
		struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

		return (intf->altsetting->desc.bInterfaceNumber !=
			ELAN_TP_USB_INTF);
	}

	return 0;
}

static int elan_input_mapping(struct hid_device *hdev, struct hid_input *hi,
			      struct hid_field *field, struct hid_usage *usage,
			      unsigned long **bit, int *max)
{
	if (is_not_elan_touchpad(hdev))
		return 0;

	if (field->report->id == ELAN_SINGLE_FINGER ||
	    field->report->id == ELAN_MT_FIRST_FINGER ||
	    field->report->id == ELAN_MT_SECOND_FINGER ||
	    field->report->id == ELAN_MT_I2C)
		return -1;

	return 0;
}

static int elan_get_device_param(struct hid_device *hdev,
				 unsigned char *dmabuf, unsigned char param)
{
	int ret;

	dmabuf[0] = ELAN_FEATURE_REPORT;
	dmabuf[1] = 0x05;
	dmabuf[2] = 0x03;
	dmabuf[3] = param;
	dmabuf[4] = 0x01;

	ret = hid_hw_raw_request(hdev, ELAN_FEATURE_REPORT, dmabuf,
				 ELAN_FEATURE_SIZE, HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	if (ret != ELAN_FEATURE_SIZE) {
		hid_err(hdev, "Set report error for parm %d: %d\n", param, ret);
		return ret;
	}

	ret = hid_hw_raw_request(hdev, ELAN_FEATURE_REPORT, dmabuf,
				 ELAN_FEATURE_SIZE, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret != ELAN_FEATURE_SIZE) {
		hid_err(hdev, "Get report error for parm %d: %d\n", param, ret);
		return ret;
	}

	return 0;
}

static unsigned int elan_convert_res(char val)
{
	/*
	 * (value from firmware) * 10 + 790 = dpi
	 * dpi * 10 / 254 = dots/mm
	 */
	return (val * 10 + 790) * 10 / 254;
}

static int elan_get_device_params(struct hid_device *hdev)
{
	struct elan_drvdata *drvdata = hid_get_drvdata(hdev);
	unsigned char *dmabuf;
	int ret;

	dmabuf = kmalloc(ELAN_FEATURE_SIZE, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	ret = elan_get_device_param(hdev, dmabuf, ELAN_PARAM_MAX_X);
	if (ret)
		goto err;

	drvdata->max_x = (dmabuf[4] << 8) | dmabuf[3];

	ret = elan_get_device_param(hdev, dmabuf, ELAN_PARAM_MAX_Y);
	if (ret)
		goto err;

	drvdata->max_y = (dmabuf[4] << 8) | dmabuf[3];

	ret = elan_get_device_param(hdev, dmabuf, ELAN_PARAM_RES);
	if (ret)
		goto err;

	drvdata->res_x = elan_convert_res(dmabuf[3]);
	drvdata->res_y = elan_convert_res(dmabuf[4]);

err:
	kfree(dmabuf);
	return ret;
}

static int elan_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	int ret;
	struct input_dev *input;
	struct elan_drvdata *drvdata = hid_get_drvdata(hdev);

	if (is_not_elan_touchpad(hdev))
		return 0;

	ret = elan_get_device_params(hdev);
	if (ret)
		return ret;

	input = devm_input_allocate_device(&hdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = "Elan Touchpad";
	input->phys = hdev->phys;
	input->uniq = hdev->uniq;
	input->id.bustype = hdev->bus;
	input->id.vendor  = hdev->vendor;
	input->id.product = hdev->product;
	input->id.version = hdev->version;
	input->dev.parent = &hdev->dev;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, drvdata->max_x,
			     0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, drvdata->max_y,
			     0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, ELAN_MAX_PRESSURE,
			     0, 0);

	__set_bit(BTN_LEFT, input->keybit);
	__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

	ret = input_mt_init_slots(input, ELAN_MAX_FINGERS, INPUT_MT_POINTER);
	if (ret) {
		hid_err(hdev, "Failed to init elan MT slots: %d\n", ret);
		return ret;
	}

	input_abs_set_res(input, ABS_X, drvdata->res_x);
	input_abs_set_res(input, ABS_Y, drvdata->res_y);

	ret = input_register_device(input);
	if (ret) {
		hid_err(hdev, "Failed to register elan input device: %d\n",
			ret);
		input_free_device(input);
		return ret;
	}

	drvdata->input = input;

	return 0;
}

static void elan_report_mt_slot(struct elan_drvdata *drvdata, u8 *data,
				unsigned int slot_num)
{
	struct input_dev *input = drvdata->input;
	int x, y, p;

	bool active = !!data;

	input_mt_slot(input, slot_num);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, active);
	if (active) {
		x = ((data[0] & 0xF0) << 4) | data[1];
		y = drvdata->max_y -
		    (((data[0] & 0x07) << 8) | data[2]);
		p = data[4];

		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
		input_report_abs(input, ABS_MT_PRESSURE, p);
	}
}

static void elan_usb_report_input(struct elan_drvdata *drvdata, u8 *data)
{
	int i;
	struct input_dev *input = drvdata->input;

	/*
	 * There is 3 types of reports: for single touch,
	 * for multitouch - first finger and for multitouch - second finger
	 *
	 * packet structure for ELAN_SINGLE_FINGER and ELAN_MT_FIRST_FINGER:
	 *
	 * byte 1: 1   0   0   0   0   0   0   1  // 0x81 or 0x82
	 * byte 2: 0   0   0   0   0   0   0   0  // looks like unused
	 * byte 3: f5  f4  f3  f2  f1  0   0   L
	 * byte 4: x12 x11 x10 x9  0?  y11 y10 y9
	 * byte 5: x8  x7  x6  x5  x4  x3  x2  x1
	 * byte 6: y8  y7  y6  y5  y4  y3  y2  y1
	 * byte 7: sy4 sy3 sy2 sy1 sx4 sx3 sx2 sx1
	 * byte 8: p8  p7  p6  p5  p4  p3  p2  p1
	 *
	 * packet structure for ELAN_MT_SECOND_FINGER:
	 *
	 * byte 1: 1   0   0   0   0   0   1   1  // 0x83
	 * byte 2: x12 x11 x10 x9  0   y11 y10 y9
	 * byte 3: x8  x7  x6  x5  x4  x3  x2  x1
	 * byte 4: y8  y7  y6  y5  y4  y3  y2  y1
	 * byte 5: sy4 sy3 sy2 sy1 sx4 sx3 sx2 sx1
	 * byte 6: p8  p7  p6  p5  p4  p3  p2  p1
	 * byte 7: 0   0   0   0   0   0   0   0
	 * byte 8: 0   0   0   0   0   0   0   0
	 *
	 * f5-f1: finger touch bits
	 * L: clickpad button
	 * sy / sx: finger width / height expressed in traces, the total number
	 *          of traces can be queried by doing a HID_REQ_SET_REPORT
	 *          { 0x0d, 0x05, 0x03, 0x05, 0x01 } followed by a GET, in the
	 *          returned buf, buf[3]=no-x-traces, buf[4]=no-y-traces.
	 * p: pressure
	 */

	if (data[0] == ELAN_SINGLE_FINGER) {
		for (i = 0; i < ELAN_MAX_FINGERS; i++) {
			if (data[2] & BIT(i + 3))
				elan_report_mt_slot(drvdata, data + 3, i);
			else
				elan_report_mt_slot(drvdata, NULL, i);
		}
		input_report_key(input, BTN_LEFT, data[2] & 0x01);
	}
	/*
	 * When touched with two fingers Elan touchpad will emit two HID reports
	 * first is ELAN_MT_FIRST_FINGER and second is ELAN_MT_SECOND_FINGER
	 * we will save ELAN_MT_FIRST_FINGER report and wait for
	 * ELAN_MT_SECOND_FINGER to finish multitouch
	 */
	if (data[0] == ELAN_MT_FIRST_FINGER) {
		memcpy(drvdata->prev_report, data,
		       sizeof(drvdata->prev_report));
		return;
	}

	if (data[0] == ELAN_MT_SECOND_FINGER) {
		int first = 0;
		u8 *prev_report = drvdata->prev_report;

		if (prev_report[0] != ELAN_MT_FIRST_FINGER)
			return;

		for (i = 0; i < ELAN_MAX_FINGERS; i++) {
			if (prev_report[2] & BIT(i + 3)) {
				if (!first) {
					first = 1;
					elan_report_mt_slot(drvdata, prev_report + 3, i);
				} else {
					elan_report_mt_slot(drvdata, data + 1, i);
				}
			} else {
				elan_report_mt_slot(drvdata, NULL, i);
			}
		}
		input_report_key(input, BTN_LEFT, prev_report[2] & 0x01);
	}

	input_mt_sync_frame(input);
	input_sync(input);
}

static void elan_i2c_report_input(struct elan_drvdata *drvdata, u8 *data)
{
	struct input_dev *input = drvdata->input;
	u8 *finger_data;
	int i;

	/*
	 * Elan MT touchpads in i2c mode send finger data in the same format
	 * as in USB mode, but then with all fingers in a single packet.
	 *
	 * packet structure for ELAN_MT_I2C:
	 *
	 * byte     1: 1   0   0   1   1   1   0   1   // 0x5d
	 * byte     2: f5  f4  f3  f2  f1  0   0   L
	 * byte     3: x12 x11 x10 x9  0?  y11 y10 y9
	 * byte     4: x8  x7  x6  x5  x4  x3  x2  x1
	 * byte     5: y8  y7  y6  y5  y4  y3  y2  y1
	 * byte     6: sy4 sy3 sy2 sy1 sx4 sx3 sx2 sx1
	 * byte     7: p8  p7  p6  p5  p4  p3  p2  p1
	 * byte  8-12: Same as byte 3-7 for second finger down
	 * byte 13-17: Same as byte 3-7 for third finger down
	 * byte 18-22: Same as byte 3-7 for fourth finger down
	 * byte 23-27: Same as byte 3-7 for fifth finger down
	 */

	finger_data = data + 2;
	for (i = 0; i < ELAN_MAX_FINGERS; i++) {
		if (data[1] & BIT(i + 3)) {
			elan_report_mt_slot(drvdata, finger_data, i);
			finger_data += ELAN_FINGER_DATA_LEN;
		} else {
			elan_report_mt_slot(drvdata, NULL, i);
		}
	}

	input_report_key(input, BTN_LEFT, data[1] & 0x01);
	input_mt_sync_frame(input);
	input_sync(input);
}

static int elan_raw_event(struct hid_device *hdev,
			  struct hid_report *report, u8 *data, int size)
{
	struct elan_drvdata *drvdata = hid_get_drvdata(hdev);

	if (is_not_elan_touchpad(hdev))
		return 0;

	if (data[0] == ELAN_SINGLE_FINGER ||
	    data[0] == ELAN_MT_FIRST_FINGER ||
	    data[0] == ELAN_MT_SECOND_FINGER) {
		if (size == ELAN_INPUT_REPORT_SIZE) {
			elan_usb_report_input(drvdata, data);
			return 1;
		}
	}

	if (data[0] == ELAN_MT_I2C && size == ELAN_I2C_REPORT_SIZE) {
		elan_i2c_report_input(drvdata, data);
		return 1;
	}

	return 0;
}

static int elan_start_multitouch(struct hid_device *hdev)
{
	int ret;

	/*
	 * This byte sequence will enable multitouch mode and disable
	 * mouse emulation
	 */
	static const unsigned char buf[] = { 0x0D, 0x00, 0x03, 0x21, 0x00 };
	unsigned char *dmabuf = kmemdup(buf, sizeof(buf), GFP_KERNEL);

	if (!dmabuf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, dmabuf[0], dmabuf, sizeof(buf),
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	kfree(dmabuf);

	if (ret != sizeof(buf)) {
		hid_err(hdev, "Failed to start multitouch: %d\n", ret);
		return ret;
	}

	return 0;
}

static enum led_brightness elan_mute_led_get_brigtness(struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = to_hid_device(dev);
	struct elan_drvdata *drvdata = hid_get_drvdata(hdev);

	return drvdata->mute_led_state;
}

static int elan_mute_led_set_brigtness(struct led_classdev *led_cdev,
				       enum led_brightness value)
{
	int ret;
	u8 led_state;
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = to_hid_device(dev);
	struct elan_drvdata *drvdata = hid_get_drvdata(hdev);

	unsigned char *dmabuf = kzalloc(ELAN_LED_REPORT_SIZE, GFP_KERNEL);

	if (!dmabuf)
		return -ENOMEM;

	led_state = !!value;

	dmabuf[0] = ELAN_MUTE_LED_REPORT;
	dmabuf[1] = 0x02;
	dmabuf[2] = led_state;

	ret = hid_hw_raw_request(hdev, dmabuf[0], dmabuf, ELAN_LED_REPORT_SIZE,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	kfree(dmabuf);

	if (ret != ELAN_LED_REPORT_SIZE) {
		hid_err(hdev, "Failed to set mute led brightness: %d\n", ret);
		return ret;
	}

	drvdata->mute_led_state = led_state;
	return 0;
}

static int elan_init_mute_led(struct hid_device *hdev)
{
	struct elan_drvdata *drvdata = hid_get_drvdata(hdev);
	struct led_classdev *mute_led = &drvdata->mute_led;

	mute_led->name = "elan:red:mute";
	mute_led->brightness_get = elan_mute_led_get_brigtness;
	mute_led->brightness_set_blocking = elan_mute_led_set_brigtness;
	mute_led->max_brightness = LED_ON;
	mute_led->dev = &hdev->dev;

	return devm_led_classdev_register(&hdev->dev, mute_led);
}

static int elan_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct elan_drvdata *drvdata;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);

	if (!drvdata)
		return -ENOMEM;

	hid_set_drvdata(hdev, drvdata);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Hid Parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "Hid hw start failed\n");
		return ret;
	}

	if (is_not_elan_touchpad(hdev))
		return 0;

	if (!drvdata->input) {
		hid_err(hdev, "Input device is not registered\n");
		ret = -ENAVAIL;
		goto err;
	}

	ret = elan_start_multitouch(hdev);
	if (ret)
		goto err;

	if (id->driver_data & ELAN_HAS_LED) {
		ret = elan_init_mute_led(hdev);
		if (ret)
			goto err;
	}

	return 0;
err:
	hid_hw_stop(hdev);
	return ret;
}

static void elan_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
}

static const struct hid_device_id elan_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELAN, USB_DEVICE_ID_HP_X2),
	  .driver_data = ELAN_HAS_LED },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELAN, USB_DEVICE_ID_HP_X2_10_COVER),
	  .driver_data = ELAN_HAS_LED },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, USB_DEVICE_ID_TOSHIBA_CLICK_L9W) },
	{ }
};
MODULE_DEVICE_TABLE(hid, elan_devices);

static struct hid_driver elan_driver = {
	.name = "elan",
	.id_table = elan_devices,
	.input_mapping = elan_input_mapping,
	.input_configured = elan_input_configured,
	.raw_event = elan_raw_event,
	.probe = elan_probe,
	.remove = elan_remove,
};

module_hid_driver(elan_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexandrov Stanislav");
MODULE_DESCRIPTION("Driver for HID ELAN Touchpads");

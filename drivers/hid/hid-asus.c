/*
 *  HID driver for Asus notebook built-in keyboard.
 *  Fixes small logical maximum to match usage maximum.
 *
 *  Currently supported devices are:
 *    EeeBook X205TA
 *    VivoBook E200HA
 *
 *  Copyright (c) 2016 Yusuke Fujimaki <usk.fujimaki@gmail.com>
 *
 *  This module based on hid-ortek by
 *  Copyright (c) 2010 Johnathon Harris <jmharris@gmail.com>
 *  Copyright (c) 2011 Jiri Kosina
 *
 *  This module has been updated to add support for Asus i2c touchpad.
 *
 *  Copyright (c) 2016 Brendan McGrath <redmcg@redmandi.dyndns.org>
 *  Copyright (c) 2016 Victor Vlasenko <victor.vlasenko@sysgears.com>
 *  Copyright (c) 2016 Frederik Wenigwieser <frederik.wenigwieser@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/input/mt.h>

#include "hid-ids.h"

MODULE_AUTHOR("Yusuke Fujimaki <usk.fujimaki@gmail.com>");
MODULE_AUTHOR("Brendan McGrath <redmcg@redmandi.dyndns.org>");
MODULE_AUTHOR("Victor Vlasenko <victor.vlasenko@sysgears.com>");
MODULE_AUTHOR("Frederik Wenigwieser <frederik.wenigwieser@gmail.com>");
MODULE_DESCRIPTION("Asus HID Keyboard and TouchPad");

#define FEATURE_REPORT_ID 0x0d
#define INPUT_REPORT_ID 0x5d

#define INPUT_REPORT_SIZE 28

#define MAX_CONTACTS 5

#define MAX_X 2794
#define MAX_Y 1758
#define MAX_TOUCH_MAJOR 8
#define MAX_PRESSURE 128

#define CONTACT_DATA_SIZE 5

#define BTN_LEFT_MASK 0x01
#define CONTACT_TOOL_TYPE_MASK 0x80
#define CONTACT_X_MSB_MASK 0xf0
#define CONTACT_Y_MSB_MASK 0x0f
#define CONTACT_TOUCH_MAJOR_MASK 0x07
#define CONTACT_PRESSURE_MASK 0x7f

#define QUIRK_FIX_NOTEBOOK_REPORT	BIT(0)
#define QUIRK_NO_INIT_REPORTS		BIT(1)
#define QUIRK_SKIP_INPUT_MAPPING	BIT(2)
#define QUIRK_IS_MULTITOUCH		BIT(3)

#define KEYBOARD_QUIRKS			(QUIRK_FIX_NOTEBOOK_REPORT | \
						 QUIRK_NO_INIT_REPORTS)
#define TOUCHPAD_QUIRKS			(QUIRK_NO_INIT_REPORTS | \
						 QUIRK_SKIP_INPUT_MAPPING | \
						 QUIRK_IS_MULTITOUCH)

#define TRKID_SGN       ((TRKID_MAX + 1) >> 1)

struct asus_drvdata {
	unsigned long quirks;
	struct input_dev *input;
};

static void asus_report_contact_down(struct input_dev *input,
		int toolType, u8 *data)
{
	int touch_major, pressure;
	int x = (data[0] & CONTACT_X_MSB_MASK) << 4 | data[1];
	int y = MAX_Y - ((data[0] & CONTACT_Y_MSB_MASK) << 8 | data[2]);

	if (toolType == MT_TOOL_PALM) {
		touch_major = MAX_TOUCH_MAJOR;
		pressure = MAX_PRESSURE;
	} else {
		touch_major = (data[3] >> 4) & CONTACT_TOUCH_MAJOR_MASK;
		pressure = data[4] & CONTACT_PRESSURE_MASK;
	}

	input_report_abs(input, ABS_MT_POSITION_X, x);
	input_report_abs(input, ABS_MT_POSITION_Y, y);
	input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch_major);
	input_report_abs(input, ABS_MT_PRESSURE, pressure);
}

/* Required for Synaptics Palm Detection */
static void asus_report_tool_width(struct input_dev *input)
{
	struct input_mt *mt = input->mt;
	struct input_mt_slot *oldest;
	int oldid, count, i;

	oldest = NULL;
	oldid = mt->trkid;
	count = 0;

	for (i = 0; i < mt->num_slots; ++i) {
		struct input_mt_slot *ps = &mt->slots[i];
		int id = input_mt_get_value(ps, ABS_MT_TRACKING_ID);

		if (id < 0)
			continue;
		if ((id - oldid) & TRKID_SGN) {
			oldest = ps;
			oldid = id;
		}
		count++;
	}

	if (oldest) {
		input_report_abs(input, ABS_TOOL_WIDTH,
			input_mt_get_value(oldest, ABS_MT_TOUCH_MAJOR));
	}
}

static void asus_report_input(struct input_dev *input, u8 *data)
{
	int i;
	u8 *contactData = data + 2;

	for (i = 0; i < MAX_CONTACTS; i++) {
		bool down = !!(data[1] & BIT(i+3));
		int toolType = contactData[3] & CONTACT_TOOL_TYPE_MASK ?
						MT_TOOL_PALM : MT_TOOL_FINGER;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, toolType, down);

		if (down) {
			asus_report_contact_down(input, toolType, contactData);
			contactData += CONTACT_DATA_SIZE;
		}
	}

	input_report_key(input, BTN_LEFT, data[1] & BTN_LEFT_MASK);
	asus_report_tool_width(input);

	input_mt_sync_frame(input);
	input_sync(input);
}

static int asus_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_IS_MULTITOUCH &&
					 data[0] == INPUT_REPORT_ID &&
						size == INPUT_REPORT_SIZE) {
		asus_report_input(drvdata->input, data);
		return 1;
	}

	return 0;
}

static int asus_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	struct input_dev *input = hi->input;
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_IS_MULTITOUCH) {
		int ret;

		input_set_abs_params(input, ABS_MT_POSITION_X, 0, MAX_X, 0, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y, 0, MAX_Y, 0, 0);
		input_set_abs_params(input, ABS_TOOL_WIDTH, 0, MAX_TOUCH_MAJOR, 0, 0);
		input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, MAX_TOUCH_MAJOR, 0, 0);
		input_set_abs_params(input, ABS_MT_PRESSURE, 0, MAX_PRESSURE, 0, 0);

		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

		ret = input_mt_init_slots(input, MAX_CONTACTS, INPUT_MT_POINTER);

		if (ret) {
			hid_err(hdev, "Asus input mt init slots failed: %d\n", ret);
			return ret;
		}
	}

	drvdata->input = input;

	return 0;
}

static int asus_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit,
		int *max)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_SKIP_INPUT_MAPPING) {
		/* Don't map anything from the HID report.
		 * We do it all manually in asus_input_configured
		 */
		return -1;
	}

	return 0;
}

static int asus_start_multitouch(struct hid_device *hdev)
{
	int ret;
	const unsigned char buf[] = { FEATURE_REPORT_ID, 0x00, 0x03, 0x01, 0x00 };
	unsigned char *dmabuf = kmemdup(buf, sizeof(buf), GFP_KERNEL);

	if (!dmabuf) {
		ret = -ENOMEM;
		hid_err(hdev, "Asus failed to alloc dma buf: %d\n", ret);
		return ret;
	}

	ret = hid_hw_raw_request(hdev, dmabuf[0], dmabuf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	kfree(dmabuf);

	if (ret != sizeof(buf)) {
		hid_err(hdev, "Asus failed to start multitouch: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused asus_reset_resume(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_IS_MULTITOUCH)
		return asus_start_multitouch(hdev);

	return 0;
}

static int asus_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct asus_drvdata *drvdata;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL) {
		hid_err(hdev, "Can't alloc Asus descriptor\n");
		return -ENOMEM;
	}

	hid_set_drvdata(hdev, drvdata);

	drvdata->quirks = id->driver_data;

	if (drvdata->quirks & QUIRK_NO_INIT_REPORTS)
		hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Asus hid parse failed: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "Asus hw start failed: %d\n", ret);
		return ret;
	}

	if (!drvdata->input) {
		hid_err(hdev, "Asus input not registered\n");
		ret = -ENOMEM;
		goto err_stop_hw;
	}

	if (drvdata->quirks & QUIRK_IS_MULTITOUCH) {
		drvdata->input->name = "Asus TouchPad";
	} else {
		drvdata->input->name = "Asus Keyboard";
	}

	if (drvdata->quirks & QUIRK_IS_MULTITOUCH) {
		ret = asus_start_multitouch(hdev);
		if (ret)
			goto err_stop_hw;
	}

	return 0;
err_stop_hw:
	hid_hw_stop(hdev);
	return ret;
}

static __u8 *asus_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_FIX_NOTEBOOK_REPORT &&
			*rsize >= 56 && rdesc[54] == 0x25 && rdesc[55] == 0x65) {
		hid_info(hdev, "Fixing up Asus notebook report descriptor\n");
		rdesc[55] = 0xdd;
	}
	return rdesc;
}

static const struct hid_device_id asus_devices[] = {
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
		 USB_DEVICE_ID_ASUSTEK_NOTEBOOK_KEYBOARD), KEYBOARD_QUIRKS},
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
			 USB_DEVICE_ID_ASUSTEK_TOUCHPAD), TOUCHPAD_QUIRKS },
	{ }
};
MODULE_DEVICE_TABLE(hid, asus_devices);

static struct hid_driver asus_driver = {
	.name			= "asus",
	.id_table		= asus_devices,
	.report_fixup		= asus_report_fixup,
	.probe                  = asus_probe,
	.input_mapping          = asus_input_mapping,
	.input_configured       = asus_input_configured,
#ifdef CONFIG_PM
	.reset_resume           = asus_reset_resume,
#endif
	.raw_event		= asus_raw_event
};
module_hid_driver(asus_driver);

MODULE_LICENSE("GPL");

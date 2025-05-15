// SPDX-License-Identifier: GPL-2.0
/*
 * Apple Touch Bar Backlight Driver
 *
 * Copyright (c) 2017-2018 Ronald Tschalär
 * Copyright (c) 2022-2023 Kerem Karabay <kekrby@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hid.h>
#include <linux/backlight.h>
#include <linux/device.h>

#include "hid-ids.h"

#define APPLETB_BL_ON			1
#define APPLETB_BL_DIM			3
#define APPLETB_BL_OFF			4

#define HID_UP_APPLEVENDOR_TB_BL	0xff120000

#define HID_VD_APPLE_TB_BRIGHTNESS	0xff120001
#define HID_USAGE_AUX1			0xff120020
#define HID_USAGE_BRIGHTNESS		0xff120021

static int appletb_bl_def_brightness = 2;
module_param_named(brightness, appletb_bl_def_brightness, int, 0444);
MODULE_PARM_DESC(brightness, "Default brightness:\n"
			 "    0 - Touchbar is off\n"
			 "    1 - Dim brightness\n"
			 "    [2] - Full brightness");

struct appletb_bl {
	struct hid_field *aux1_field, *brightness_field;
	struct backlight_device *bdev;

	bool full_on;
};

static const u8 appletb_bl_brightness_map[] = {
	APPLETB_BL_OFF,
	APPLETB_BL_DIM,
	APPLETB_BL_ON,
};

static int appletb_bl_set_brightness(struct appletb_bl *bl, u8 brightness)
{
	struct hid_report *report = bl->brightness_field->report;
	struct hid_device *hdev = report->device;
	int ret;

	ret = hid_set_field(bl->aux1_field, 0, 1);
	if (ret) {
		hid_err(hdev, "Failed to set auxiliary field (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	ret = hid_set_field(bl->brightness_field, 0, brightness);
	if (ret) {
		hid_err(hdev, "Failed to set brightness field (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	if (!bl->full_on) {
		ret = hid_hw_power(hdev, PM_HINT_FULLON);
		if (ret < 0) {
			hid_err(hdev, "Device didn't power on (%pe)\n", ERR_PTR(ret));
			return ret;
		}

		bl->full_on = true;
	}

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);

	if (brightness == APPLETB_BL_OFF) {
		hid_hw_power(hdev, PM_HINT_NORMAL);
		bl->full_on = false;
	}

	return 0;
}

static int appletb_bl_update_status(struct backlight_device *bdev)
{
	struct appletb_bl *bl = bl_get_data(bdev);
	u8 brightness;

	if (backlight_is_blank(bdev))
		brightness = APPLETB_BL_OFF;
	else
		brightness = appletb_bl_brightness_map[backlight_get_brightness(bdev)];

	return appletb_bl_set_brightness(bl, brightness);
}

static const struct backlight_ops appletb_bl_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = appletb_bl_update_status,
};

static int appletb_bl_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hid_field *aux1_field, *brightness_field;
	struct backlight_properties bl_props = { 0 };
	struct device *dev = &hdev->dev;
	struct appletb_bl *bl;
	int ret;

	ret = hid_parse(hdev);
	if (ret)
		return dev_err_probe(dev, ret, "HID parse failed\n");

	aux1_field = hid_find_field(hdev, HID_FEATURE_REPORT,
				    HID_VD_APPLE_TB_BRIGHTNESS, HID_USAGE_AUX1);

	brightness_field = hid_find_field(hdev, HID_FEATURE_REPORT,
					  HID_VD_APPLE_TB_BRIGHTNESS, HID_USAGE_BRIGHTNESS);

	if (!aux1_field || !brightness_field)
		return -ENODEV;

	if (aux1_field->report != brightness_field->report)
		return dev_err_probe(dev, -ENODEV, "Encountered unexpected report structure\n");

	bl = devm_kzalloc(dev, sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	ret = hid_hw_start(hdev, HID_CONNECT_DRIVER);
	if (ret)
		return dev_err_probe(dev, ret, "HID hardware start failed\n");

	ret = hid_hw_open(hdev);
	if (ret) {
		dev_err_probe(dev, ret, "HID hardware open failed\n");
		goto stop_hw;
	}

	bl->aux1_field = aux1_field;
	bl->brightness_field = brightness_field;

	ret = appletb_bl_set_brightness(bl,
		appletb_bl_brightness_map[(appletb_bl_def_brightness > 2) ? 2 : appletb_bl_def_brightness]);

	if (ret) {
		dev_err_probe(dev, ret, "Failed to set default touch bar brightness to %d\n",
			      appletb_bl_def_brightness);
		goto close_hw;
	}

	bl_props.type = BACKLIGHT_RAW;
	bl_props.max_brightness = ARRAY_SIZE(appletb_bl_brightness_map) - 1;

	bl->bdev = devm_backlight_device_register(dev, "appletb_backlight", dev, bl,
						  &appletb_bl_backlight_ops, &bl_props);
	if (IS_ERR(bl->bdev)) {
		ret = PTR_ERR(bl->bdev);
		dev_err_probe(dev, ret, "Failed to register backlight device\n");
		goto close_hw;
	}

	hid_set_drvdata(hdev, bl);

	return 0;

close_hw:
	hid_hw_close(hdev);
stop_hw:
	hid_hw_stop(hdev);

	return ret;
}

static void appletb_bl_remove(struct hid_device *hdev)
{
	struct appletb_bl *bl = hid_get_drvdata(hdev);

	appletb_bl_set_brightness(bl, APPLETB_BL_OFF);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id appletb_bl_hid_ids[] = {
	/* MacBook Pro's 2018, 2019, with T2 chip: iBridge DFR Brightness */
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_TOUCHBAR_BACKLIGHT) },
	{ }
};
MODULE_DEVICE_TABLE(hid, appletb_bl_hid_ids);

static struct hid_driver appletb_bl_hid_driver = {
	.name = "hid-appletb-bl",
	.id_table = appletb_bl_hid_ids,
	.probe = appletb_bl_probe,
	.remove = appletb_bl_remove,
};
module_hid_driver(appletb_bl_hid_driver);

MODULE_AUTHOR("Ronald Tschalär");
MODULE_AUTHOR("Kerem Karabay <kekrby@gmail.com>");
MODULE_DESCRIPTION("MacBook Pro Touch Bar Backlight driver");
MODULE_LICENSE("GPL");

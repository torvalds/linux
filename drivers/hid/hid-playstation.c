// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Sony DualSense(TM) controller.
 *
 *  Copyright (c) 2020 Sony Interactive Entertainment
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input/mt.h>
#include <linux/module.h>

#include <asm/unaligned.h>

#include "hid-ids.h"

#define HID_PLAYSTATION_VERSION_PATCH 0x8000

/* Base class for playstation devices. */
struct ps_device {
	struct hid_device *hdev;

	int (*parse_report)(struct ps_device *dev, struct hid_report *report, u8 *data, int size);
};

#define DS_INPUT_REPORT_USB			0x01
#define DS_INPUT_REPORT_USB_SIZE		64

/* Button masks for DualSense input report. */
#define DS_BUTTONS0_HAT_SWITCH	GENMASK(3, 0)
#define DS_BUTTONS0_SQUARE	BIT(4)
#define DS_BUTTONS0_CROSS	BIT(5)
#define DS_BUTTONS0_CIRCLE	BIT(6)
#define DS_BUTTONS0_TRIANGLE	BIT(7)
#define DS_BUTTONS1_L1		BIT(0)
#define DS_BUTTONS1_R1		BIT(1)
#define DS_BUTTONS1_L2		BIT(2)
#define DS_BUTTONS1_R2		BIT(3)
#define DS_BUTTONS1_CREATE	BIT(4)
#define DS_BUTTONS1_OPTIONS	BIT(5)
#define DS_BUTTONS1_L3		BIT(6)
#define DS_BUTTONS1_R3		BIT(7)
#define DS_BUTTONS2_PS_HOME	BIT(0)
#define DS_BUTTONS2_TOUCHPAD	BIT(1)

struct dualsense {
	struct ps_device base;
	struct input_dev *gamepad;
};

struct dualsense_touch_point {
	uint8_t contact;
	uint8_t x_lo;
	uint8_t x_hi:4, y_lo:4;
	uint8_t y_hi;
} __packed;
static_assert(sizeof(struct dualsense_touch_point) == 4);

/* Main DualSense input report excluding any BT/USB specific headers. */
struct dualsense_input_report {
	uint8_t x, y;
	uint8_t rx, ry;
	uint8_t z, rz;
	uint8_t seq_number;
	uint8_t buttons[4];
	uint8_t reserved[4];

	/* Motion sensors */
	__le16 gyro[3]; /* x, y, z */
	__le16 accel[3]; /* x, y, z */
	__le32 sensor_timestamp;
	uint8_t reserved2;

	/* Touchpad */
	struct dualsense_touch_point points[2];

	uint8_t reserved3[12];
	uint8_t status;
	uint8_t reserved4[10];
} __packed;
/* Common input report size shared equals the size of the USB report minus 1 byte for ReportID. */
static_assert(sizeof(struct dualsense_input_report) == DS_INPUT_REPORT_USB_SIZE - 1);

/*
 * Common gamepad buttons across DualShock 3 / 4 and DualSense.
 * Note: for device with a touchpad, touchpad button is not included
 *        as it will be part of the touchpad device.
 */
static const int ps_gamepad_buttons[] = {
	BTN_WEST, /* Square */
	BTN_NORTH, /* Triangle */
	BTN_EAST, /* Circle */
	BTN_SOUTH, /* Cross */
	BTN_TL, /* L1 */
	BTN_TR, /* R1 */
	BTN_TL2, /* L2 */
	BTN_TR2, /* R2 */
	BTN_SELECT, /* Create (PS5) / Share (PS4) */
	BTN_START, /* Option */
	BTN_THUMBL, /* L3 */
	BTN_THUMBR, /* R3 */
	BTN_MODE, /* PS Home */
};

static const struct {int x; int y; } ps_gamepad_hat_mapping[] = {
	{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
	{0, 0},
};

static struct input_dev *ps_allocate_input_dev(struct hid_device *hdev, const char *name_suffix)
{
	struct input_dev *input_dev;

	input_dev = devm_input_allocate_device(&hdev->dev);
	if (!input_dev)
		return ERR_PTR(-ENOMEM);

	input_dev->id.bustype = hdev->bus;
	input_dev->id.vendor = hdev->vendor;
	input_dev->id.product = hdev->product;
	input_dev->id.version = hdev->version;
	input_dev->uniq = hdev->uniq;

	if (name_suffix) {
		input_dev->name = devm_kasprintf(&hdev->dev, GFP_KERNEL, "%s %s", hdev->name,
				name_suffix);
		if (!input_dev->name)
			return ERR_PTR(-ENOMEM);
	} else {
		input_dev->name = hdev->name;
	}

	input_set_drvdata(input_dev, hdev);

	return input_dev;
}

static struct input_dev *ps_gamepad_create(struct hid_device *hdev)
{
	struct input_dev *gamepad;
	unsigned int i;
	int ret;

	gamepad = ps_allocate_input_dev(hdev, NULL);
	if (IS_ERR(gamepad))
		return ERR_CAST(gamepad);

	input_set_abs_params(gamepad, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(gamepad, ABS_Y, 0, 255, 0, 0);
	input_set_abs_params(gamepad, ABS_Z, 0, 255, 0, 0);
	input_set_abs_params(gamepad, ABS_RX, 0, 255, 0, 0);
	input_set_abs_params(gamepad, ABS_RY, 0, 255, 0, 0);
	input_set_abs_params(gamepad, ABS_RZ, 0, 255, 0, 0);

	input_set_abs_params(gamepad, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(gamepad, ABS_HAT0Y, -1, 1, 0, 0);

	for (i = 0; i < ARRAY_SIZE(ps_gamepad_buttons); i++)
		input_set_capability(gamepad, EV_KEY, ps_gamepad_buttons[i]);

	ret = input_register_device(gamepad);
	if (ret)
		return ERR_PTR(ret);

	return gamepad;
}

static int dualsense_parse_report(struct ps_device *ps_dev, struct hid_report *report,
		u8 *data, int size)
{
	struct hid_device *hdev = ps_dev->hdev;
	struct dualsense *ds = container_of(ps_dev, struct dualsense, base);
	struct dualsense_input_report *ds_report;
	uint8_t value;

	/*
	 * DualSense in USB uses the full HID report for reportID 1, but
	 * Bluetooth uses a minimal HID report for reportID 1 and reports
	 * the full report using reportID 49.
	 */
	if (hdev->bus == BUS_USB && report->id == DS_INPUT_REPORT_USB &&
			size == DS_INPUT_REPORT_USB_SIZE) {
		ds_report = (struct dualsense_input_report *)&data[1];
	} else {
		hid_err(hdev, "Unhandled reportID=%d\n", report->id);
		return -1;
	}

	input_report_abs(ds->gamepad, ABS_X,  ds_report->x);
	input_report_abs(ds->gamepad, ABS_Y,  ds_report->y);
	input_report_abs(ds->gamepad, ABS_RX, ds_report->rx);
	input_report_abs(ds->gamepad, ABS_RY, ds_report->ry);
	input_report_abs(ds->gamepad, ABS_Z,  ds_report->z);
	input_report_abs(ds->gamepad, ABS_RZ, ds_report->rz);

	value = ds_report->buttons[0] & DS_BUTTONS0_HAT_SWITCH;
	if (value > ARRAY_SIZE(ps_gamepad_hat_mapping))
		value = 8; /* center */
	input_report_abs(ds->gamepad, ABS_HAT0X, ps_gamepad_hat_mapping[value].x);
	input_report_abs(ds->gamepad, ABS_HAT0Y, ps_gamepad_hat_mapping[value].y);

	input_report_key(ds->gamepad, BTN_WEST,   ds_report->buttons[0] & DS_BUTTONS0_SQUARE);
	input_report_key(ds->gamepad, BTN_SOUTH,  ds_report->buttons[0] & DS_BUTTONS0_CROSS);
	input_report_key(ds->gamepad, BTN_EAST,   ds_report->buttons[0] & DS_BUTTONS0_CIRCLE);
	input_report_key(ds->gamepad, BTN_NORTH,  ds_report->buttons[0] & DS_BUTTONS0_TRIANGLE);
	input_report_key(ds->gamepad, BTN_TL,     ds_report->buttons[1] & DS_BUTTONS1_L1);
	input_report_key(ds->gamepad, BTN_TR,     ds_report->buttons[1] & DS_BUTTONS1_R1);
	input_report_key(ds->gamepad, BTN_TL2,    ds_report->buttons[1] & DS_BUTTONS1_L2);
	input_report_key(ds->gamepad, BTN_TR2,    ds_report->buttons[1] & DS_BUTTONS1_R2);
	input_report_key(ds->gamepad, BTN_SELECT, ds_report->buttons[1] & DS_BUTTONS1_CREATE);
	input_report_key(ds->gamepad, BTN_START,  ds_report->buttons[1] & DS_BUTTONS1_OPTIONS);
	input_report_key(ds->gamepad, BTN_THUMBL, ds_report->buttons[1] & DS_BUTTONS1_L3);
	input_report_key(ds->gamepad, BTN_THUMBR, ds_report->buttons[1] & DS_BUTTONS1_R3);
	input_report_key(ds->gamepad, BTN_MODE,   ds_report->buttons[2] & DS_BUTTONS2_PS_HOME);
	input_sync(ds->gamepad);

	return 0;
}

static struct ps_device *dualsense_create(struct hid_device *hdev)
{
	struct dualsense *ds;
	int ret;

	ds = devm_kzalloc(&hdev->dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return ERR_PTR(-ENOMEM);

	/*
	 * Patch version to allow userspace to distinguish between
	 * hid-generic vs hid-playstation axis and button mapping.
	 */
	hdev->version |= HID_PLAYSTATION_VERSION_PATCH;

	ds->base.hdev = hdev;
	ds->base.parse_report = dualsense_parse_report;
	hid_set_drvdata(hdev, ds);

	ds->gamepad = ps_gamepad_create(hdev);
	if (IS_ERR(ds->gamepad)) {
		ret = PTR_ERR(ds->gamepad);
		goto err;
	}

	return &ds->base;

err:
	return ERR_PTR(ret);
}

static int ps_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *data, int size)
{
	struct ps_device *dev = hid_get_drvdata(hdev);

	if (dev && dev->parse_report)
		return dev->parse_report(dev, report, data, size);

	return 0;
}

static int ps_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct ps_device *dev;
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "Failed to start HID device\n");
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "Failed to open HID device\n");
		goto err_stop;
	}

	if (hdev->product == USB_DEVICE_ID_SONY_PS5_CONTROLLER) {
		dev = dualsense_create(hdev);
		if (IS_ERR(dev)) {
			hid_err(hdev, "Failed to create dualsense.\n");
			ret = PTR_ERR(dev);
			goto err_close;
		}
	}

	return ret;

err_close:
	hid_hw_close(hdev);
err_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void ps_remove(struct hid_device *hdev)
{
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id ps_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS5_CONTROLLER) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ps_devices);

static struct hid_driver ps_driver = {
	.name		= "playstation",
	.id_table	= ps_devices,
	.probe		= ps_probe,
	.remove		= ps_remove,
	.raw_event	= ps_raw_event,
};

module_hid_driver(ps_driver);

MODULE_AUTHOR("Sony Interactive Entertainment");
MODULE_DESCRIPTION("HID Driver for PlayStation peripherals.");
MODULE_LICENSE("GPL");

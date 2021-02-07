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
	spinlock_t lock;

	struct power_supply_desc battery_desc;
	struct power_supply *battery;
	uint8_t battery_capacity;
	int battery_status;

	uint8_t mac_address[6]; /* Note: stored in little endian order. */

	int (*parse_report)(struct ps_device *dev, struct hid_report *report, u8 *data, int size);
};

#define DS_INPUT_REPORT_USB			0x01
#define DS_INPUT_REPORT_USB_SIZE		64

#define DS_FEATURE_REPORT_PAIRING_INFO		0x09
#define DS_FEATURE_REPORT_PAIRING_INFO_SIZE	20

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

/* Status field of DualSense input report. */
#define DS_STATUS_BATTERY_CAPACITY	GENMASK(3, 0)
#define DS_STATUS_CHARGING		GENMASK(7, 4)
#define DS_STATUS_CHARGING_SHIFT	4

/*
 * Status of a DualSense touch point contact.
 * Contact IDs, with highest bit set are 'inactive'
 * and any associated data is then invalid.
 */
#define DS_TOUCH_POINT_INACTIVE BIT(7)

/* DualSense hardware limits */
#define DS_TOUCHPAD_WIDTH	1920
#define DS_TOUCHPAD_HEIGHT	1080

struct dualsense {
	struct ps_device base;
	struct input_dev *gamepad;
	struct input_dev *touchpad;
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

static enum power_supply_property ps_power_supply_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
};

static int ps_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct ps_device *dev = power_supply_get_drvdata(psy);
	uint8_t battery_capacity;
	int battery_status;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev->lock, flags);
	battery_capacity = dev->battery_capacity;
	battery_status = dev->battery_status;
	spin_unlock_irqrestore(&dev->lock, flags);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = battery_status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = battery_capacity;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return 0;
}

static int ps_device_register_battery(struct ps_device *dev)
{
	struct power_supply *battery;
	struct power_supply_config battery_cfg = { .drv_data = dev };
	int ret;

	dev->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	dev->battery_desc.properties = ps_power_supply_props;
	dev->battery_desc.num_properties = ARRAY_SIZE(ps_power_supply_props);
	dev->battery_desc.get_property = ps_battery_get_property;
	dev->battery_desc.name = devm_kasprintf(&dev->hdev->dev, GFP_KERNEL,
			"ps-controller-battery-%pMR", dev->mac_address);
	if (!dev->battery_desc.name)
		return -ENOMEM;

	battery = devm_power_supply_register(&dev->hdev->dev, &dev->battery_desc, &battery_cfg);
	if (IS_ERR(battery)) {
		ret = PTR_ERR(battery);
		hid_err(dev->hdev, "Unable to register battery device: %d\n", ret);
		return ret;
	}
	dev->battery = battery;

	ret = power_supply_powers(dev->battery, &dev->hdev->dev);
	if (ret) {
		hid_err(dev->hdev, "Unable to activate battery device: %d\n", ret);
		return ret;
	}

	return 0;
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

static int ps_get_report(struct hid_device *hdev, uint8_t report_id, uint8_t *buf, size_t size)
{
	int ret;

	ret = hid_hw_raw_request(hdev, report_id, buf, size, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret < 0) {
		hid_err(hdev, "Failed to retrieve feature with reportID %d: %d\n", report_id, ret);
		return ret;
	}

	if (ret != size) {
		hid_err(hdev, "Invalid byte count transferred, expected %zu got %d\n", size, ret);
		return -EINVAL;
	}

	if (buf[0] != report_id) {
		hid_err(hdev, "Invalid reportID received, expected %d got %d\n", report_id, buf[0]);
		return -EINVAL;
	}

	return 0;
}

static struct input_dev *ps_touchpad_create(struct hid_device *hdev, int width, int height,
		unsigned int num_contacts)
{
	struct input_dev *touchpad;
	int ret;

	touchpad = ps_allocate_input_dev(hdev, "Touchpad");
	if (IS_ERR(touchpad))
		return ERR_CAST(touchpad);

	/* Map button underneath touchpad to BTN_LEFT. */
	input_set_capability(touchpad, EV_KEY, BTN_LEFT);
	__set_bit(INPUT_PROP_BUTTONPAD, touchpad->propbit);

	input_set_abs_params(touchpad, ABS_MT_POSITION_X, 0, width - 1, 0, 0);
	input_set_abs_params(touchpad, ABS_MT_POSITION_Y, 0, height - 1, 0, 0);

	ret = input_mt_init_slots(touchpad, num_contacts, INPUT_MT_POINTER);
	if (ret)
		return ERR_PTR(ret);

	ret = input_register_device(touchpad);
	if (ret)
		return ERR_PTR(ret);

	return touchpad;
}

static int dualsense_get_mac_address(struct dualsense *ds)
{
	uint8_t *buf;
	int ret = 0;

	buf = kzalloc(DS_FEATURE_REPORT_PAIRING_INFO_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = ps_get_report(ds->base.hdev, DS_FEATURE_REPORT_PAIRING_INFO, buf,
			DS_FEATURE_REPORT_PAIRING_INFO_SIZE);
	if (ret) {
		hid_err(ds->base.hdev, "Failed to retrieve DualSense pairing info: %d\n", ret);
		goto err_free;
	}

	memcpy(ds->base.mac_address, &buf[1], sizeof(ds->base.mac_address));

err_free:
	kfree(buf);
	return ret;
}

static int dualsense_parse_report(struct ps_device *ps_dev, struct hid_report *report,
		u8 *data, int size)
{
	struct hid_device *hdev = ps_dev->hdev;
	struct dualsense *ds = container_of(ps_dev, struct dualsense, base);
	struct dualsense_input_report *ds_report;
	uint8_t battery_data, battery_capacity, charging_status, value;
	int battery_status;
	unsigned long flags;
	int i;

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

	for (i = 0; i < ARRAY_SIZE(ds_report->points); i++) {
		struct dualsense_touch_point *point = &ds_report->points[i];
		bool active = (point->contact & DS_TOUCH_POINT_INACTIVE) ? false : true;

		input_mt_slot(ds->touchpad, i);
		input_mt_report_slot_state(ds->touchpad, MT_TOOL_FINGER, active);

		if (active) {
			int x = (point->x_hi << 8) | point->x_lo;
			int y = (point->y_hi << 4) | point->y_lo;

			input_report_abs(ds->touchpad, ABS_MT_POSITION_X, x);
			input_report_abs(ds->touchpad, ABS_MT_POSITION_Y, y);
		}
	}
	input_mt_sync_frame(ds->touchpad);
	input_report_key(ds->touchpad, BTN_LEFT, ds_report->buttons[2] & DS_BUTTONS2_TOUCHPAD);
	input_sync(ds->touchpad);

	battery_data = ds_report->status & DS_STATUS_BATTERY_CAPACITY;
	charging_status = (ds_report->status & DS_STATUS_CHARGING) >> DS_STATUS_CHARGING_SHIFT;

	switch (charging_status) {
	case 0x0:
		/*
		 * Each unit of battery data corresponds to 10%
		 * 0 = 0-9%, 1 = 10-19%, .. and 10 = 100%
		 */
		battery_capacity = min(battery_data * 10 + 5, 100);
		battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case 0x1:
		battery_capacity = min(battery_data * 10 + 5, 100);
		battery_status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case 0x2:
		battery_capacity = 100;
		battery_status = POWER_SUPPLY_STATUS_FULL;
		break;
	case 0xa: /* voltage or temperature out of range */
	case 0xb: /* temperature error */
		battery_capacity = 0;
		battery_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case 0xf: /* charging error */
	default:
		battery_capacity = 0;
		battery_status = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	spin_lock_irqsave(&ps_dev->lock, flags);
	ps_dev->battery_capacity = battery_capacity;
	ps_dev->battery_status = battery_status;
	spin_unlock_irqrestore(&ps_dev->lock, flags);

	return 0;
}

static struct ps_device *dualsense_create(struct hid_device *hdev)
{
	struct dualsense *ds;
	struct ps_device *ps_dev;
	int ret;

	ds = devm_kzalloc(&hdev->dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return ERR_PTR(-ENOMEM);

	/*
	 * Patch version to allow userspace to distinguish between
	 * hid-generic vs hid-playstation axis and button mapping.
	 */
	hdev->version |= HID_PLAYSTATION_VERSION_PATCH;

	ps_dev = &ds->base;
	ps_dev->hdev = hdev;
	spin_lock_init(&ps_dev->lock);
	ps_dev->battery_capacity = 100; /* initial value until parse_report. */
	ps_dev->battery_status = POWER_SUPPLY_STATUS_UNKNOWN;
	ps_dev->parse_report = dualsense_parse_report;
	hid_set_drvdata(hdev, ds);

	ret = dualsense_get_mac_address(ds);
	if (ret) {
		hid_err(hdev, "Failed to get MAC address from DualSense\n");
		return ERR_PTR(ret);
	}
	snprintf(hdev->uniq, sizeof(hdev->uniq), "%pMR", ds->base.mac_address);

	ds->gamepad = ps_gamepad_create(hdev);
	if (IS_ERR(ds->gamepad)) {
		ret = PTR_ERR(ds->gamepad);
		goto err;
	}

	ds->touchpad = ps_touchpad_create(hdev, DS_TOUCHPAD_WIDTH, DS_TOUCHPAD_HEIGHT, 2);
	if (IS_ERR(ds->touchpad)) {
		ret = PTR_ERR(ds->touchpad);
		goto err;
	}

	ret = ps_device_register_battery(ps_dev);
	if (ret)
		goto err;

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

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 *  HID driver for NVIDIA SHIELD peripherals.
 */

#include <linux/hid.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "hid-ids.h"

#define NOT_INIT_STR "NOT INITIALIZED"
#define android_map_key(c) hid_map_usage(hi, usage, bit, max, EV_KEY, (c))

enum {
	HID_USAGE_ANDROID_PLAYPAUSE_BTN = 0xcd, /* Double-tap volume slider */
	HID_USAGE_ANDROID_VOLUMEUP_BTN = 0xe9,
	HID_USAGE_ANDROID_VOLUMEDOWN_BTN = 0xea,
	HID_USAGE_ANDROID_SEARCH_BTN = 0x221, /* NVIDIA btn on Thunderstrike */
	HID_USAGE_ANDROID_HOME_BTN = 0x223,
	HID_USAGE_ANDROID_BACK_BTN = 0x224,
};

enum {
	SHIELD_FW_VERSION_INITIALIZED = 0,
	SHIELD_BOARD_INFO_INITIALIZED,
};

enum {
	THUNDERSTRIKE_FW_VERSION_UPDATE = 0,
	THUNDERSTRIKE_BOARD_INFO_UPDATE,
	THUNDERSTRIKE_HAPTICS_UPDATE,
	THUNDERSTRIKE_LED_UPDATE,
};

enum {
	THUNDERSTRIKE_HOSTCMD_REPORT_SIZE = 33,
	THUNDERSTRIKE_HOSTCMD_REQ_REPORT_ID = 0x4,
	THUNDERSTRIKE_HOSTCMD_RESP_REPORT_ID = 0x3,
};

enum {
	THUNDERSTRIKE_HOSTCMD_ID_FW_VERSION = 1,
	THUNDERSTRIKE_HOSTCMD_ID_LED = 6,
	THUNDERSTRIKE_HOSTCMD_ID_BOARD_INFO = 16,
	THUNDERSTRIKE_HOSTCMD_ID_USB_INIT = 53,
	THUNDERSTRIKE_HOSTCMD_ID_HAPTICS = 57,
	THUNDERSTRIKE_HOSTCMD_ID_BLUETOOTH_INIT = 58,
};

enum thunderstrike_led_state {
	THUNDERSTRIKE_LED_OFF = 1,
	THUNDERSTRIKE_LED_ON = 8,
} __packed;
static_assert(sizeof(enum thunderstrike_led_state) == 1);

struct thunderstrike_hostcmd_board_info {
	__le16 revision;
	__le16 serial[7];
};

struct thunderstrike_hostcmd_haptics {
	u8 motor_left;
	u8 motor_right;
};

struct thunderstrike_hostcmd_resp_report {
	u8 report_id; /* THUNDERSTRIKE_HOSTCMD_RESP_REPORT_ID */
	u8 cmd_id;
	u8 reserved_at_10;

	union {
		struct thunderstrike_hostcmd_board_info board_info;
		struct thunderstrike_hostcmd_haptics motors;
		__le16 fw_version;
		enum thunderstrike_led_state led_state;
		u8 payload[30];
	};
} __packed;
static_assert(sizeof(struct thunderstrike_hostcmd_resp_report) ==
	      THUNDERSTRIKE_HOSTCMD_REPORT_SIZE);

struct thunderstrike_hostcmd_req_report {
	u8 report_id; /* THUNDERSTRIKE_HOSTCMD_REQ_REPORT_ID */
	u8 cmd_id;
	u8 reserved_at_10;

	union {
		struct {
			u8 update;
			enum thunderstrike_led_state state;
		} led;
		struct {
			u8 update;
			struct thunderstrike_hostcmd_haptics motors;
		} haptics;
	};
	u8 reserved_at_30[27];
} __packed;
static_assert(sizeof(struct thunderstrike_hostcmd_req_report) ==
	      THUNDERSTRIKE_HOSTCMD_REPORT_SIZE);

/* Common struct for shield accessories. */
struct shield_device {
	struct hid_device *hdev;

	unsigned long initialized_flags;
	const char *codename;
	u16 fw_version;
	struct {
		u16 revision;
		char serial_number[15];
	} board_info;
};

struct thunderstrike {
	struct shield_device base;

	/* Sub-devices */
	struct input_dev *haptics_dev;
	struct led_classdev led_dev;

	/* Resources */
	void *req_report_dmabuf;
	unsigned long update_flags;
	struct thunderstrike_hostcmd_haptics haptics_val;
	spinlock_t haptics_update_lock;
	u8 led_state : 1;
	enum thunderstrike_led_state led_value;
	struct work_struct hostcmd_req_work;
};

static inline void thunderstrike_hostcmd_req_report_init(
	struct thunderstrike_hostcmd_req_report *report, u8 cmd_id)
{
	memset(report, 0, sizeof(*report));
	report->report_id = THUNDERSTRIKE_HOSTCMD_REQ_REPORT_ID;
	report->cmd_id = cmd_id;
}

static inline void shield_strrev(char *dest, size_t len, u16 rev)
{
	dest[0] = ('A' - 1) + (rev >> 8);
	snprintf(&dest[1], len - 1, "%02X", 0xff & rev);
}

static struct input_dev *shield_allocate_input_dev(struct hid_device *hdev,
						   const char *name_suffix)
{
	struct input_dev *idev;

	idev = input_allocate_device();
	if (!idev)
		goto err_device;

	idev->id.bustype = hdev->bus;
	idev->id.vendor = hdev->vendor;
	idev->id.product = hdev->product;
	idev->id.version = hdev->version;
	idev->uniq = hdev->uniq;
	idev->name = devm_kasprintf(&idev->dev, GFP_KERNEL, "%s %s", hdev->name,
				    name_suffix);
	if (!idev->name)
		goto err_name;

	input_set_drvdata(idev, hdev);

	return idev;

err_name:
	input_free_device(idev);
err_device:
	return ERR_PTR(-ENOMEM);
}

static struct input_dev *shield_haptics_create(
	struct shield_device *dev,
	int (*play_effect)(struct input_dev *, void *, struct ff_effect *))
{
	struct input_dev *haptics;
	int ret;

	if (!IS_ENABLED(CONFIG_NVIDIA_SHIELD_FF))
		return NULL;

	haptics = shield_allocate_input_dev(dev->hdev, "Haptics");
	if (IS_ERR(haptics))
		return haptics;

	input_set_capability(haptics, EV_FF, FF_RUMBLE);
	input_ff_create_memless(haptics, NULL, play_effect);

	ret = input_register_device(haptics);
	if (ret)
		goto err;

	return haptics;

err:
	input_free_device(haptics);
	return ERR_PTR(ret);
}

static inline void thunderstrike_send_hostcmd_request(struct thunderstrike *ts)
{
	struct thunderstrike_hostcmd_req_report *report = ts->req_report_dmabuf;
	struct shield_device *shield_dev = &ts->base;
	int ret;

	ret = hid_hw_raw_request(shield_dev->hdev, report->report_id,
				 ts->req_report_dmabuf,
				 THUNDERSTRIKE_HOSTCMD_REPORT_SIZE,
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);

	if (ret < 0) {
		hid_err(shield_dev->hdev,
			"Failed to output Thunderstrike HOSTCMD request HID report due to %pe\n",
			ERR_PTR(ret));
	}
}

static void thunderstrike_hostcmd_req_work_handler(struct work_struct *work)
{
	struct thunderstrike *ts =
		container_of(work, struct thunderstrike, hostcmd_req_work);
	struct thunderstrike_hostcmd_req_report *report;
	unsigned long flags;

	report = ts->req_report_dmabuf;

	if (test_and_clear_bit(THUNDERSTRIKE_FW_VERSION_UPDATE, &ts->update_flags)) {
		thunderstrike_hostcmd_req_report_init(
			report, THUNDERSTRIKE_HOSTCMD_ID_FW_VERSION);
		thunderstrike_send_hostcmd_request(ts);
	}

	if (test_and_clear_bit(THUNDERSTRIKE_LED_UPDATE, &ts->update_flags)) {
		thunderstrike_hostcmd_req_report_init(report, THUNDERSTRIKE_HOSTCMD_ID_LED);
		report->led.update = 1;
		report->led.state = ts->led_value;
		thunderstrike_send_hostcmd_request(ts);
	}

	if (test_and_clear_bit(THUNDERSTRIKE_BOARD_INFO_UPDATE, &ts->update_flags)) {
		thunderstrike_hostcmd_req_report_init(
			report, THUNDERSTRIKE_HOSTCMD_ID_BOARD_INFO);
		thunderstrike_send_hostcmd_request(ts);
	}

	if (test_and_clear_bit(THUNDERSTRIKE_HAPTICS_UPDATE, &ts->update_flags)) {
		thunderstrike_hostcmd_req_report_init(
			report, THUNDERSTRIKE_HOSTCMD_ID_HAPTICS);

		report->haptics.update = 1;
		spin_lock_irqsave(&ts->haptics_update_lock, flags);
		report->haptics.motors = ts->haptics_val;
		spin_unlock_irqrestore(&ts->haptics_update_lock, flags);

		thunderstrike_send_hostcmd_request(ts);
	}
}

static inline void thunderstrike_request_firmware_version(struct thunderstrike *ts)
{
	set_bit(THUNDERSTRIKE_FW_VERSION_UPDATE, &ts->update_flags);
	schedule_work(&ts->hostcmd_req_work);
}

static inline void thunderstrike_request_board_info(struct thunderstrike *ts)
{
	set_bit(THUNDERSTRIKE_BOARD_INFO_UPDATE, &ts->update_flags);
	schedule_work(&ts->hostcmd_req_work);
}

static inline int
thunderstrike_update_haptics(struct thunderstrike *ts,
			     struct thunderstrike_hostcmd_haptics *motors)
{
	unsigned long flags;

	spin_lock_irqsave(&ts->haptics_update_lock, flags);
	ts->haptics_val = *motors;
	spin_unlock_irqrestore(&ts->haptics_update_lock, flags);

	set_bit(THUNDERSTRIKE_HAPTICS_UPDATE, &ts->update_flags);
	schedule_work(&ts->hostcmd_req_work);

	return 0;
}

static int thunderstrike_play_effect(struct input_dev *idev, void *data,
				     struct ff_effect *effect)
{
	struct hid_device *hdev = input_get_drvdata(idev);
	struct thunderstrike_hostcmd_haptics motors;
	struct shield_device *shield_dev;
	struct thunderstrike *ts;

	if (effect->type != FF_RUMBLE)
		return 0;

	shield_dev = hid_get_drvdata(hdev);
	ts = container_of(shield_dev, struct thunderstrike, base);

	/* Thunderstrike motor values range from 0 to 32 inclusively */
	motors.motor_left = effect->u.rumble.strong_magnitude / 2047;
	motors.motor_right = effect->u.rumble.weak_magnitude / 2047;

	hid_dbg(hdev, "Thunderstrike FF_RUMBLE request, left: %u right: %u\n",
		motors.motor_left, motors.motor_right);

	return thunderstrike_update_haptics(ts, &motors);
}

static enum led_brightness
thunderstrike_led_get_brightness(struct led_classdev *led)
{
	struct hid_device *hdev = to_hid_device(led->dev->parent);
	struct shield_device *shield_dev = hid_get_drvdata(hdev);
	struct thunderstrike *ts;

	ts = container_of(shield_dev, struct thunderstrike, base);

	return ts->led_state;
}

static void thunderstrike_led_set_brightness(struct led_classdev *led,
					    enum led_brightness value)
{
	struct hid_device *hdev = to_hid_device(led->dev->parent);
	struct shield_device *shield_dev = hid_get_drvdata(hdev);
	struct thunderstrike *ts;

	ts = container_of(shield_dev, struct thunderstrike, base);

	switch (value) {
	case LED_OFF:
		ts->led_value = THUNDERSTRIKE_LED_OFF;
		break;
	default:
		ts->led_value = THUNDERSTRIKE_LED_ON;
		break;
	}

	set_bit(THUNDERSTRIKE_LED_UPDATE, &ts->update_flags);
	schedule_work(&ts->hostcmd_req_work);
}

static void
thunderstrike_parse_fw_version_payload(struct shield_device *shield_dev,
				       __le16 fw_version)
{
	shield_dev->fw_version = le16_to_cpu(fw_version);

	set_bit(SHIELD_FW_VERSION_INITIALIZED, &shield_dev->initialized_flags);

	hid_dbg(shield_dev->hdev, "Thunderstrike firmware version 0x%04X\n",
		shield_dev->fw_version);
}

static void
thunderstrike_parse_board_info_payload(struct shield_device *shield_dev,
				       struct thunderstrike_hostcmd_board_info *board_info)
{
	char board_revision_str[4];
	int i;

	shield_dev->board_info.revision = le16_to_cpu(board_info->revision);
	for (i = 0; i < 7; ++i) {
		u16 val = le16_to_cpu(board_info->serial[i]);

		shield_dev->board_info.serial_number[2 * i] = val & 0xFF;
		shield_dev->board_info.serial_number[2 * i + 1] = val >> 8;
	}
	shield_dev->board_info.serial_number[14] = '\0';

	set_bit(SHIELD_BOARD_INFO_INITIALIZED, &shield_dev->initialized_flags);

	shield_strrev(board_revision_str, 4, shield_dev->board_info.revision);
	hid_dbg(shield_dev->hdev,
		"Thunderstrike BOARD_REVISION_%s (0x%04X) S/N: %s\n",
		board_revision_str, shield_dev->board_info.revision,
		shield_dev->board_info.serial_number);
}

static inline void
thunderstrike_parse_haptics_payload(struct shield_device *shield_dev,
				    struct thunderstrike_hostcmd_haptics *haptics)
{
	hid_dbg(shield_dev->hdev,
		"Thunderstrike haptics HOSTCMD response, left: %u right: %u\n",
		haptics->motor_left, haptics->motor_right);
}

static void
thunderstrike_parse_led_payload(struct shield_device *shield_dev,
				enum thunderstrike_led_state led_state)
{
	struct thunderstrike *ts = container_of(shield_dev, struct thunderstrike, base);

	switch (led_state) {
	case THUNDERSTRIKE_LED_OFF:
		ts->led_state = 0;
		break;
	case THUNDERSTRIKE_LED_ON:
		ts->led_state = 1;
		break;
	}

	hid_dbg(shield_dev->hdev, "Thunderstrike led HOSTCMD response, 0x%02X\n", led_state);
}

static int thunderstrike_parse_report(struct shield_device *shield_dev,
				      struct hid_report *report, u8 *data,
				      int size)
{
	struct thunderstrike_hostcmd_resp_report *hostcmd_resp_report;
	struct thunderstrike *ts =
		container_of(shield_dev, struct thunderstrike, base);
	struct hid_device *hdev = shield_dev->hdev;

	switch (report->id) {
	case THUNDERSTRIKE_HOSTCMD_RESP_REPORT_ID:
		if (size != THUNDERSTRIKE_HOSTCMD_REPORT_SIZE) {
			hid_err(hdev,
				"Encountered Thunderstrike HOSTCMD HID report with unexpected size %d\n",
				size);
			return -EINVAL;
		}

		hostcmd_resp_report =
			(struct thunderstrike_hostcmd_resp_report *)data;

		switch (hostcmd_resp_report->cmd_id) {
		case THUNDERSTRIKE_HOSTCMD_ID_FW_VERSION:
			thunderstrike_parse_fw_version_payload(
				shield_dev, hostcmd_resp_report->fw_version);
			break;
		case THUNDERSTRIKE_HOSTCMD_ID_LED:
			thunderstrike_parse_led_payload(shield_dev, hostcmd_resp_report->led_state);
			break;
		case THUNDERSTRIKE_HOSTCMD_ID_BOARD_INFO:
			thunderstrike_parse_board_info_payload(
				shield_dev, &hostcmd_resp_report->board_info);
			break;
		case THUNDERSTRIKE_HOSTCMD_ID_HAPTICS:
			thunderstrike_parse_haptics_payload(
				shield_dev, &hostcmd_resp_report->motors);
			break;

		case THUNDERSTRIKE_HOSTCMD_ID_USB_INIT:
		case THUNDERSTRIKE_HOSTCMD_ID_BLUETOOTH_INIT:
			/* May block HOSTCMD requests till received initially */
			thunderstrike_request_firmware_version(ts);
			thunderstrike_request_board_info(ts);
			/* Only HOSTCMD that can be triggered without a request */
			return 0;
		default:
			hid_warn(hdev,
				 "Unhandled Thunderstrike HOSTCMD id %d\n",
				 hostcmd_resp_report->cmd_id);
			return -ENOENT;
		}

		break;
	default:
		return 0;
	}

	return 0;
}

static inline int thunderstrike_led_create(struct thunderstrike *ts)
{
	struct led_classdev *led = &ts->led_dev;

	led->name = "thunderstrike:blue:led";
	led->max_brightness = 1;
	led->flags = LED_CORE_SUSPENDRESUME;
	led->brightness_get = &thunderstrike_led_get_brightness;
	led->brightness_set = &thunderstrike_led_set_brightness;

	return led_classdev_register(&ts->base.hdev->dev, led);
}

static struct shield_device *thunderstrike_create(struct hid_device *hdev)
{
	struct shield_device *shield_dev;
	struct thunderstrike *ts;
	int ret;

	ts = devm_kzalloc(&hdev->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return ERR_PTR(-ENOMEM);

	ts->req_report_dmabuf = devm_kzalloc(
		&hdev->dev, THUNDERSTRIKE_HOSTCMD_REPORT_SIZE, GFP_KERNEL);
	if (!ts->req_report_dmabuf)
		return ERR_PTR(-ENOMEM);

	shield_dev = &ts->base;
	shield_dev->hdev = hdev;
	shield_dev->codename = "Thunderstrike";

	spin_lock_init(&ts->haptics_update_lock);
	INIT_WORK(&ts->hostcmd_req_work, thunderstrike_hostcmd_req_work_handler);

	hid_set_drvdata(hdev, shield_dev);

	ret = thunderstrike_led_create(ts);
	if (ret) {
		hid_err(hdev, "Failed to create Thunderstrike LED instance\n");
		return ERR_PTR(ret);
	}

	ts->haptics_dev = shield_haptics_create(shield_dev, thunderstrike_play_effect);
	if (IS_ERR(ts->haptics_dev))
		goto err;

	hid_info(hdev, "Registered Thunderstrike controller\n");
	return shield_dev;

err:
	led_classdev_unregister(&ts->led_dev);
	return ERR_CAST(ts->haptics_dev);
}

static int android_input_mapping(struct hid_device *hdev, struct hid_input *hi,
				 struct hid_field *field,
				 struct hid_usage *usage, unsigned long **bit,
				 int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case HID_USAGE_ANDROID_PLAYPAUSE_BTN:
		android_map_key(KEY_PLAYPAUSE);
		break;
	case HID_USAGE_ANDROID_VOLUMEUP_BTN:
		android_map_key(KEY_VOLUMEUP);
		break;
	case HID_USAGE_ANDROID_VOLUMEDOWN_BTN:
		android_map_key(KEY_VOLUMEDOWN);
		break;
	case HID_USAGE_ANDROID_SEARCH_BTN:
		android_map_key(BTN_Z);
		break;
	case HID_USAGE_ANDROID_HOME_BTN:
		android_map_key(BTN_MODE);
		break;
	case HID_USAGE_ANDROID_BACK_BTN:
		android_map_key(BTN_SELECT);
		break;
	default:
		return 0;
	}

	return 1;
}

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct shield_device *shield_dev;
	int ret;

	shield_dev = hid_get_drvdata(hdev);

	if (test_bit(SHIELD_FW_VERSION_INITIALIZED, &shield_dev->initialized_flags))
		ret = sysfs_emit(buf, "0x%04X\n", shield_dev->fw_version);
	else
		ret = sysfs_emit(buf, NOT_INIT_STR "\n");

	return ret;
}

static DEVICE_ATTR_RO(firmware_version);

static ssize_t hardware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct shield_device *shield_dev;
	char board_revision_str[4];
	int ret;

	shield_dev = hid_get_drvdata(hdev);

	if (test_bit(SHIELD_BOARD_INFO_INITIALIZED, &shield_dev->initialized_flags)) {
		shield_strrev(board_revision_str, 4, shield_dev->board_info.revision);
		ret = sysfs_emit(buf, "%s BOARD_REVISION_%s (0x%04X)\n",
				 shield_dev->codename, board_revision_str,
				 shield_dev->board_info.revision);
	} else
		ret = sysfs_emit(buf, NOT_INIT_STR "\n");

	return ret;
}

static DEVICE_ATTR_RO(hardware_version);

static ssize_t serial_number_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct shield_device *shield_dev;
	int ret;

	shield_dev = hid_get_drvdata(hdev);

	if (test_bit(SHIELD_BOARD_INFO_INITIALIZED, &shield_dev->initialized_flags))
		ret = sysfs_emit(buf, "%s\n", shield_dev->board_info.serial_number);
	else
		ret = sysfs_emit(buf, NOT_INIT_STR "\n");

	return ret;
}

static DEVICE_ATTR_RO(serial_number);

static struct attribute *shield_device_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_hardware_version.attr,
	&dev_attr_serial_number.attr,
	NULL,
};
ATTRIBUTE_GROUPS(shield_device);

static int shield_raw_event(struct hid_device *hdev, struct hid_report *report,
			    u8 *data, int size)
{
	struct shield_device *dev = hid_get_drvdata(hdev);

	return thunderstrike_parse_report(dev, report, data, size);
}

static int shield_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct shield_device *shield_dev = NULL;
	struct thunderstrike *ts;
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Parse failed\n");
		return ret;
	}

	switch (id->product) {
	case USB_DEVICE_ID_NVIDIA_THUNDERSTRIKE_CONTROLLER:
		shield_dev = thunderstrike_create(hdev);
		break;
	}

	if (unlikely(!shield_dev)) {
		hid_err(hdev, "Failed to identify SHIELD device\n");
		return -ENODEV;
	}
	if (IS_ERR(shield_dev)) {
		hid_err(hdev, "Failed to create SHIELD device\n");
		return PTR_ERR(shield_dev);
	}

	ts = container_of(shield_dev, struct thunderstrike, base);

	ret = hid_hw_start(hdev, HID_CONNECT_HIDINPUT);
	if (ret) {
		hid_err(hdev, "Failed to start HID device\n");
		goto err_haptics;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "Failed to open HID device\n");
		goto err_stop;
	}

	thunderstrike_request_firmware_version(ts);
	thunderstrike_request_board_info(ts);

	return ret;

err_stop:
	hid_hw_stop(hdev);
err_haptics:
	if (ts->haptics_dev)
		input_unregister_device(ts->haptics_dev);
	return ret;
}

static void shield_remove(struct hid_device *hdev)
{
	struct shield_device *dev = hid_get_drvdata(hdev);
	struct thunderstrike *ts;

	ts = container_of(dev, struct thunderstrike, base);

	hid_hw_close(hdev);
	led_classdev_unregister(&ts->led_dev);
	if (ts->haptics_dev)
		input_unregister_device(ts->haptics_dev);
	cancel_work_sync(&ts->hostcmd_req_work);
	hid_hw_stop(hdev);
}

static const struct hid_device_id shield_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NVIDIA,
			       USB_DEVICE_ID_NVIDIA_THUNDERSTRIKE_CONTROLLER) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NVIDIA,
			 USB_DEVICE_ID_NVIDIA_THUNDERSTRIKE_CONTROLLER) },
	{ }
};
MODULE_DEVICE_TABLE(hid, shield_devices);

static struct hid_driver shield_driver = {
	.name          = "shield",
	.id_table      = shield_devices,
	.input_mapping = android_input_mapping,
	.probe         = shield_probe,
	.remove        = shield_remove,
	.raw_event     = shield_raw_event,
	.driver = {
		.dev_groups = shield_device_groups,
	},
};
module_hid_driver(shield_driver);

MODULE_AUTHOR("Rahul Rameshbabu <rrameshbabu@nvidia.com>");
MODULE_DESCRIPTION("HID Driver for NVIDIA SHIELD peripherals.");
MODULE_LICENSE("GPL");

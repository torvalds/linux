// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Sony DualSense(TM) controller.
 *
 *  Copyright (c) 2020 Sony Interactive Entertainment
 */

#include <linux/bits.h>
#include <linux/crc32.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/idr.h>
#include <linux/input/mt.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>

#include <asm/unaligned.h>

#include "hid-ids.h"

/* List of connected playstation devices. */
static DEFINE_MUTEX(ps_devices_lock);
static LIST_HEAD(ps_devices_list);

static DEFINE_IDA(ps_player_id_allocator);

#define HID_PLAYSTATION_VERSION_PATCH 0x8000

/* Base class for playstation devices. */
struct ps_device {
	struct list_head list;
	struct hid_device *hdev;
	spinlock_t lock;

	uint32_t player_id;

	struct power_supply_desc battery_desc;
	struct power_supply *battery;
	uint8_t battery_capacity;
	int battery_status;

	const char *input_dev_name; /* Name of primary input device. */
	uint8_t mac_address[6]; /* Note: stored in little endian order. */
	uint32_t hw_version;
	uint32_t fw_version;

	int (*parse_report)(struct ps_device *dev, struct hid_report *report, u8 *data, int size);
	void (*remove)(struct ps_device *dev);
};

/* Calibration data for playstation motion sensors. */
struct ps_calibration_data {
	int abs_code;
	short bias;
	int sens_numer;
	int sens_denom;
};

struct ps_led_info {
	const char *name;
	const char *color;
	enum led_brightness (*brightness_get)(struct led_classdev *cdev);
	int (*brightness_set)(struct led_classdev *cdev, enum led_brightness);
};

/* Seed values for DualShock4 / DualSense CRC32 for different report types. */
#define PS_INPUT_CRC32_SEED	0xA1
#define PS_OUTPUT_CRC32_SEED	0xA2
#define PS_FEATURE_CRC32_SEED	0xA3

#define DS_INPUT_REPORT_USB			0x01
#define DS_INPUT_REPORT_USB_SIZE		64
#define DS_INPUT_REPORT_BT			0x31
#define DS_INPUT_REPORT_BT_SIZE			78
#define DS_OUTPUT_REPORT_USB			0x02
#define DS_OUTPUT_REPORT_USB_SIZE		63
#define DS_OUTPUT_REPORT_BT			0x31
#define DS_OUTPUT_REPORT_BT_SIZE		78

#define DS_FEATURE_REPORT_CALIBRATION		0x05
#define DS_FEATURE_REPORT_CALIBRATION_SIZE	41
#define DS_FEATURE_REPORT_PAIRING_INFO		0x09
#define DS_FEATURE_REPORT_PAIRING_INFO_SIZE	20
#define DS_FEATURE_REPORT_FIRMWARE_INFO		0x20
#define DS_FEATURE_REPORT_FIRMWARE_INFO_SIZE	64

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
#define DS_BUTTONS2_MIC_MUTE	BIT(2)

/* Status field of DualSense input report. */
#define DS_STATUS_BATTERY_CAPACITY	GENMASK(3, 0)
#define DS_STATUS_CHARGING		GENMASK(7, 4)
#define DS_STATUS_CHARGING_SHIFT	4

/* Feature version from DualSense Firmware Info report. */
#define DS_FEATURE_VERSION(major, minor) ((major & 0xff) << 8 | (minor & 0xff))

/*
 * Status of a DualSense touch point contact.
 * Contact IDs, with highest bit set are 'inactive'
 * and any associated data is then invalid.
 */
#define DS_TOUCH_POINT_INACTIVE BIT(7)

 /* Magic value required in tag field of Bluetooth output report. */
#define DS_OUTPUT_TAG 0x10
/* Flags for DualSense output report. */
#define DS_OUTPUT_VALID_FLAG0_COMPATIBLE_VIBRATION BIT(0)
#define DS_OUTPUT_VALID_FLAG0_HAPTICS_SELECT BIT(1)
#define DS_OUTPUT_VALID_FLAG1_MIC_MUTE_LED_CONTROL_ENABLE BIT(0)
#define DS_OUTPUT_VALID_FLAG1_POWER_SAVE_CONTROL_ENABLE BIT(1)
#define DS_OUTPUT_VALID_FLAG1_LIGHTBAR_CONTROL_ENABLE BIT(2)
#define DS_OUTPUT_VALID_FLAG1_RELEASE_LEDS BIT(3)
#define DS_OUTPUT_VALID_FLAG1_PLAYER_INDICATOR_CONTROL_ENABLE BIT(4)
#define DS_OUTPUT_VALID_FLAG2_LIGHTBAR_SETUP_CONTROL_ENABLE BIT(1)
#define DS_OUTPUT_VALID_FLAG2_COMPATIBLE_VIBRATION2 BIT(2)
#define DS_OUTPUT_POWER_SAVE_CONTROL_MIC_MUTE BIT(4)
#define DS_OUTPUT_LIGHTBAR_SETUP_LIGHT_OUT BIT(1)

/* DualSense hardware limits */
#define DS_ACC_RES_PER_G	8192
#define DS_ACC_RANGE		(4*DS_ACC_RES_PER_G)
#define DS_GYRO_RES_PER_DEG_S	1024
#define DS_GYRO_RANGE		(2048*DS_GYRO_RES_PER_DEG_S)
#define DS_TOUCHPAD_WIDTH	1920
#define DS_TOUCHPAD_HEIGHT	1080

struct dualsense {
	struct ps_device base;
	struct input_dev *gamepad;
	struct input_dev *sensors;
	struct input_dev *touchpad;

	/* Update version is used as a feature/capability version. */
	uint16_t update_version;

	/* Calibration data for accelerometer and gyroscope. */
	struct ps_calibration_data accel_calib_data[3];
	struct ps_calibration_data gyro_calib_data[3];

	/* Timestamp for sensor data */
	bool sensor_timestamp_initialized;
	uint32_t prev_sensor_timestamp;
	uint32_t sensor_timestamp_us;

	/* Compatible rumble state */
	bool use_vibration_v2;
	bool update_rumble;
	uint8_t motor_left;
	uint8_t motor_right;

	/* RGB lightbar */
	struct led_classdev_mc lightbar;
	bool update_lightbar;
	uint8_t lightbar_red;
	uint8_t lightbar_green;
	uint8_t lightbar_blue;

	/* Microphone */
	bool update_mic_mute;
	bool mic_muted;
	bool last_btn_mic_state;

	/* Player leds */
	bool update_player_leds;
	uint8_t player_leds_state;
	struct led_classdev player_leds[5];

	struct work_struct output_worker;
	bool output_worker_initialized;
	void *output_report_dmabuf;
	uint8_t output_seq; /* Sequence number for output report. */
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

/* Common data between DualSense BT/USB main output report. */
struct dualsense_output_report_common {
	uint8_t valid_flag0;
	uint8_t valid_flag1;

	/* For DualShock 4 compatibility mode. */
	uint8_t motor_right;
	uint8_t motor_left;

	/* Audio controls */
	uint8_t reserved[4];
	uint8_t mute_button_led;

	uint8_t power_save_control;
	uint8_t reserved2[28];

	/* LEDs and lightbar */
	uint8_t valid_flag2;
	uint8_t reserved3[2];
	uint8_t lightbar_setup;
	uint8_t led_brightness;
	uint8_t player_leds;
	uint8_t lightbar_red;
	uint8_t lightbar_green;
	uint8_t lightbar_blue;
} __packed;
static_assert(sizeof(struct dualsense_output_report_common) == 47);

struct dualsense_output_report_bt {
	uint8_t report_id; /* 0x31 */
	uint8_t seq_tag;
	uint8_t tag;
	struct dualsense_output_report_common common;
	uint8_t reserved[24];
	__le32 crc32;
} __packed;
static_assert(sizeof(struct dualsense_output_report_bt) == DS_OUTPUT_REPORT_BT_SIZE);

struct dualsense_output_report_usb {
	uint8_t report_id; /* 0x02 */
	struct dualsense_output_report_common common;
	uint8_t reserved[15];
} __packed;
static_assert(sizeof(struct dualsense_output_report_usb) == DS_OUTPUT_REPORT_USB_SIZE);

/*
 * The DualSense has a main output report used to control most features. It is
 * largely the same between Bluetooth and USB except for different headers and CRC.
 * This structure hide the differences between the two to simplify sending output reports.
 */
struct dualsense_output_report {
	uint8_t *data; /* Start of data */
	uint8_t len; /* Size of output report */

	/* Points to Bluetooth data payload in case for a Bluetooth report else NULL. */
	struct dualsense_output_report_bt *bt;
	/* Points to USB data payload in case for a USB report else NULL. */
	struct dualsense_output_report_usb *usb;
	/* Points to common section of report, so past any headers. */
	struct dualsense_output_report_common *common;
};

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

static inline void dualsense_schedule_work(struct dualsense *ds);
static void dualsense_set_lightbar(struct dualsense *ds, uint8_t red, uint8_t green, uint8_t blue);

/*
 * Add a new ps_device to ps_devices if it doesn't exist.
 * Return error on duplicate device, which can happen if the same
 * device is connected using both Bluetooth and USB.
 */
static int ps_devices_list_add(struct ps_device *dev)
{
	struct ps_device *entry;

	mutex_lock(&ps_devices_lock);
	list_for_each_entry(entry, &ps_devices_list, list) {
		if (!memcmp(entry->mac_address, dev->mac_address, sizeof(dev->mac_address))) {
			hid_err(dev->hdev, "Duplicate device found for MAC address %pMR.\n",
					dev->mac_address);
			mutex_unlock(&ps_devices_lock);
			return -EEXIST;
		}
	}

	list_add_tail(&dev->list, &ps_devices_list);
	mutex_unlock(&ps_devices_lock);
	return 0;
}

static int ps_devices_list_remove(struct ps_device *dev)
{
	mutex_lock(&ps_devices_lock);
	list_del(&dev->list);
	mutex_unlock(&ps_devices_lock);
	return 0;
}

static int ps_device_set_player_id(struct ps_device *dev)
{
	int ret = ida_alloc(&ps_player_id_allocator, GFP_KERNEL);

	if (ret < 0)
		return ret;

	dev->player_id = ret;
	return 0;
}

static void ps_device_release_player_id(struct ps_device *dev)
{
	ida_free(&ps_player_id_allocator, dev->player_id);

	dev->player_id = U32_MAX;
}

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
	int ret = 0;

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

	return ret;
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

/* Compute crc32 of HID data and compare against expected CRC. */
static bool ps_check_crc32(uint8_t seed, uint8_t *data, size_t len, uint32_t report_crc)
{
	uint32_t crc;

	crc = crc32_le(0xFFFFFFFF, &seed, 1);
	crc = ~crc32_le(crc, data, len);

	return crc == report_crc;
}

static struct input_dev *ps_gamepad_create(struct hid_device *hdev,
		int (*play_effect)(struct input_dev *, void *, struct ff_effect *))
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

#if IS_ENABLED(CONFIG_PLAYSTATION_FF)
	if (play_effect) {
		input_set_capability(gamepad, EV_FF, FF_RUMBLE);
		input_ff_create_memless(gamepad, NULL, play_effect);
	}
#endif

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

	if (hdev->bus == BUS_BLUETOOTH) {
		/* Last 4 bytes contains crc32. */
		uint8_t crc_offset = size - 4;
		uint32_t report_crc = get_unaligned_le32(&buf[crc_offset]);

		if (!ps_check_crc32(PS_FEATURE_CRC32_SEED, buf, crc_offset, report_crc)) {
			hid_err(hdev, "CRC check failed for reportID=%d\n", report_id);
			return -EILSEQ;
		}
	}

	return 0;
}

static int ps_led_register(struct ps_device *ps_dev, struct led_classdev *led,
		const struct ps_led_info *led_info)
{
	int ret;

	led->name = devm_kasprintf(&ps_dev->hdev->dev, GFP_KERNEL,
			"%s:%s:%s", ps_dev->input_dev_name, led_info->color, led_info->name);

	if (!led->name)
		return -ENOMEM;

	led->brightness = 0;
	led->max_brightness = 1;
	led->flags = LED_CORE_SUSPENDRESUME;
	led->brightness_get = led_info->brightness_get;
	led->brightness_set_blocking = led_info->brightness_set;

	ret = devm_led_classdev_register(&ps_dev->hdev->dev, led);
	if (ret) {
		hid_err(ps_dev->hdev, "Failed to register LED %s: %d\n", led_info->name, ret);
		return ret;
	}

	return 0;
}

/* Register a DualSense/DualShock4 RGB lightbar represented by a multicolor LED. */
static int ps_lightbar_register(struct ps_device *ps_dev, struct led_classdev_mc *lightbar_mc_dev,
	int (*brightness_set)(struct led_classdev *, enum led_brightness))
{
	struct hid_device *hdev = ps_dev->hdev;
	struct mc_subled *mc_led_info;
	struct led_classdev *led_cdev;
	int ret;

	mc_led_info = devm_kmalloc_array(&hdev->dev, 3, sizeof(*mc_led_info),
					 GFP_KERNEL | __GFP_ZERO);
	if (!mc_led_info)
		return -ENOMEM;

	mc_led_info[0].color_index = LED_COLOR_ID_RED;
	mc_led_info[1].color_index = LED_COLOR_ID_GREEN;
	mc_led_info[2].color_index = LED_COLOR_ID_BLUE;

	lightbar_mc_dev->subled_info = mc_led_info;
	lightbar_mc_dev->num_colors = 3;

	led_cdev = &lightbar_mc_dev->led_cdev;
	led_cdev->name = devm_kasprintf(&hdev->dev, GFP_KERNEL, "%s:rgb:indicator",
			ps_dev->input_dev_name);
	if (!led_cdev->name)
		return -ENOMEM;
	led_cdev->brightness = 255;
	led_cdev->max_brightness = 255;
	led_cdev->brightness_set_blocking = brightness_set;

	ret = devm_led_classdev_multicolor_register(&hdev->dev, lightbar_mc_dev);
	if (ret < 0) {
		hid_err(hdev, "Cannot register multicolor LED device\n");
		return ret;
	}

	return 0;
}

static struct input_dev *ps_sensors_create(struct hid_device *hdev, int accel_range, int accel_res,
		int gyro_range, int gyro_res)
{
	struct input_dev *sensors;
	int ret;

	sensors = ps_allocate_input_dev(hdev, "Motion Sensors");
	if (IS_ERR(sensors))
		return ERR_CAST(sensors);

	__set_bit(INPUT_PROP_ACCELEROMETER, sensors->propbit);
	__set_bit(EV_MSC, sensors->evbit);
	__set_bit(MSC_TIMESTAMP, sensors->mscbit);

	/* Accelerometer */
	input_set_abs_params(sensors, ABS_X, -accel_range, accel_range, 16, 0);
	input_set_abs_params(sensors, ABS_Y, -accel_range, accel_range, 16, 0);
	input_set_abs_params(sensors, ABS_Z, -accel_range, accel_range, 16, 0);
	input_abs_set_res(sensors, ABS_X, accel_res);
	input_abs_set_res(sensors, ABS_Y, accel_res);
	input_abs_set_res(sensors, ABS_Z, accel_res);

	/* Gyroscope */
	input_set_abs_params(sensors, ABS_RX, -gyro_range, gyro_range, 16, 0);
	input_set_abs_params(sensors, ABS_RY, -gyro_range, gyro_range, 16, 0);
	input_set_abs_params(sensors, ABS_RZ, -gyro_range, gyro_range, 16, 0);
	input_abs_set_res(sensors, ABS_RX, gyro_res);
	input_abs_set_res(sensors, ABS_RY, gyro_res);
	input_abs_set_res(sensors, ABS_RZ, gyro_res);

	ret = input_register_device(sensors);
	if (ret)
		return ERR_PTR(ret);

	return sensors;
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

static ssize_t firmware_version_show(struct device *dev,
				struct device_attribute
				*attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct ps_device *ps_dev = hid_get_drvdata(hdev);

	return sysfs_emit(buf, "0x%08x\n", ps_dev->fw_version);
}

static DEVICE_ATTR_RO(firmware_version);

static ssize_t hardware_version_show(struct device *dev,
				struct device_attribute
				*attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct ps_device *ps_dev = hid_get_drvdata(hdev);

	return sysfs_emit(buf, "0x%08x\n", ps_dev->hw_version);
}

static DEVICE_ATTR_RO(hardware_version);

static struct attribute *ps_device_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_hardware_version.attr,
	NULL
};
ATTRIBUTE_GROUPS(ps_device);

static int dualsense_get_calibration_data(struct dualsense *ds)
{
	short gyro_pitch_bias, gyro_pitch_plus, gyro_pitch_minus;
	short gyro_yaw_bias, gyro_yaw_plus, gyro_yaw_minus;
	short gyro_roll_bias, gyro_roll_plus, gyro_roll_minus;
	short gyro_speed_plus, gyro_speed_minus;
	short acc_x_plus, acc_x_minus;
	short acc_y_plus, acc_y_minus;
	short acc_z_plus, acc_z_minus;
	int speed_2x;
	int range_2g;
	int ret = 0;
	uint8_t *buf;

	buf = kzalloc(DS_FEATURE_REPORT_CALIBRATION_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = ps_get_report(ds->base.hdev, DS_FEATURE_REPORT_CALIBRATION, buf,
			DS_FEATURE_REPORT_CALIBRATION_SIZE);
	if (ret) {
		hid_err(ds->base.hdev, "Failed to retrieve DualSense calibration info: %d\n", ret);
		goto err_free;
	}

	gyro_pitch_bias  = get_unaligned_le16(&buf[1]);
	gyro_yaw_bias    = get_unaligned_le16(&buf[3]);
	gyro_roll_bias   = get_unaligned_le16(&buf[5]);
	gyro_pitch_plus  = get_unaligned_le16(&buf[7]);
	gyro_pitch_minus = get_unaligned_le16(&buf[9]);
	gyro_yaw_plus    = get_unaligned_le16(&buf[11]);
	gyro_yaw_minus   = get_unaligned_le16(&buf[13]);
	gyro_roll_plus   = get_unaligned_le16(&buf[15]);
	gyro_roll_minus  = get_unaligned_le16(&buf[17]);
	gyro_speed_plus  = get_unaligned_le16(&buf[19]);
	gyro_speed_minus = get_unaligned_le16(&buf[21]);
	acc_x_plus       = get_unaligned_le16(&buf[23]);
	acc_x_minus      = get_unaligned_le16(&buf[25]);
	acc_y_plus       = get_unaligned_le16(&buf[27]);
	acc_y_minus      = get_unaligned_le16(&buf[29]);
	acc_z_plus       = get_unaligned_le16(&buf[31]);
	acc_z_minus      = get_unaligned_le16(&buf[33]);

	/*
	 * Set gyroscope calibration and normalization parameters.
	 * Data values will be normalized to 1/DS_GYRO_RES_PER_DEG_S degree/s.
	 */
	speed_2x = (gyro_speed_plus + gyro_speed_minus);
	ds->gyro_calib_data[0].abs_code = ABS_RX;
	ds->gyro_calib_data[0].bias = gyro_pitch_bias;
	ds->gyro_calib_data[0].sens_numer = speed_2x*DS_GYRO_RES_PER_DEG_S;
	ds->gyro_calib_data[0].sens_denom = gyro_pitch_plus - gyro_pitch_minus;

	ds->gyro_calib_data[1].abs_code = ABS_RY;
	ds->gyro_calib_data[1].bias = gyro_yaw_bias;
	ds->gyro_calib_data[1].sens_numer = speed_2x*DS_GYRO_RES_PER_DEG_S;
	ds->gyro_calib_data[1].sens_denom = gyro_yaw_plus - gyro_yaw_minus;

	ds->gyro_calib_data[2].abs_code = ABS_RZ;
	ds->gyro_calib_data[2].bias = gyro_roll_bias;
	ds->gyro_calib_data[2].sens_numer = speed_2x*DS_GYRO_RES_PER_DEG_S;
	ds->gyro_calib_data[2].sens_denom = gyro_roll_plus - gyro_roll_minus;

	/*
	 * Set accelerometer calibration and normalization parameters.
	 * Data values will be normalized to 1/DS_ACC_RES_PER_G g.
	 */
	range_2g = acc_x_plus - acc_x_minus;
	ds->accel_calib_data[0].abs_code = ABS_X;
	ds->accel_calib_data[0].bias = acc_x_plus - range_2g / 2;
	ds->accel_calib_data[0].sens_numer = 2*DS_ACC_RES_PER_G;
	ds->accel_calib_data[0].sens_denom = range_2g;

	range_2g = acc_y_plus - acc_y_minus;
	ds->accel_calib_data[1].abs_code = ABS_Y;
	ds->accel_calib_data[1].bias = acc_y_plus - range_2g / 2;
	ds->accel_calib_data[1].sens_numer = 2*DS_ACC_RES_PER_G;
	ds->accel_calib_data[1].sens_denom = range_2g;

	range_2g = acc_z_plus - acc_z_minus;
	ds->accel_calib_data[2].abs_code = ABS_Z;
	ds->accel_calib_data[2].bias = acc_z_plus - range_2g / 2;
	ds->accel_calib_data[2].sens_numer = 2*DS_ACC_RES_PER_G;
	ds->accel_calib_data[2].sens_denom = range_2g;

err_free:
	kfree(buf);
	return ret;
}


static int dualsense_get_firmware_info(struct dualsense *ds)
{
	uint8_t *buf;
	int ret;

	buf = kzalloc(DS_FEATURE_REPORT_FIRMWARE_INFO_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = ps_get_report(ds->base.hdev, DS_FEATURE_REPORT_FIRMWARE_INFO, buf,
			DS_FEATURE_REPORT_FIRMWARE_INFO_SIZE);
	if (ret) {
		hid_err(ds->base.hdev, "Failed to retrieve DualSense firmware info: %d\n", ret);
		goto err_free;
	}

	ds->base.hw_version = get_unaligned_le32(&buf[24]);
	ds->base.fw_version = get_unaligned_le32(&buf[28]);

	/* Update version is some kind of feature version. It is distinct from
	 * the firmware version as there can be many different variations of a
	 * controller over time with the same physical shell, but with different
	 * PCBs and other internal changes. The update version (internal name) is
	 * used as a means to detect what features are available and change behavior.
	 * Note: the version is different between DualSense and DualSense Edge.
	 */
	ds->update_version = get_unaligned_le16(&buf[44]);

err_free:
	kfree(buf);
	return ret;
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

static int dualsense_lightbar_set_brightness(struct led_classdev *cdev,
	enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct dualsense *ds = container_of(mc_cdev, struct dualsense, lightbar);
	uint8_t red, green, blue;

	led_mc_calc_color_components(mc_cdev, brightness);
	red = mc_cdev->subled_info[0].brightness;
	green = mc_cdev->subled_info[1].brightness;
	blue = mc_cdev->subled_info[2].brightness;

	dualsense_set_lightbar(ds, red, green, blue);
	return 0;
}

static enum led_brightness dualsense_player_led_get_brightness(struct led_classdev *led)
{
	struct hid_device *hdev = to_hid_device(led->dev->parent);
	struct dualsense *ds = hid_get_drvdata(hdev);

	return !!(ds->player_leds_state & BIT(led - ds->player_leds));
}

static int dualsense_player_led_set_brightness(struct led_classdev *led, enum led_brightness value)
{
	struct hid_device *hdev = to_hid_device(led->dev->parent);
	struct dualsense *ds = hid_get_drvdata(hdev);
	unsigned long flags;
	unsigned int led_index;

	spin_lock_irqsave(&ds->base.lock, flags);

	led_index = led - ds->player_leds;
	if (value == LED_OFF)
		ds->player_leds_state &= ~BIT(led_index);
	else
		ds->player_leds_state |= BIT(led_index);

	ds->update_player_leds = true;
	spin_unlock_irqrestore(&ds->base.lock, flags);

	dualsense_schedule_work(ds);

	return 0;
}

static void dualsense_init_output_report(struct dualsense *ds, struct dualsense_output_report *rp,
		void *buf)
{
	struct hid_device *hdev = ds->base.hdev;

	if (hdev->bus == BUS_BLUETOOTH) {
		struct dualsense_output_report_bt *bt = buf;

		memset(bt, 0, sizeof(*bt));
		bt->report_id = DS_OUTPUT_REPORT_BT;
		bt->tag = DS_OUTPUT_TAG; /* Tag must be set. Exact meaning is unclear. */

		/*
		 * Highest 4-bit is a sequence number, which needs to be increased
		 * every report. Lowest 4-bit is tag and can be zero for now.
		 */
		bt->seq_tag = (ds->output_seq << 4) | 0x0;
		if (++ds->output_seq == 16)
			ds->output_seq = 0;

		rp->data = buf;
		rp->len = sizeof(*bt);
		rp->bt = bt;
		rp->usb = NULL;
		rp->common = &bt->common;
	} else { /* USB */
		struct dualsense_output_report_usb *usb = buf;

		memset(usb, 0, sizeof(*usb));
		usb->report_id = DS_OUTPUT_REPORT_USB;

		rp->data = buf;
		rp->len = sizeof(*usb);
		rp->bt = NULL;
		rp->usb = usb;
		rp->common = &usb->common;
	}
}

static inline void dualsense_schedule_work(struct dualsense *ds)
{
	unsigned long flags;

	spin_lock_irqsave(&ds->base.lock, flags);
	if (ds->output_worker_initialized)
		schedule_work(&ds->output_worker);
	spin_unlock_irqrestore(&ds->base.lock, flags);
}

/*
 * Helper function to send DualSense output reports. Applies a CRC at the end of a report
 * for Bluetooth reports.
 */
static void dualsense_send_output_report(struct dualsense *ds,
		struct dualsense_output_report *report)
{
	struct hid_device *hdev = ds->base.hdev;

	/* Bluetooth packets need to be signed with a CRC in the last 4 bytes. */
	if (report->bt) {
		uint32_t crc;
		uint8_t seed = PS_OUTPUT_CRC32_SEED;

		crc = crc32_le(0xFFFFFFFF, &seed, 1);
		crc = ~crc32_le(crc, report->data, report->len - 4);

		report->bt->crc32 = cpu_to_le32(crc);
	}

	hid_hw_output_report(hdev, report->data, report->len);
}

static void dualsense_output_worker(struct work_struct *work)
{
	struct dualsense *ds = container_of(work, struct dualsense, output_worker);
	struct dualsense_output_report report;
	struct dualsense_output_report_common *common;
	unsigned long flags;

	dualsense_init_output_report(ds, &report, ds->output_report_dmabuf);
	common = report.common;

	spin_lock_irqsave(&ds->base.lock, flags);

	if (ds->update_rumble) {
		/* Select classic rumble style haptics and enable it. */
		common->valid_flag0 |= DS_OUTPUT_VALID_FLAG0_HAPTICS_SELECT;
		if (ds->use_vibration_v2)
			common->valid_flag2 |= DS_OUTPUT_VALID_FLAG2_COMPATIBLE_VIBRATION2;
		else
			common->valid_flag0 |= DS_OUTPUT_VALID_FLAG0_COMPATIBLE_VIBRATION;
		common->motor_left = ds->motor_left;
		common->motor_right = ds->motor_right;
		ds->update_rumble = false;
	}

	if (ds->update_lightbar) {
		common->valid_flag1 |= DS_OUTPUT_VALID_FLAG1_LIGHTBAR_CONTROL_ENABLE;
		common->lightbar_red = ds->lightbar_red;
		common->lightbar_green = ds->lightbar_green;
		common->lightbar_blue = ds->lightbar_blue;

		ds->update_lightbar = false;
	}

	if (ds->update_player_leds) {
		common->valid_flag1 |= DS_OUTPUT_VALID_FLAG1_PLAYER_INDICATOR_CONTROL_ENABLE;
		common->player_leds = ds->player_leds_state;

		ds->update_player_leds = false;
	}

	if (ds->update_mic_mute) {
		common->valid_flag1 |= DS_OUTPUT_VALID_FLAG1_MIC_MUTE_LED_CONTROL_ENABLE;
		common->mute_button_led = ds->mic_muted;

		if (ds->mic_muted) {
			/* Disable microphone */
			common->valid_flag1 |= DS_OUTPUT_VALID_FLAG1_POWER_SAVE_CONTROL_ENABLE;
			common->power_save_control |= DS_OUTPUT_POWER_SAVE_CONTROL_MIC_MUTE;
		} else {
			/* Enable microphone */
			common->valid_flag1 |= DS_OUTPUT_VALID_FLAG1_POWER_SAVE_CONTROL_ENABLE;
			common->power_save_control &= ~DS_OUTPUT_POWER_SAVE_CONTROL_MIC_MUTE;
		}

		ds->update_mic_mute = false;
	}

	spin_unlock_irqrestore(&ds->base.lock, flags);

	dualsense_send_output_report(ds, &report);
}

static int dualsense_parse_report(struct ps_device *ps_dev, struct hid_report *report,
		u8 *data, int size)
{
	struct hid_device *hdev = ps_dev->hdev;
	struct dualsense *ds = container_of(ps_dev, struct dualsense, base);
	struct dualsense_input_report *ds_report;
	uint8_t battery_data, battery_capacity, charging_status, value;
	int battery_status;
	uint32_t sensor_timestamp;
	bool btn_mic_state;
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
	} else if (hdev->bus == BUS_BLUETOOTH && report->id == DS_INPUT_REPORT_BT &&
			size == DS_INPUT_REPORT_BT_SIZE) {
		/* Last 4 bytes of input report contain crc32 */
		uint32_t report_crc = get_unaligned_le32(&data[size - 4]);

		if (!ps_check_crc32(PS_INPUT_CRC32_SEED, data, size - 4, report_crc)) {
			hid_err(hdev, "DualSense input CRC's check failed\n");
			return -EILSEQ;
		}

		ds_report = (struct dualsense_input_report *)&data[2];
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
	if (value >= ARRAY_SIZE(ps_gamepad_hat_mapping))
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

	/*
	 * The DualSense has an internal microphone, which can be muted through a mute button
	 * on the device. The driver is expected to read the button state and program the device
	 * to mute/unmute audio at the hardware level.
	 */
	btn_mic_state = !!(ds_report->buttons[2] & DS_BUTTONS2_MIC_MUTE);
	if (btn_mic_state && !ds->last_btn_mic_state) {
		spin_lock_irqsave(&ps_dev->lock, flags);
		ds->update_mic_mute = true;
		ds->mic_muted = !ds->mic_muted; /* toggle */
		spin_unlock_irqrestore(&ps_dev->lock, flags);

		/* Schedule updating of microphone state at hardware level. */
		dualsense_schedule_work(ds);
	}
	ds->last_btn_mic_state = btn_mic_state;

	/* Parse and calibrate gyroscope data. */
	for (i = 0; i < ARRAY_SIZE(ds_report->gyro); i++) {
		int raw_data = (short)le16_to_cpu(ds_report->gyro[i]);
		int calib_data = mult_frac(ds->gyro_calib_data[i].sens_numer,
					   raw_data - ds->gyro_calib_data[i].bias,
					   ds->gyro_calib_data[i].sens_denom);

		input_report_abs(ds->sensors, ds->gyro_calib_data[i].abs_code, calib_data);
	}

	/* Parse and calibrate accelerometer data. */
	for (i = 0; i < ARRAY_SIZE(ds_report->accel); i++) {
		int raw_data = (short)le16_to_cpu(ds_report->accel[i]);
		int calib_data = mult_frac(ds->accel_calib_data[i].sens_numer,
					   raw_data - ds->accel_calib_data[i].bias,
					   ds->accel_calib_data[i].sens_denom);

		input_report_abs(ds->sensors, ds->accel_calib_data[i].abs_code, calib_data);
	}

	/* Convert timestamp (in 0.33us unit) to timestamp_us */
	sensor_timestamp = le32_to_cpu(ds_report->sensor_timestamp);
	if (!ds->sensor_timestamp_initialized) {
		ds->sensor_timestamp_us = DIV_ROUND_CLOSEST(sensor_timestamp, 3);
		ds->sensor_timestamp_initialized = true;
	} else {
		uint32_t delta;

		if (ds->prev_sensor_timestamp > sensor_timestamp)
			delta = (U32_MAX - ds->prev_sensor_timestamp + sensor_timestamp + 1);
		else
			delta = sensor_timestamp - ds->prev_sensor_timestamp;
		ds->sensor_timestamp_us += DIV_ROUND_CLOSEST(delta, 3);
	}
	ds->prev_sensor_timestamp = sensor_timestamp;
	input_event(ds->sensors, EV_MSC, MSC_TIMESTAMP, ds->sensor_timestamp_us);
	input_sync(ds->sensors);

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

static int dualsense_play_effect(struct input_dev *dev, void *data, struct ff_effect *effect)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct dualsense *ds = hid_get_drvdata(hdev);
	unsigned long flags;

	if (effect->type != FF_RUMBLE)
		return 0;

	spin_lock_irqsave(&ds->base.lock, flags);
	ds->update_rumble = true;
	ds->motor_left = effect->u.rumble.strong_magnitude / 256;
	ds->motor_right = effect->u.rumble.weak_magnitude / 256;
	spin_unlock_irqrestore(&ds->base.lock, flags);

	dualsense_schedule_work(ds);
	return 0;
}

static void dualsense_remove(struct ps_device *ps_dev)
{
	struct dualsense *ds = container_of(ps_dev, struct dualsense, base);
	unsigned long flags;

	spin_lock_irqsave(&ds->base.lock, flags);
	ds->output_worker_initialized = false;
	spin_unlock_irqrestore(&ds->base.lock, flags);

	cancel_work_sync(&ds->output_worker);
}

static int dualsense_reset_leds(struct dualsense *ds)
{
	struct dualsense_output_report report;
	uint8_t *buf;

	buf = kzalloc(sizeof(struct dualsense_output_report_bt), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	dualsense_init_output_report(ds, &report, buf);
	/*
	 * On Bluetooth the DualSense outputs an animation on the lightbar
	 * during startup and maintains a color afterwards. We need to explicitly
	 * reconfigure the lightbar before we can do any programming later on.
	 * In USB the lightbar is not on by default, but redoing the setup there
	 * doesn't hurt.
	 */
	report.common->valid_flag2 = DS_OUTPUT_VALID_FLAG2_LIGHTBAR_SETUP_CONTROL_ENABLE;
	report.common->lightbar_setup = DS_OUTPUT_LIGHTBAR_SETUP_LIGHT_OUT; /* Fade light out. */
	dualsense_send_output_report(ds, &report);

	kfree(buf);
	return 0;
}

static void dualsense_set_lightbar(struct dualsense *ds, uint8_t red, uint8_t green, uint8_t blue)
{
	unsigned long flags;

	spin_lock_irqsave(&ds->base.lock, flags);
	ds->update_lightbar = true;
	ds->lightbar_red = red;
	ds->lightbar_green = green;
	ds->lightbar_blue = blue;
	spin_unlock_irqrestore(&ds->base.lock, flags);

	dualsense_schedule_work(ds);
}

static void dualsense_set_player_leds(struct dualsense *ds)
{
	/*
	 * The DualSense controller has a row of 5 LEDs used for player ids.
	 * Behavior on the PlayStation 5 console is to center the player id
	 * across the LEDs, so e.g. player 1 would be "--x--" with x being 'on'.
	 * Follow a similar mapping here.
	 */
	static const int player_ids[5] = {
		BIT(2),
		BIT(3) | BIT(1),
		BIT(4) | BIT(2) | BIT(0),
		BIT(4) | BIT(3) | BIT(1) | BIT(0),
		BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0)
	};

	uint8_t player_id = ds->base.player_id % ARRAY_SIZE(player_ids);

	ds->update_player_leds = true;
	ds->player_leds_state = player_ids[player_id];
	dualsense_schedule_work(ds);
}

static struct ps_device *dualsense_create(struct hid_device *hdev)
{
	struct dualsense *ds;
	struct ps_device *ps_dev;
	uint8_t max_output_report_size;
	int i, ret;

	static const struct ps_led_info player_leds_info[] = {
		{ LED_FUNCTION_PLAYER1, "white", dualsense_player_led_get_brightness,
				dualsense_player_led_set_brightness },
		{ LED_FUNCTION_PLAYER2, "white", dualsense_player_led_get_brightness,
				dualsense_player_led_set_brightness },
		{ LED_FUNCTION_PLAYER3, "white", dualsense_player_led_get_brightness,
				dualsense_player_led_set_brightness },
		{ LED_FUNCTION_PLAYER4, "white", dualsense_player_led_get_brightness,
				dualsense_player_led_set_brightness },
		{ LED_FUNCTION_PLAYER5, "white", dualsense_player_led_get_brightness,
				dualsense_player_led_set_brightness }
	};

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
	ps_dev->remove = dualsense_remove;
	INIT_WORK(&ds->output_worker, dualsense_output_worker);
	ds->output_worker_initialized = true;
	hid_set_drvdata(hdev, ds);

	max_output_report_size = sizeof(struct dualsense_output_report_bt);
	ds->output_report_dmabuf = devm_kzalloc(&hdev->dev, max_output_report_size, GFP_KERNEL);
	if (!ds->output_report_dmabuf)
		return ERR_PTR(-ENOMEM);

	ret = dualsense_get_mac_address(ds);
	if (ret) {
		hid_err(hdev, "Failed to get MAC address from DualSense\n");
		return ERR_PTR(ret);
	}
	snprintf(hdev->uniq, sizeof(hdev->uniq), "%pMR", ds->base.mac_address);

	ret = dualsense_get_firmware_info(ds);
	if (ret) {
		hid_err(hdev, "Failed to get firmware info from DualSense\n");
		return ERR_PTR(ret);
	}

	/* Original DualSense firmware simulated classic controller rumble through
	 * its new haptics hardware. It felt different from classic rumble users
	 * were used to. Since then new firmwares were introduced to change behavior
	 * and make this new 'v2' behavior default on PlayStation and other platforms.
	 * The original DualSense requires a new enough firmware as bundled with PS5
	 * software released in 2021. DualSense edge supports it out of the box.
	 * Both devices also support the old mode, but it is not really used.
	 */
	if (hdev->product == USB_DEVICE_ID_SONY_PS5_CONTROLLER) {
		/* Feature version 2.21 introduced new vibration method. */
		ds->use_vibration_v2 = ds->update_version >= DS_FEATURE_VERSION(2, 21);
	} else if (hdev->product == USB_DEVICE_ID_SONY_PS5_CONTROLLER_2) {
		ds->use_vibration_v2 = true;
	}

	ret = ps_devices_list_add(ps_dev);
	if (ret)
		return ERR_PTR(ret);

	ret = dualsense_get_calibration_data(ds);
	if (ret) {
		hid_err(hdev, "Failed to get calibration data from DualSense\n");
		goto err;
	}

	ds->gamepad = ps_gamepad_create(hdev, dualsense_play_effect);
	if (IS_ERR(ds->gamepad)) {
		ret = PTR_ERR(ds->gamepad);
		goto err;
	}
	/* Use gamepad input device name as primary device name for e.g. LEDs */
	ps_dev->input_dev_name = dev_name(&ds->gamepad->dev);

	ds->sensors = ps_sensors_create(hdev, DS_ACC_RANGE, DS_ACC_RES_PER_G,
			DS_GYRO_RANGE, DS_GYRO_RES_PER_DEG_S);
	if (IS_ERR(ds->sensors)) {
		ret = PTR_ERR(ds->sensors);
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

	/*
	 * The hardware may have control over the LEDs (e.g. in Bluetooth on startup).
	 * Reset the LEDs (lightbar, mute, player leds), so we can control them
	 * from software.
	 */
	ret = dualsense_reset_leds(ds);
	if (ret)
		goto err;

	ret = ps_lightbar_register(ps_dev, &ds->lightbar, dualsense_lightbar_set_brightness);
	if (ret)
		goto err;

	/* Set default lightbar color. */
	dualsense_set_lightbar(ds, 0, 0, 128); /* blue */

	for (i = 0; i < ARRAY_SIZE(player_leds_info); i++) {
		const struct ps_led_info *led_info = &player_leds_info[i];

		ret = ps_led_register(ps_dev, &ds->player_leds[i], led_info);
		if (ret < 0)
			goto err;
	}

	ret = ps_device_set_player_id(ps_dev);
	if (ret) {
		hid_err(hdev, "Failed to assign player id for DualSense: %d\n", ret);
		goto err;
	}

	/* Set player LEDs to our player id. */
	dualsense_set_player_leds(ds);

	/*
	 * Reporting hardware and firmware is important as there are frequent updates, which
	 * can change behavior.
	 */
	hid_info(hdev, "Registered DualSense controller hw_version=0x%08x fw_version=0x%08x\n",
			ds->base.hw_version, ds->base.fw_version);

	return &ds->base;

err:
	ps_devices_list_remove(ps_dev);
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

	if (hdev->product == USB_DEVICE_ID_SONY_PS5_CONTROLLER ||
		hdev->product == USB_DEVICE_ID_SONY_PS5_CONTROLLER_2) {
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
	struct ps_device *dev = hid_get_drvdata(hdev);

	ps_devices_list_remove(dev);
	ps_device_release_player_id(dev);

	if (dev->remove)
		dev->remove(dev);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id ps_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS5_CONTROLLER) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS5_CONTROLLER) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS5_CONTROLLER_2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS5_CONTROLLER_2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ps_devices);

static struct hid_driver ps_driver = {
	.name		= "playstation",
	.id_table	= ps_devices,
	.probe		= ps_probe,
	.remove		= ps_remove,
	.raw_event	= ps_raw_event,
	.driver = {
		.dev_groups = ps_device_groups,
	},
};

static int __init ps_init(void)
{
	return hid_register_driver(&ps_driver);
}

static void __exit ps_exit(void)
{
	hid_unregister_driver(&ps_driver);
	ida_destroy(&ps_player_id_allocator);
}

module_init(ps_init);
module_exit(ps_exit);

MODULE_AUTHOR("Sony Interactive Entertainment");
MODULE_DESCRIPTION("HID Driver for PlayStation peripherals.");
MODULE_LICENSE("GPL");

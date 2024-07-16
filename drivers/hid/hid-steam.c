// SPDX-License-Identifier: GPL-2.0+
/*
 * HID driver for Valve Steam Controller
 *
 * Copyright (c) 2018 Rodrigo Rivas Costa <rodrigorivascosta@gmail.com>
 * Copyright (c) 2022 Valve Software
 *
 * Supports both the wired and wireless interfaces.
 *
 * This controller has a builtin emulation of mouse and keyboard: the right pad
 * can be used as a mouse, the shoulder buttons are mouse buttons, A and B
 * buttons are ENTER and ESCAPE, and so on. This is implemented as additional
 * HID interfaces.
 *
 * This is known as the "lizard mode", because apparently lizards like to use
 * the computer from the coach, without a proper mouse and keyboard.
 *
 * This driver will disable the lizard mode when the input device is opened
 * and re-enable it when the input device is closed, so as not to break user
 * mode behaviour. The lizard_mode parameter can be used to change that.
 *
 * There are a few user space applications (notably Steam Client) that use
 * the hidraw interface directly to create input devices (XTest, uinput...).
 * In order to avoid breaking them this driver creates a layered hidraw device,
 * so it can detect when the client is running and then:
 *  - it will not send any command to the controller.
 *  - this input device will be removed, to avoid double input of the same
 *    user action.
 * When the client is closed, this input device will be created again.
 *
 * For additional functions, such as changing the right-pad margin or switching
 * the led, you can use the user-space tool at:
 *
 *   https://github.com/rodrigorc/steamctrl
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include "hid-ids.h"

MODULE_DESCRIPTION("HID driver for Valve Steam Controller");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rodrigo Rivas Costa <rodrigorivascosta@gmail.com>");

static bool lizard_mode = true;

static DEFINE_MUTEX(steam_devices_lock);
static LIST_HEAD(steam_devices);

#define STEAM_QUIRK_WIRELESS		BIT(0)
#define STEAM_QUIRK_DECK		BIT(1)

/* Touch pads are 40 mm in diameter and 65535 units */
#define STEAM_PAD_RESOLUTION 1638
/* Trigger runs are about 5 mm and 256 units */
#define STEAM_TRIGGER_RESOLUTION 51
/* Joystick runs are about 5 mm and 256 units */
#define STEAM_JOYSTICK_RESOLUTION 51
/* Trigger runs are about 6 mm and 32768 units */
#define STEAM_DECK_TRIGGER_RESOLUTION 5461
/* Joystick runs are about 5 mm and 32768 units */
#define STEAM_DECK_JOYSTICK_RESOLUTION 6553
/* Accelerometer has 16 bit resolution and a range of +/- 2g */
#define STEAM_DECK_ACCEL_RES_PER_G 16384
#define STEAM_DECK_ACCEL_RANGE 32768
#define STEAM_DECK_ACCEL_FUZZ 32
/* Gyroscope has 16 bit resolution and a range of +/- 2000 dps */
#define STEAM_DECK_GYRO_RES_PER_DPS 16
#define STEAM_DECK_GYRO_RANGE 32768
#define STEAM_DECK_GYRO_FUZZ 1

#define STEAM_PAD_FUZZ 256

/*
 * Commands that can be sent in a feature report.
 * Thanks to Valve and SDL for the names.
 */
enum {
	ID_SET_DIGITAL_MAPPINGS		= 0x80,
	ID_CLEAR_DIGITAL_MAPPINGS	= 0x81,
	ID_GET_DIGITAL_MAPPINGS		= 0x82,
	ID_GET_ATTRIBUTES_VALUES	= 0x83,
	ID_GET_ATTRIBUTE_LABEL		= 0x84,
	ID_SET_DEFAULT_DIGITAL_MAPPINGS	= 0x85,
	ID_FACTORY_RESET		= 0x86,
	ID_SET_SETTINGS_VALUES		= 0x87,
	ID_CLEAR_SETTINGS_VALUES	= 0x88,
	ID_GET_SETTINGS_VALUES		= 0x89,
	ID_GET_SETTING_LABEL		= 0x8A,
	ID_GET_SETTINGS_MAXS		= 0x8B,
	ID_GET_SETTINGS_DEFAULTS	= 0x8C,
	ID_SET_CONTROLLER_MODE		= 0x8D,
	ID_LOAD_DEFAULT_SETTINGS	= 0x8E,
	ID_TRIGGER_HAPTIC_PULSE		= 0x8F,
	ID_TURN_OFF_CONTROLLER		= 0x9F,

	ID_GET_DEVICE_INFO		= 0xA1,

	ID_CALIBRATE_TRACKPADS		= 0xA7,
	ID_RESERVED_0			= 0xA8,
	ID_SET_SERIAL_NUMBER		= 0xA9,
	ID_GET_TRACKPAD_CALIBRATION	= 0xAA,
	ID_GET_TRACKPAD_FACTORY_CALIBRATION = 0xAB,
	ID_GET_TRACKPAD_RAW_DATA	= 0xAC,
	ID_ENABLE_PAIRING		= 0xAD,
	ID_GET_STRING_ATTRIBUTE		= 0xAE,
	ID_RADIO_ERASE_RECORDS		= 0xAF,
	ID_RADIO_WRITE_RECORD		= 0xB0,
	ID_SET_DONGLE_SETTING		= 0xB1,
	ID_DONGLE_DISCONNECT_DEVICE	= 0xB2,
	ID_DONGLE_COMMIT_DEVICE		= 0xB3,
	ID_DONGLE_GET_WIRELESS_STATE	= 0xB4,
	ID_CALIBRATE_GYRO		= 0xB5,
	ID_PLAY_AUDIO			= 0xB6,
	ID_AUDIO_UPDATE_START		= 0xB7,
	ID_AUDIO_UPDATE_DATA		= 0xB8,
	ID_AUDIO_UPDATE_COMPLETE	= 0xB9,
	ID_GET_CHIPID			= 0xBA,

	ID_CALIBRATE_JOYSTICK		= 0xBF,
	ID_CALIBRATE_ANALOG_TRIGGERS	= 0xC0,
	ID_SET_AUDIO_MAPPING		= 0xC1,
	ID_CHECK_GYRO_FW_LOAD		= 0xC2,
	ID_CALIBRATE_ANALOG		= 0xC3,
	ID_DONGLE_GET_CONNECTED_SLOTS	= 0xC4,

	ID_RESET_IMU			= 0xCE,

	ID_TRIGGER_HAPTIC_CMD		= 0xEA,
	ID_TRIGGER_RUMBLE_CMD		= 0xEB,
};

/* Settings IDs */
enum {
	/* 0 */
	SETTING_MOUSE_SENSITIVITY,
	SETTING_MOUSE_ACCELERATION,
	SETTING_TRACKBALL_ROTATION_ANGLE,
	SETTING_HAPTIC_INTENSITY_UNUSED,
	SETTING_LEFT_GAMEPAD_STICK_ENABLED,
	SETTING_RIGHT_GAMEPAD_STICK_ENABLED,
	SETTING_USB_DEBUG_MODE,
	SETTING_LEFT_TRACKPAD_MODE,
	SETTING_RIGHT_TRACKPAD_MODE,
	SETTING_MOUSE_POINTER_ENABLED,

	/* 10 */
	SETTING_DPAD_DEADZONE,
	SETTING_MINIMUM_MOMENTUM_VEL,
	SETTING_MOMENTUM_DECAY_AMMOUNT,
	SETTING_TRACKPAD_RELATIVE_MODE_TICKS_PER_PIXEL,
	SETTING_HAPTIC_INCREMENT,
	SETTING_DPAD_ANGLE_SIN,
	SETTING_DPAD_ANGLE_COS,
	SETTING_MOMENTUM_VERTICAL_DIVISOR,
	SETTING_MOMENTUM_MAXIMUM_VELOCITY,
	SETTING_TRACKPAD_Z_ON,

	/* 20 */
	SETTING_TRACKPAD_Z_OFF,
	SETTING_SENSITIVY_SCALE_AMMOUNT,
	SETTING_LEFT_TRACKPAD_SECONDARY_MODE,
	SETTING_RIGHT_TRACKPAD_SECONDARY_MODE,
	SETTING_SMOOTH_ABSOLUTE_MOUSE,
	SETTING_STEAMBUTTON_POWEROFF_TIME,
	SETTING_UNUSED_1,
	SETTING_TRACKPAD_OUTER_RADIUS,
	SETTING_TRACKPAD_Z_ON_LEFT,
	SETTING_TRACKPAD_Z_OFF_LEFT,

	/* 30 */
	SETTING_TRACKPAD_OUTER_SPIN_VEL,
	SETTING_TRACKPAD_OUTER_SPIN_RADIUS,
	SETTING_TRACKPAD_OUTER_SPIN_HORIZONTAL_ONLY,
	SETTING_TRACKPAD_RELATIVE_MODE_DEADZONE,
	SETTING_TRACKPAD_RELATIVE_MODE_MAX_VEL,
	SETTING_TRACKPAD_RELATIVE_MODE_INVERT_Y,
	SETTING_TRACKPAD_DOUBLE_TAP_BEEP_ENABLED,
	SETTING_TRACKPAD_DOUBLE_TAP_BEEP_PERIOD,
	SETTING_TRACKPAD_DOUBLE_TAP_BEEP_COUNT,
	SETTING_TRACKPAD_OUTER_RADIUS_RELEASE_ON_TRANSITION,

	/* 40 */
	SETTING_RADIAL_MODE_ANGLE,
	SETTING_HAPTIC_INTENSITY_MOUSE_MODE,
	SETTING_LEFT_DPAD_REQUIRES_CLICK,
	SETTING_RIGHT_DPAD_REQUIRES_CLICK,
	SETTING_LED_BASELINE_BRIGHTNESS,
	SETTING_LED_USER_BRIGHTNESS,
	SETTING_ENABLE_RAW_JOYSTICK,
	SETTING_ENABLE_FAST_SCAN,
	SETTING_IMU_MODE,
	SETTING_WIRELESS_PACKET_VERSION,

	/* 50 */
	SETTING_SLEEP_INACTIVITY_TIMEOUT,
	SETTING_TRACKPAD_NOISE_THRESHOLD,
	SETTING_LEFT_TRACKPAD_CLICK_PRESSURE,
	SETTING_RIGHT_TRACKPAD_CLICK_PRESSURE,
	SETTING_LEFT_BUMPER_CLICK_PRESSURE,
	SETTING_RIGHT_BUMPER_CLICK_PRESSURE,
	SETTING_LEFT_GRIP_CLICK_PRESSURE,
	SETTING_RIGHT_GRIP_CLICK_PRESSURE,
	SETTING_LEFT_GRIP2_CLICK_PRESSURE,
	SETTING_RIGHT_GRIP2_CLICK_PRESSURE,

	/* 60 */
	SETTING_PRESSURE_MODE,
	SETTING_CONTROLLER_TEST_MODE,
	SETTING_TRIGGER_MODE,
	SETTING_TRACKPAD_Z_THRESHOLD,
	SETTING_FRAME_RATE,
	SETTING_TRACKPAD_FILT_CTRL,
	SETTING_TRACKPAD_CLIP,
	SETTING_DEBUG_OUTPUT_SELECT,
	SETTING_TRIGGER_THRESHOLD_PERCENT,
	SETTING_TRACKPAD_FREQUENCY_HOPPING,

	/* 70 */
	SETTING_HAPTICS_ENABLED,
	SETTING_STEAM_WATCHDOG_ENABLE,
	SETTING_TIMP_TOUCH_THRESHOLD_ON,
	SETTING_TIMP_TOUCH_THRESHOLD_OFF,
	SETTING_FREQ_HOPPING,
	SETTING_TEST_CONTROL,
	SETTING_HAPTIC_MASTER_GAIN_DB,
	SETTING_THUMB_TOUCH_THRESH,
	SETTING_DEVICE_POWER_STATUS,
	SETTING_HAPTIC_INTENSITY,

	/* 80 */
	SETTING_STABILIZER_ENABLED,
	SETTING_TIMP_MODE_MTE,
};

/* Input report identifiers */
enum
{
	ID_CONTROLLER_STATE = 1,
	ID_CONTROLLER_DEBUG = 2,
	ID_CONTROLLER_WIRELESS = 3,
	ID_CONTROLLER_STATUS = 4,
	ID_CONTROLLER_DEBUG2 = 5,
	ID_CONTROLLER_SECONDARY_STATE = 6,
	ID_CONTROLLER_BLE_STATE = 7,
	ID_CONTROLLER_DECK_STATE = 9
};

/* String attribute idenitifiers */
enum {
	ATTRIB_STR_BOARD_SERIAL,
	ATTRIB_STR_UNIT_SERIAL,
};

/* Values for GYRO_MODE (bitmask) */
enum {
	SETTING_GYRO_MODE_OFF			= 0,
	SETTING_GYRO_MODE_STEERING		= BIT(0),
	SETTING_GYRO_MODE_TILT			= BIT(1),
	SETTING_GYRO_MODE_SEND_ORIENTATION	= BIT(2),
	SETTING_GYRO_MODE_SEND_RAW_ACCEL	= BIT(3),
	SETTING_GYRO_MODE_SEND_RAW_GYRO		= BIT(4),
};

/* Trackpad modes */
enum {
	TRACKPAD_ABSOLUTE_MOUSE,
	TRACKPAD_RELATIVE_MOUSE,
	TRACKPAD_DPAD_FOUR_WAY_DISCRETE,
	TRACKPAD_DPAD_FOUR_WAY_OVERLAP,
	TRACKPAD_DPAD_EIGHT_WAY,
	TRACKPAD_RADIAL_MODE,
	TRACKPAD_ABSOLUTE_DPAD,
	TRACKPAD_NONE,
	TRACKPAD_GESTURE_KEYBOARD,
};

/* Pad identifiers for the deck */
#define STEAM_PAD_LEFT 0
#define STEAM_PAD_RIGHT 1
#define STEAM_PAD_BOTH 2

/* Other random constants */
#define STEAM_SERIAL_LEN 0x15

struct steam_device {
	struct list_head list;
	spinlock_t lock;
	struct hid_device *hdev, *client_hdev;
	struct mutex report_mutex;
	unsigned long client_opened;
	struct input_dev __rcu *input;
	struct input_dev __rcu *sensors;
	unsigned long quirks;
	struct work_struct work_connect;
	bool connected;
	char serial_no[STEAM_SERIAL_LEN + 1];
	struct power_supply_desc battery_desc;
	struct power_supply __rcu *battery;
	u8 battery_charge;
	u16 voltage;
	struct delayed_work mode_switch;
	bool did_mode_switch;
	bool gamepad_mode;
	struct work_struct rumble_work;
	u16 rumble_left;
	u16 rumble_right;
	unsigned int sensor_timestamp_us;
};

static int steam_recv_report(struct steam_device *steam,
		u8 *data, int size)
{
	struct hid_report *r;
	u8 *buf;
	int ret;

	r = steam->hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[0];
	if (!r) {
		hid_err(steam->hdev, "No HID_FEATURE_REPORT submitted -  nothing to read\n");
		return -EINVAL;
	}

	if (hid_report_len(r) < 64)
		return -EINVAL;

	buf = hid_alloc_report_buf(r, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * The report ID is always 0, so strip the first byte from the output.
	 * hid_report_len() is not counting the report ID, so +1 to the length
	 * or else we get a EOVERFLOW. We are safe from a buffer overflow
	 * because hid_alloc_report_buf() allocates +7 bytes.
	 */
	ret = hid_hw_raw_request(steam->hdev, 0x00,
			buf, hid_report_len(r) + 1,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret > 0)
		memcpy(data, buf + 1, min(size, ret - 1));
	kfree(buf);
	return ret;
}

static int steam_send_report(struct steam_device *steam,
		u8 *cmd, int size)
{
	struct hid_report *r;
	u8 *buf;
	unsigned int retries = 50;
	int ret;

	r = steam->hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[0];
	if (!r) {
		hid_err(steam->hdev, "No HID_FEATURE_REPORT submitted -  nothing to read\n");
		return -EINVAL;
	}

	if (hid_report_len(r) < 64)
		return -EINVAL;

	buf = hid_alloc_report_buf(r, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* The report ID is always 0 */
	memcpy(buf + 1, cmd, size);

	/*
	 * Sometimes the wireless controller fails with EPIPE
	 * when sending a feature report.
	 * Doing a HID_REQ_GET_REPORT and waiting for a while
	 * seems to fix that.
	 */
	do {
		ret = hid_hw_raw_request(steam->hdev, 0,
				buf, max(size, 64) + 1,
				HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
		if (ret != -EPIPE)
			break;
		msleep(20);
	} while (--retries);

	kfree(buf);
	if (ret < 0)
		hid_err(steam->hdev, "%s: error %d (%*ph)\n", __func__,
				ret, size, cmd);
	return ret;
}

static inline int steam_send_report_byte(struct steam_device *steam, u8 cmd)
{
	return steam_send_report(steam, &cmd, 1);
}

static int steam_write_settings(struct steam_device *steam,
		/* u8 reg, u16 val */...)
{
	/* Send: 0x87 len (reg valLo valHi)* */
	u8 reg;
	u16 val;
	u8 cmd[64] = {ID_SET_SETTINGS_VALUES, 0x00};
	int ret;
	va_list args;

	va_start(args, steam);
	for (;;) {
		reg = va_arg(args, int);
		if (reg == 0)
			break;
		val = va_arg(args, int);
		cmd[cmd[1] + 2] = reg;
		cmd[cmd[1] + 3] = val & 0xff;
		cmd[cmd[1] + 4] = val >> 8;
		cmd[1] += 3;
	}
	va_end(args);

	ret = steam_send_report(steam, cmd, 2 + cmd[1]);
	if (ret < 0)
		return ret;

	/*
	 * Sometimes a lingering report for this command can
	 * get read back instead of the last set report if
	 * this isn't explicitly queried
	 */
	return steam_recv_report(steam, cmd, 2 + cmd[1]);
}

static int steam_get_serial(struct steam_device *steam)
{
	/*
	 * Send: 0xae 0x15 0x01
	 * Recv: 0xae 0x15 0x01 serialnumber
	 */
	int ret = 0;
	u8 cmd[] = {ID_GET_STRING_ATTRIBUTE, sizeof(steam->serial_no), ATTRIB_STR_UNIT_SERIAL};
	u8 reply[3 + STEAM_SERIAL_LEN + 1];

	mutex_lock(&steam->report_mutex);
	ret = steam_send_report(steam, cmd, sizeof(cmd));
	if (ret < 0)
		goto out;
	ret = steam_recv_report(steam, reply, sizeof(reply));
	if (ret < 0)
		goto out;
	if (reply[0] != ID_GET_STRING_ATTRIBUTE || reply[1] < 1 ||
	    reply[1] > sizeof(steam->serial_no) || reply[2] != ATTRIB_STR_UNIT_SERIAL) {
		ret = -EIO;
		goto out;
	}
	reply[3 + STEAM_SERIAL_LEN] = 0;
	strscpy(steam->serial_no, reply + 3, reply[1]);
out:
	mutex_unlock(&steam->report_mutex);
	return ret;
}

/*
 * This command requests the wireless adaptor to post an event
 * with the connection status. Useful if this driver is loaded when
 * the controller is already connected.
 */
static inline int steam_request_conn_status(struct steam_device *steam)
{
	int ret;
	mutex_lock(&steam->report_mutex);
	ret = steam_send_report_byte(steam, ID_DONGLE_GET_WIRELESS_STATE);
	mutex_unlock(&steam->report_mutex);
	return ret;
}

/*
 * Send a haptic pulse to the trackpads
 * Duration and interval are measured in microseconds, count is the number
 * of pulses to send for duration time with interval microseconds between them
 * and gain is measured in decibels, ranging from -24 to +6
 */
static inline int steam_haptic_pulse(struct steam_device *steam, u8 pad,
				u16 duration, u16 interval, u16 count, u8 gain)
{
	int ret;
	u8 report[10] = {ID_TRIGGER_HAPTIC_PULSE, 8};

	/* Left and right are swapped on this report for legacy reasons */
	if (pad < STEAM_PAD_BOTH)
		pad ^= 1;

	report[2] = pad;
	report[3] = duration & 0xFF;
	report[4] = duration >> 8;
	report[5] = interval & 0xFF;
	report[6] = interval >> 8;
	report[7] = count & 0xFF;
	report[8] = count >> 8;
	report[9] = gain;

	mutex_lock(&steam->report_mutex);
	ret = steam_send_report(steam, report, sizeof(report));
	mutex_unlock(&steam->report_mutex);
	return ret;
}

static inline int steam_haptic_rumble(struct steam_device *steam,
				u16 intensity, u16 left_speed, u16 right_speed,
				u8 left_gain, u8 right_gain)
{
	int ret;
	u8 report[11] = {ID_TRIGGER_RUMBLE_CMD, 9};

	report[3] = intensity & 0xFF;
	report[4] = intensity >> 8;
	report[5] = left_speed & 0xFF;
	report[6] = left_speed >> 8;
	report[7] = right_speed & 0xFF;
	report[8] = right_speed >> 8;
	report[9] = left_gain;
	report[10] = right_gain;

	mutex_lock(&steam->report_mutex);
	ret = steam_send_report(steam, report, sizeof(report));
	mutex_unlock(&steam->report_mutex);
	return ret;
}

static void steam_haptic_rumble_cb(struct work_struct *work)
{
	struct steam_device *steam = container_of(work, struct steam_device,
							rumble_work);
	steam_haptic_rumble(steam, 0, steam->rumble_left,
		steam->rumble_right, 2, 0);
}

#ifdef CONFIG_STEAM_FF
static int steam_play_effect(struct input_dev *dev, void *data,
				struct ff_effect *effect)
{
	struct steam_device *steam = input_get_drvdata(dev);

	steam->rumble_left = effect->u.rumble.strong_magnitude;
	steam->rumble_right = effect->u.rumble.weak_magnitude;

	return schedule_work(&steam->rumble_work);
}
#endif

static void steam_set_lizard_mode(struct steam_device *steam, bool enable)
{
	if (steam->gamepad_mode)
		enable = false;

	if (enable) {
		mutex_lock(&steam->report_mutex);
		/* enable esc, enter, cursors */
		steam_send_report_byte(steam, ID_SET_DEFAULT_DIGITAL_MAPPINGS);
		/* reset settings */
		steam_send_report_byte(steam, ID_LOAD_DEFAULT_SETTINGS);
		mutex_unlock(&steam->report_mutex);
	} else {
		mutex_lock(&steam->report_mutex);
		/* disable esc, enter, cursor */
		steam_send_report_byte(steam, ID_CLEAR_DIGITAL_MAPPINGS);

		if (steam->quirks & STEAM_QUIRK_DECK) {
			steam_write_settings(steam,
				SETTING_LEFT_TRACKPAD_MODE, TRACKPAD_NONE, /* disable mouse */
				SETTING_RIGHT_TRACKPAD_MODE, TRACKPAD_NONE, /* disable mouse */
				SETTING_LEFT_TRACKPAD_CLICK_PRESSURE, 0xFFFF, /* disable haptic click */
				SETTING_RIGHT_TRACKPAD_CLICK_PRESSURE, 0xFFFF, /* disable haptic click */
				SETTING_STEAM_WATCHDOG_ENABLE, 0, /* disable watchdog that tests if Steam is active */
				0);
			mutex_unlock(&steam->report_mutex);
		} else {
			steam_write_settings(steam,
				SETTING_LEFT_TRACKPAD_MODE, TRACKPAD_NONE, /* disable mouse */
				SETTING_RIGHT_TRACKPAD_MODE, TRACKPAD_NONE, /* disable mouse */
				0);
			mutex_unlock(&steam->report_mutex);
		}
	}
}

static int steam_input_open(struct input_dev *dev)
{
	struct steam_device *steam = input_get_drvdata(dev);
	unsigned long flags;
	bool set_lizard_mode;

	/*
	 * Disabling lizard mode automatically is only done on the Steam
	 * Controller. On the Steam Deck, this is toggled manually by holding
	 * the options button instead, handled by steam_mode_switch_cb.
	 */
	if (!(steam->quirks & STEAM_QUIRK_DECK)) {
		spin_lock_irqsave(&steam->lock, flags);
		set_lizard_mode = !steam->client_opened && lizard_mode;
		spin_unlock_irqrestore(&steam->lock, flags);
		if (set_lizard_mode)
			steam_set_lizard_mode(steam, false);
	}

	return 0;
}

static void steam_input_close(struct input_dev *dev)
{
	struct steam_device *steam = input_get_drvdata(dev);
	unsigned long flags;
	bool set_lizard_mode;

	if (!(steam->quirks & STEAM_QUIRK_DECK)) {
		spin_lock_irqsave(&steam->lock, flags);
		set_lizard_mode = !steam->client_opened && lizard_mode;
		spin_unlock_irqrestore(&steam->lock, flags);
		if (set_lizard_mode)
			steam_set_lizard_mode(steam, true);
	}
}

static enum power_supply_property steam_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int steam_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct steam_device *steam = power_supply_get_drvdata(psy);
	unsigned long flags;
	s16 volts;
	u8 batt;
	int ret = 0;

	spin_lock_irqsave(&steam->lock, flags);
	volts = steam->voltage;
	batt = steam->battery_charge;
	spin_unlock_irqrestore(&steam->lock, flags);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = volts * 1000; /* mV -> uV */
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = batt;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int steam_battery_register(struct steam_device *steam)
{
	struct power_supply *battery;
	struct power_supply_config battery_cfg = { .drv_data = steam, };
	unsigned long flags;
	int ret;

	steam->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	steam->battery_desc.properties = steam_battery_props;
	steam->battery_desc.num_properties = ARRAY_SIZE(steam_battery_props);
	steam->battery_desc.get_property = steam_battery_get_property;
	steam->battery_desc.name = devm_kasprintf(&steam->hdev->dev,
			GFP_KERNEL, "steam-controller-%s-battery",
			steam->serial_no);
	if (!steam->battery_desc.name)
		return -ENOMEM;

	/* avoid the warning of 0% battery while waiting for the first info */
	spin_lock_irqsave(&steam->lock, flags);
	steam->voltage = 3000;
	steam->battery_charge = 100;
	spin_unlock_irqrestore(&steam->lock, flags);

	battery = power_supply_register(&steam->hdev->dev,
			&steam->battery_desc, &battery_cfg);
	if (IS_ERR(battery)) {
		ret = PTR_ERR(battery);
		hid_err(steam->hdev,
				"%s:power_supply_register failed with error %d\n",
				__func__, ret);
		return ret;
	}
	rcu_assign_pointer(steam->battery, battery);
	power_supply_powers(battery, &steam->hdev->dev);
	return 0;
}

static int steam_input_register(struct steam_device *steam)
{
	struct hid_device *hdev = steam->hdev;
	struct input_dev *input;
	int ret;

	rcu_read_lock();
	input = rcu_dereference(steam->input);
	rcu_read_unlock();
	if (input) {
		dbg_hid("%s: already connected\n", __func__);
		return 0;
	}

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input_set_drvdata(input, steam);
	input->dev.parent = &hdev->dev;
	input->open = steam_input_open;
	input->close = steam_input_close;

	input->name = (steam->quirks & STEAM_QUIRK_WIRELESS) ? "Wireless Steam Controller" :
		(steam->quirks & STEAM_QUIRK_DECK) ? "Steam Deck" :
		"Steam Controller";
	input->phys = hdev->phys;
	input->uniq = steam->serial_no;
	input->id.bustype = hdev->bus;
	input->id.vendor = hdev->vendor;
	input->id.product = hdev->product;
	input->id.version = hdev->version;

	input_set_capability(input, EV_KEY, BTN_TR2);
	input_set_capability(input, EV_KEY, BTN_TL2);
	input_set_capability(input, EV_KEY, BTN_TR);
	input_set_capability(input, EV_KEY, BTN_TL);
	input_set_capability(input, EV_KEY, BTN_Y);
	input_set_capability(input, EV_KEY, BTN_B);
	input_set_capability(input, EV_KEY, BTN_X);
	input_set_capability(input, EV_KEY, BTN_A);
	input_set_capability(input, EV_KEY, BTN_DPAD_UP);
	input_set_capability(input, EV_KEY, BTN_DPAD_RIGHT);
	input_set_capability(input, EV_KEY, BTN_DPAD_LEFT);
	input_set_capability(input, EV_KEY, BTN_DPAD_DOWN);
	input_set_capability(input, EV_KEY, BTN_SELECT);
	input_set_capability(input, EV_KEY, BTN_MODE);
	input_set_capability(input, EV_KEY, BTN_START);
	input_set_capability(input, EV_KEY, BTN_THUMBR);
	input_set_capability(input, EV_KEY, BTN_THUMBL);
	input_set_capability(input, EV_KEY, BTN_THUMB);
	input_set_capability(input, EV_KEY, BTN_THUMB2);
	if (steam->quirks & STEAM_QUIRK_DECK) {
		input_set_capability(input, EV_KEY, BTN_BASE);
		input_set_capability(input, EV_KEY, BTN_TRIGGER_HAPPY1);
		input_set_capability(input, EV_KEY, BTN_TRIGGER_HAPPY2);
		input_set_capability(input, EV_KEY, BTN_TRIGGER_HAPPY3);
		input_set_capability(input, EV_KEY, BTN_TRIGGER_HAPPY4);
	} else {
		input_set_capability(input, EV_KEY, BTN_GEAR_DOWN);
		input_set_capability(input, EV_KEY, BTN_GEAR_UP);
	}

	input_set_abs_params(input, ABS_X, -32767, 32767, 0, 0);
	input_set_abs_params(input, ABS_Y, -32767, 32767, 0, 0);

	input_set_abs_params(input, ABS_HAT0X, -32767, 32767,
			STEAM_PAD_FUZZ, 0);
	input_set_abs_params(input, ABS_HAT0Y, -32767, 32767,
			STEAM_PAD_FUZZ, 0);

	if (steam->quirks & STEAM_QUIRK_DECK) {
		input_set_abs_params(input, ABS_HAT2Y, 0, 32767, 0, 0);
		input_set_abs_params(input, ABS_HAT2X, 0, 32767, 0, 0);

		input_set_abs_params(input, ABS_RX, -32767, 32767, 0, 0);
		input_set_abs_params(input, ABS_RY, -32767, 32767, 0, 0);

		input_set_abs_params(input, ABS_HAT1X, -32767, 32767,
				STEAM_PAD_FUZZ, 0);
		input_set_abs_params(input, ABS_HAT1Y, -32767, 32767,
				STEAM_PAD_FUZZ, 0);

		input_abs_set_res(input, ABS_X, STEAM_DECK_JOYSTICK_RESOLUTION);
		input_abs_set_res(input, ABS_Y, STEAM_DECK_JOYSTICK_RESOLUTION);
		input_abs_set_res(input, ABS_RX, STEAM_DECK_JOYSTICK_RESOLUTION);
		input_abs_set_res(input, ABS_RY, STEAM_DECK_JOYSTICK_RESOLUTION);
		input_abs_set_res(input, ABS_HAT1X, STEAM_PAD_RESOLUTION);
		input_abs_set_res(input, ABS_HAT1Y, STEAM_PAD_RESOLUTION);
		input_abs_set_res(input, ABS_HAT2Y, STEAM_DECK_TRIGGER_RESOLUTION);
		input_abs_set_res(input, ABS_HAT2X, STEAM_DECK_TRIGGER_RESOLUTION);
	} else {
		input_set_abs_params(input, ABS_HAT2Y, 0, 255, 0, 0);
		input_set_abs_params(input, ABS_HAT2X, 0, 255, 0, 0);

		input_set_abs_params(input, ABS_RX, -32767, 32767,
				STEAM_PAD_FUZZ, 0);
		input_set_abs_params(input, ABS_RY, -32767, 32767,
				STEAM_PAD_FUZZ, 0);

		input_abs_set_res(input, ABS_X, STEAM_JOYSTICK_RESOLUTION);
		input_abs_set_res(input, ABS_Y, STEAM_JOYSTICK_RESOLUTION);
		input_abs_set_res(input, ABS_RX, STEAM_PAD_RESOLUTION);
		input_abs_set_res(input, ABS_RY, STEAM_PAD_RESOLUTION);
		input_abs_set_res(input, ABS_HAT2Y, STEAM_TRIGGER_RESOLUTION);
		input_abs_set_res(input, ABS_HAT2X, STEAM_TRIGGER_RESOLUTION);
	}
	input_abs_set_res(input, ABS_HAT0X, STEAM_PAD_RESOLUTION);
	input_abs_set_res(input, ABS_HAT0Y, STEAM_PAD_RESOLUTION);

#ifdef CONFIG_STEAM_FF
	if (steam->quirks & STEAM_QUIRK_DECK) {
		input_set_capability(input, EV_FF, FF_RUMBLE);
		ret = input_ff_create_memless(input, NULL, steam_play_effect);
		if (ret)
			goto input_register_fail;
	}
#endif

	ret = input_register_device(input);
	if (ret)
		goto input_register_fail;

	rcu_assign_pointer(steam->input, input);
	return 0;

input_register_fail:
	input_free_device(input);
	return ret;
}

static int steam_sensors_register(struct steam_device *steam)
{
	struct hid_device *hdev = steam->hdev;
	struct input_dev *sensors;
	int ret;

	if (!(steam->quirks & STEAM_QUIRK_DECK))
		return 0;

	rcu_read_lock();
	sensors = rcu_dereference(steam->sensors);
	rcu_read_unlock();
	if (sensors) {
		dbg_hid("%s: already connected\n", __func__);
		return 0;
	}

	sensors = input_allocate_device();
	if (!sensors)
		return -ENOMEM;

	input_set_drvdata(sensors, steam);
	sensors->dev.parent = &hdev->dev;

	sensors->name = "Steam Deck Motion Sensors";
	sensors->phys = hdev->phys;
	sensors->uniq = steam->serial_no;
	sensors->id.bustype = hdev->bus;
	sensors->id.vendor = hdev->vendor;
	sensors->id.product = hdev->product;
	sensors->id.version = hdev->version;

	__set_bit(INPUT_PROP_ACCELEROMETER, sensors->propbit);
	__set_bit(EV_MSC, sensors->evbit);
	__set_bit(MSC_TIMESTAMP, sensors->mscbit);

	input_set_abs_params(sensors, ABS_X, -STEAM_DECK_ACCEL_RANGE,
			STEAM_DECK_ACCEL_RANGE, STEAM_DECK_ACCEL_FUZZ, 0);
	input_set_abs_params(sensors, ABS_Y, -STEAM_DECK_ACCEL_RANGE,
			STEAM_DECK_ACCEL_RANGE, STEAM_DECK_ACCEL_FUZZ, 0);
	input_set_abs_params(sensors, ABS_Z, -STEAM_DECK_ACCEL_RANGE,
			STEAM_DECK_ACCEL_RANGE, STEAM_DECK_ACCEL_FUZZ, 0);
	input_abs_set_res(sensors, ABS_X, STEAM_DECK_ACCEL_RES_PER_G);
	input_abs_set_res(sensors, ABS_Y, STEAM_DECK_ACCEL_RES_PER_G);
	input_abs_set_res(sensors, ABS_Z, STEAM_DECK_ACCEL_RES_PER_G);

	input_set_abs_params(sensors, ABS_RX, -STEAM_DECK_GYRO_RANGE,
			STEAM_DECK_GYRO_RANGE, STEAM_DECK_GYRO_FUZZ, 0);
	input_set_abs_params(sensors, ABS_RY, -STEAM_DECK_GYRO_RANGE,
			STEAM_DECK_GYRO_RANGE, STEAM_DECK_GYRO_FUZZ, 0);
	input_set_abs_params(sensors, ABS_RZ, -STEAM_DECK_GYRO_RANGE,
			STEAM_DECK_GYRO_RANGE, STEAM_DECK_GYRO_FUZZ, 0);
	input_abs_set_res(sensors, ABS_RX, STEAM_DECK_GYRO_RES_PER_DPS);
	input_abs_set_res(sensors, ABS_RY, STEAM_DECK_GYRO_RES_PER_DPS);
	input_abs_set_res(sensors, ABS_RZ, STEAM_DECK_GYRO_RES_PER_DPS);

	ret = input_register_device(sensors);
	if (ret)
		goto sensors_register_fail;

	rcu_assign_pointer(steam->sensors, sensors);
	return 0;

sensors_register_fail:
	input_free_device(sensors);
	return ret;
}

static void steam_input_unregister(struct steam_device *steam)
{
	struct input_dev *input;
	rcu_read_lock();
	input = rcu_dereference(steam->input);
	rcu_read_unlock();
	if (!input)
		return;
	RCU_INIT_POINTER(steam->input, NULL);
	synchronize_rcu();
	input_unregister_device(input);
}

static void steam_sensors_unregister(struct steam_device *steam)
{
	struct input_dev *sensors;

	if (!(steam->quirks & STEAM_QUIRK_DECK))
		return;

	rcu_read_lock();
	sensors = rcu_dereference(steam->sensors);
	rcu_read_unlock();

	if (!sensors)
		return;
	RCU_INIT_POINTER(steam->sensors, NULL);
	synchronize_rcu();
	input_unregister_device(sensors);
}

static void steam_battery_unregister(struct steam_device *steam)
{
	struct power_supply *battery;

	rcu_read_lock();
	battery = rcu_dereference(steam->battery);
	rcu_read_unlock();

	if (!battery)
		return;
	RCU_INIT_POINTER(steam->battery, NULL);
	synchronize_rcu();
	power_supply_unregister(battery);
}

static int steam_register(struct steam_device *steam)
{
	int ret;
	unsigned long client_opened;
	unsigned long flags;

	/*
	 * This function can be called several times in a row with the
	 * wireless adaptor, without steam_unregister() between them, because
	 * another client send a get_connection_status command, for example.
	 * The battery and serial number are set just once per device.
	 */
	if (!steam->serial_no[0]) {
		/*
		 * Unlikely, but getting the serial could fail, and it is not so
		 * important, so make up a serial number and go on.
		 */
		if (steam_get_serial(steam) < 0)
			strscpy(steam->serial_no, "XXXXXXXXXX",
					sizeof(steam->serial_no));

		hid_info(steam->hdev, "Steam Controller '%s' connected",
				steam->serial_no);

		/* ignore battery errors, we can live without it */
		if (steam->quirks & STEAM_QUIRK_WIRELESS)
			steam_battery_register(steam);

		mutex_lock(&steam_devices_lock);
		if (list_empty(&steam->list))
			list_add(&steam->list, &steam_devices);
		mutex_unlock(&steam_devices_lock);
	}

	spin_lock_irqsave(&steam->lock, flags);
	client_opened = steam->client_opened;
	spin_unlock_irqrestore(&steam->lock, flags);

	if (!client_opened) {
		steam_set_lizard_mode(steam, lizard_mode);
		ret = steam_input_register(steam);
		if (ret != 0)
			goto steam_register_input_fail;
		ret = steam_sensors_register(steam);
		if (ret != 0)
			goto steam_register_sensors_fail;
	}
	return 0;

steam_register_sensors_fail:
	steam_input_unregister(steam);
steam_register_input_fail:
	return ret;
}

static void steam_unregister(struct steam_device *steam)
{
	steam_battery_unregister(steam);
	steam_sensors_unregister(steam);
	steam_input_unregister(steam);
	if (steam->serial_no[0]) {
		hid_info(steam->hdev, "Steam Controller '%s' disconnected",
				steam->serial_no);
		mutex_lock(&steam_devices_lock);
		list_del_init(&steam->list);
		mutex_unlock(&steam_devices_lock);
		steam->serial_no[0] = 0;
	}
}

static void steam_work_connect_cb(struct work_struct *work)
{
	struct steam_device *steam = container_of(work, struct steam_device,
							work_connect);
	unsigned long flags;
	bool connected;
	int ret;

	spin_lock_irqsave(&steam->lock, flags);
	connected = steam->connected;
	spin_unlock_irqrestore(&steam->lock, flags);

	if (connected) {
		ret = steam_register(steam);
		if (ret) {
			hid_err(steam->hdev,
				"%s:steam_register failed with error %d\n",
				__func__, ret);
		}
	} else {
		steam_unregister(steam);
	}
}

static void steam_mode_switch_cb(struct work_struct *work)
{
	struct steam_device *steam = container_of(to_delayed_work(work),
							struct steam_device, mode_switch);
	unsigned long flags;
	bool client_opened;
	steam->gamepad_mode = !steam->gamepad_mode;
	if (!lizard_mode)
		return;

	if (steam->gamepad_mode)
		steam_set_lizard_mode(steam, false);
	else {
		spin_lock_irqsave(&steam->lock, flags);
		client_opened = steam->client_opened;
		spin_unlock_irqrestore(&steam->lock, flags);
		if (!client_opened)
			steam_set_lizard_mode(steam, lizard_mode);
	}

	steam_haptic_pulse(steam, STEAM_PAD_RIGHT, 0x190, 0, 1, 0);
	if (steam->gamepad_mode) {
		steam_haptic_pulse(steam, STEAM_PAD_LEFT, 0x14D, 0x14D, 0x2D, 0);
	} else {
		steam_haptic_pulse(steam, STEAM_PAD_LEFT, 0x1F4, 0x1F4, 0x1E, 0);
	}
}

static bool steam_is_valve_interface(struct hid_device *hdev)
{
	struct hid_report_enum *rep_enum;

	/*
	 * The wired device creates 3 interfaces:
	 *  0: emulated mouse.
	 *  1: emulated keyboard.
	 *  2: the real game pad.
	 * The wireless device creates 5 interfaces:
	 *  0: emulated keyboard.
	 *  1-4: slots where up to 4 real game pads will be connected to.
	 * We know which one is the real gamepad interface because they are the
	 * only ones with a feature report.
	 */
	rep_enum = &hdev->report_enum[HID_FEATURE_REPORT];
	return !list_empty(&rep_enum->report_list);
}

static int steam_client_ll_parse(struct hid_device *hdev)
{
	struct steam_device *steam = hdev->driver_data;

	return hid_parse_report(hdev, steam->hdev->dev_rdesc,
			steam->hdev->dev_rsize);
}

static int steam_client_ll_start(struct hid_device *hdev)
{
	return 0;
}

static void steam_client_ll_stop(struct hid_device *hdev)
{
}

static int steam_client_ll_open(struct hid_device *hdev)
{
	struct steam_device *steam = hdev->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&steam->lock, flags);
	steam->client_opened++;
	spin_unlock_irqrestore(&steam->lock, flags);

	steam_sensors_unregister(steam);
	steam_input_unregister(steam);

	return 0;
}

static void steam_client_ll_close(struct hid_device *hdev)
{
	struct steam_device *steam = hdev->driver_data;

	unsigned long flags;
	bool connected;

	spin_lock_irqsave(&steam->lock, flags);
	steam->client_opened--;
	connected = steam->connected && !steam->client_opened;
	spin_unlock_irqrestore(&steam->lock, flags);

	if (connected) {
		steam_set_lizard_mode(steam, lizard_mode);
		steam_input_register(steam);
		steam_sensors_register(steam);
	}
}

static int steam_client_ll_raw_request(struct hid_device *hdev,
				unsigned char reportnum, u8 *buf,
				size_t count, unsigned char report_type,
				int reqtype)
{
	struct steam_device *steam = hdev->driver_data;

	return hid_hw_raw_request(steam->hdev, reportnum, buf, count,
			report_type, reqtype);
}

static const struct hid_ll_driver steam_client_ll_driver = {
	.parse = steam_client_ll_parse,
	.start = steam_client_ll_start,
	.stop = steam_client_ll_stop,
	.open = steam_client_ll_open,
	.close = steam_client_ll_close,
	.raw_request = steam_client_ll_raw_request,
};

static struct hid_device *steam_create_client_hid(struct hid_device *hdev)
{
	struct hid_device *client_hdev;

	client_hdev = hid_allocate_device();
	if (IS_ERR(client_hdev))
		return client_hdev;

	client_hdev->ll_driver = &steam_client_ll_driver;
	client_hdev->dev.parent = hdev->dev.parent;
	client_hdev->bus = hdev->bus;
	client_hdev->vendor = hdev->vendor;
	client_hdev->product = hdev->product;
	client_hdev->version = hdev->version;
	client_hdev->type = hdev->type;
	client_hdev->country = hdev->country;
	strscpy(client_hdev->name, hdev->name,
			sizeof(client_hdev->name));
	strscpy(client_hdev->phys, hdev->phys,
			sizeof(client_hdev->phys));
	/*
	 * Since we use the same device info than the real interface to
	 * trick userspace, we will be calling steam_probe recursively.
	 * We need to recognize the client interface somehow.
	 */
	client_hdev->group = HID_GROUP_STEAM;
	return client_hdev;
}

static int steam_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	struct steam_device *steam;
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev,
			"%s:parse of hid interface failed\n", __func__);
		return ret;
	}

	/*
	 * The virtual client_dev is only used for hidraw.
	 * Also avoid the recursive probe.
	 */
	if (hdev->group == HID_GROUP_STEAM)
		return hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	/*
	 * The non-valve interfaces (mouse and keyboard emulation) are
	 * connected without changes.
	 */
	if (!steam_is_valve_interface(hdev))
		return hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	steam = devm_kzalloc(&hdev->dev, sizeof(*steam), GFP_KERNEL);
	if (!steam)
		return -ENOMEM;

	steam->hdev = hdev;
	hid_set_drvdata(hdev, steam);
	spin_lock_init(&steam->lock);
	mutex_init(&steam->report_mutex);
	steam->quirks = id->driver_data;
	INIT_WORK(&steam->work_connect, steam_work_connect_cb);
	INIT_DELAYED_WORK(&steam->mode_switch, steam_mode_switch_cb);
	INIT_LIST_HEAD(&steam->list);
	INIT_WORK(&steam->rumble_work, steam_haptic_rumble_cb);
	steam->sensor_timestamp_us = 0;

	/*
	 * With the real steam controller interface, do not connect hidraw.
	 * Instead, create the client_hid and connect that.
	 */
	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_HIDRAW);
	if (ret)
		goto err_cancel_work;

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev,
			"%s:hid_hw_open\n",
			__func__);
		goto err_hw_stop;
	}

	if (steam->quirks & STEAM_QUIRK_WIRELESS) {
		hid_info(hdev, "Steam wireless receiver connected");
		/* If using a wireless adaptor ask for connection status */
		steam->connected = false;
		steam_request_conn_status(steam);
	} else {
		/* A wired connection is always present */
		steam->connected = true;
		ret = steam_register(steam);
		if (ret) {
			hid_err(hdev,
				"%s:steam_register failed with error %d\n",
				__func__, ret);
			goto err_hw_close;
		}
	}

	steam->client_hdev = steam_create_client_hid(hdev);
	if (IS_ERR(steam->client_hdev)) {
		ret = PTR_ERR(steam->client_hdev);
		goto err_steam_unregister;
	}
	steam->client_hdev->driver_data = steam;

	ret = hid_add_device(steam->client_hdev);
	if (ret)
		goto err_destroy;

	return 0;

err_destroy:
	hid_destroy_device(steam->client_hdev);
err_steam_unregister:
	if (steam->connected)
		steam_unregister(steam);
err_hw_close:
	hid_hw_close(hdev);
err_hw_stop:
	hid_hw_stop(hdev);
err_cancel_work:
	cancel_work_sync(&steam->work_connect);
	cancel_delayed_work_sync(&steam->mode_switch);
	cancel_work_sync(&steam->rumble_work);

	return ret;
}

static void steam_remove(struct hid_device *hdev)
{
	struct steam_device *steam = hid_get_drvdata(hdev);

	if (!steam || hdev->group == HID_GROUP_STEAM) {
		hid_hw_stop(hdev);
		return;
	}

	cancel_delayed_work_sync(&steam->mode_switch);
	cancel_work_sync(&steam->work_connect);
	hid_destroy_device(steam->client_hdev);
	steam->client_hdev = NULL;
	steam->client_opened = 0;
	if (steam->quirks & STEAM_QUIRK_WIRELESS) {
		hid_info(hdev, "Steam wireless receiver disconnected");
	}
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	steam_unregister(steam);
}

static void steam_do_connect_event(struct steam_device *steam, bool connected)
{
	unsigned long flags;
	bool changed;

	spin_lock_irqsave(&steam->lock, flags);
	changed = steam->connected != connected;
	steam->connected = connected;
	spin_unlock_irqrestore(&steam->lock, flags);

	if (changed && schedule_work(&steam->work_connect) == 0)
		dbg_hid("%s: connected=%d event already queued\n",
				__func__, connected);
}

/*
 * Some input data in the protocol has the opposite sign.
 * Clamp the values to 32767..-32767 so that the range is
 * symmetrical and can be negated safely.
 */
static inline s16 steam_le16(u8 *data)
{
	s16 x = (s16) le16_to_cpup((__le16 *)data);

	return x == -32768 ? -32767 : x;
}

/*
 * The size for this message payload is 60.
 * The known values are:
 *  (* values are not sent through wireless)
 *  (* accelerator/gyro is disabled by default)
 *  Offset| Type  | Mapped to |Meaning
 * -------+-------+-----------+--------------------------
 *  4-7   | u32   | --        | sequence number
 *  8-10  | 24bit | see below | buttons
 *  11    | u8    | ABS_HAT2Y | left trigger
 *  12    | u8    | ABS_HAT2X | right trigger
 *  13-15 | --    | --        | always 0
 *  16-17 | s16   | ABS_X/ABS_HAT0X     | X value
 *  18-19 | s16   | ABS_Y/ABS_HAT0Y     | Y value
 *  20-21 | s16   | ABS_RX    | right-pad X value
 *  22-23 | s16   | ABS_RY    | right-pad Y value
 *  24-25 | s16   | --        | * left trigger
 *  26-27 | s16   | --        | * right trigger
 *  28-29 | s16   | --        | * accelerometer X value
 *  30-31 | s16   | --        | * accelerometer Y value
 *  32-33 | s16   | --        | * accelerometer Z value
 *  34-35 | s16   | --        | gyro X value
 *  36-36 | s16   | --        | gyro Y value
 *  38-39 | s16   | --        | gyro Z value
 *  40-41 | s16   | --        | quaternion W value
 *  42-43 | s16   | --        | quaternion X value
 *  44-45 | s16   | --        | quaternion Y value
 *  46-47 | s16   | --        | quaternion Z value
 *  48-49 | --    | --        | always 0
 *  50-51 | s16   | --        | * left trigger (uncalibrated)
 *  52-53 | s16   | --        | * right trigger (uncalibrated)
 *  54-55 | s16   | --        | * joystick X value (uncalibrated)
 *  56-57 | s16   | --        | * joystick Y value (uncalibrated)
 *  58-59 | s16   | --        | * left-pad X value
 *  60-61 | s16   | --        | * left-pad Y value
 *  62-63 | u16   | --        | * battery voltage
 *
 * The buttons are:
 *  Bit  | Mapped to  | Description
 * ------+------------+--------------------------------
 *  8.0  | BTN_TR2    | right trigger fully pressed
 *  8.1  | BTN_TL2    | left trigger fully pressed
 *  8.2  | BTN_TR     | right shoulder
 *  8.3  | BTN_TL     | left shoulder
 *  8.4  | BTN_Y      | button Y
 *  8.5  | BTN_B      | button B
 *  8.6  | BTN_X      | button X
 *  8.7  | BTN_A      | button A
 *  9.0  | BTN_DPAD_UP    | left-pad up
 *  9.1  | BTN_DPAD_RIGHT | left-pad right
 *  9.2  | BTN_DPAD_LEFT  | left-pad left
 *  9.3  | BTN_DPAD_DOWN  | left-pad down
 *  9.4  | BTN_SELECT | menu left
 *  9.5  | BTN_MODE   | steam logo
 *  9.6  | BTN_START  | menu right
 *  9.7  | BTN_GEAR_DOWN | left back lever
 * 10.0  | BTN_GEAR_UP   | right back lever
 * 10.1  | --         | left-pad clicked
 * 10.2  | BTN_THUMBR | right-pad clicked
 * 10.3  | BTN_THUMB  | left-pad touched (but see explanation below)
 * 10.4  | BTN_THUMB2 | right-pad touched
 * 10.5  | --         | unknown
 * 10.6  | BTN_THUMBL | joystick clicked
 * 10.7  | --         | lpad_and_joy
 */

static void steam_do_input_event(struct steam_device *steam,
		struct input_dev *input, u8 *data)
{
	/* 24 bits of buttons */
	u8 b8, b9, b10;
	s16 x, y;
	bool lpad_touched, lpad_and_joy;

	b8 = data[8];
	b9 = data[9];
	b10 = data[10];

	input_report_abs(input, ABS_HAT2Y, data[11]);
	input_report_abs(input, ABS_HAT2X, data[12]);

	/*
	 * These two bits tells how to interpret the values X and Y.
	 * lpad_and_joy tells that the joystick and the lpad are used at the
	 * same time.
	 * lpad_touched tells whether X/Y are to be read as lpad coord or
	 * joystick values.
	 * (lpad_touched || lpad_and_joy) tells if the lpad is really touched.
	 */
	lpad_touched = b10 & BIT(3);
	lpad_and_joy = b10 & BIT(7);
	x = steam_le16(data + 16);
	y = -steam_le16(data + 18);

	input_report_abs(input, lpad_touched ? ABS_HAT0X : ABS_X, x);
	input_report_abs(input, lpad_touched ? ABS_HAT0Y : ABS_Y, y);
	/* Check if joystick is centered */
	if (lpad_touched && !lpad_and_joy) {
		input_report_abs(input, ABS_X, 0);
		input_report_abs(input, ABS_Y, 0);
	}
	/* Check if lpad is untouched */
	if (!(lpad_touched || lpad_and_joy)) {
		input_report_abs(input, ABS_HAT0X, 0);
		input_report_abs(input, ABS_HAT0Y, 0);
	}

	input_report_abs(input, ABS_RX, steam_le16(data + 20));
	input_report_abs(input, ABS_RY, -steam_le16(data + 22));

	input_event(input, EV_KEY, BTN_TR2, !!(b8 & BIT(0)));
	input_event(input, EV_KEY, BTN_TL2, !!(b8 & BIT(1)));
	input_event(input, EV_KEY, BTN_TR, !!(b8 & BIT(2)));
	input_event(input, EV_KEY, BTN_TL, !!(b8 & BIT(3)));
	input_event(input, EV_KEY, BTN_Y, !!(b8 & BIT(4)));
	input_event(input, EV_KEY, BTN_B, !!(b8 & BIT(5)));
	input_event(input, EV_KEY, BTN_X, !!(b8 & BIT(6)));
	input_event(input, EV_KEY, BTN_A, !!(b8 & BIT(7)));
	input_event(input, EV_KEY, BTN_SELECT, !!(b9 & BIT(4)));
	input_event(input, EV_KEY, BTN_MODE, !!(b9 & BIT(5)));
	input_event(input, EV_KEY, BTN_START, !!(b9 & BIT(6)));
	input_event(input, EV_KEY, BTN_GEAR_DOWN, !!(b9 & BIT(7)));
	input_event(input, EV_KEY, BTN_GEAR_UP, !!(b10 & BIT(0)));
	input_event(input, EV_KEY, BTN_THUMBR, !!(b10 & BIT(2)));
	input_event(input, EV_KEY, BTN_THUMBL, !!(b10 & BIT(6)));
	input_event(input, EV_KEY, BTN_THUMB, lpad_touched || lpad_and_joy);
	input_event(input, EV_KEY, BTN_THUMB2, !!(b10 & BIT(4)));
	input_event(input, EV_KEY, BTN_DPAD_UP, !!(b9 & BIT(0)));
	input_event(input, EV_KEY, BTN_DPAD_RIGHT, !!(b9 & BIT(1)));
	input_event(input, EV_KEY, BTN_DPAD_LEFT, !!(b9 & BIT(2)));
	input_event(input, EV_KEY, BTN_DPAD_DOWN, !!(b9 & BIT(3)));

	input_sync(input);
}

/*
 * The size for this message payload is 56.
 * The known values are:
 *  Offset| Type  | Mapped to |Meaning
 * -------+-------+-----------+--------------------------
 *  4-7   | u32   | --        | sequence number
 *  8-15  | u64   | see below | buttons
 *  16-17 | s16   | ABS_HAT0X | left-pad X value
 *  18-19 | s16   | ABS_HAT0Y | left-pad Y value
 *  20-21 | s16   | ABS_HAT1X | right-pad X value
 *  22-23 | s16   | ABS_HAT1Y | right-pad Y value
 *  24-25 | s16   | IMU ABS_X | accelerometer X value
 *  26-27 | s16   | IMU ABS_Z | accelerometer Y value
 *  28-29 | s16   | IMU ABS_Y | accelerometer Z value
 *  30-31 | s16   | IMU ABS_RX | gyro X value
 *  32-33 | s16   | IMU ABS_RZ | gyro Y value
 *  34-35 | s16   | IMU ABS_RY | gyro Z value
 *  36-37 | s16   | --        | quaternion W value
 *  38-39 | s16   | --        | quaternion X value
 *  40-41 | s16   | --        | quaternion Y value
 *  42-43 | s16   | --        | quaternion Z value
 *  44-45 | u16   | ABS_HAT2Y | left trigger (uncalibrated)
 *  46-47 | u16   | ABS_HAT2X | right trigger (uncalibrated)
 *  48-49 | s16   | ABS_X     | left joystick X
 *  50-51 | s16   | ABS_Y     | left joystick Y
 *  52-53 | s16   | ABS_RX    | right joystick X
 *  54-55 | s16   | ABS_RY    | right joystick Y
 *  56-57 | u16   | --        | left pad pressure
 *  58-59 | u16   | --        | right pad pressure
 *
 * The buttons are:
 *  Bit  | Mapped to  | Description
 * ------+------------+--------------------------------
 *  8.0  | BTN_TR2    | right trigger fully pressed
 *  8.1  | BTN_TL2    | left trigger fully pressed
 *  8.2  | BTN_TR     | right shoulder
 *  8.3  | BTN_TL     | left shoulder
 *  8.4  | BTN_Y      | button Y
 *  8.5  | BTN_B      | button B
 *  8.6  | BTN_X      | button X
 *  8.7  | BTN_A      | button A
 *  9.0  | BTN_DPAD_UP    | left-pad up
 *  9.1  | BTN_DPAD_RIGHT | left-pad right
 *  9.2  | BTN_DPAD_LEFT  | left-pad left
 *  9.3  | BTN_DPAD_DOWN  | left-pad down
 *  9.4  | BTN_SELECT | menu left
 *  9.5  | BTN_MODE   | steam logo
 *  9.6  | BTN_START  | menu right
 *  9.7  | BTN_TRIGGER_HAPPY3 | left bottom grip button
 *  10.0 | BTN_TRIGGER_HAPPY4 | right bottom grip button
 *  10.1 | BTN_THUMB  | left pad pressed
 *  10.2 | BTN_THUMB2 | right pad pressed
 *  10.3 | --         | left pad touched
 *  10.4 | --         | right pad touched
 *  10.5 | --         | unknown
 *  10.6 | BTN_THUMBL | left joystick clicked
 *  10.7 | --         | unknown
 *  11.0 | --         | unknown
 *  11.1 | --         | unknown
 *  11.2 | BTN_THUMBR | right joystick clicked
 *  11.3 | --         | unknown
 *  11.4 | --         | unknown
 *  11.5 | --         | unknown
 *  11.6 | --         | unknown
 *  11.7 | --         | unknown
 *  12.0 | --         | unknown
 *  12.1 | --         | unknown
 *  12.2 | --         | unknown
 *  12.3 | --         | unknown
 *  12.4 | --         | unknown
 *  12.5 | --         | unknown
 *  12.6 | --         | unknown
 *  12.7 | --         | unknown
 *  13.0 | --         | unknown
 *  13.1 | BTN_TRIGGER_HAPPY1 | left top grip button
 *  13.2 | BTN_TRIGGER_HAPPY2 | right top grip button
 *  13.3 | --         | unknown
 *  13.4 | --         | unknown
 *  13.5 | --         | unknown
 *  13.6 | --         | left joystick touched
 *  13.7 | --         | right joystick touched
 *  14.0 | --         | unknown
 *  14.1 | --         | unknown
 *  14.2 | BTN_BASE   | quick access button
 *  14.3 | --         | unknown
 *  14.4 | --         | unknown
 *  14.5 | --         | unknown
 *  14.6 | --         | unknown
 *  14.7 | --         | unknown
 *  15.0 | --         | unknown
 *  15.1 | --         | unknown
 *  15.2 | --         | unknown
 *  15.3 | --         | unknown
 *  15.4 | --         | unknown
 *  15.5 | --         | unknown
 *  15.6 | --         | unknown
 *  15.7 | --         | unknown
 */
static void steam_do_deck_input_event(struct steam_device *steam,
		struct input_dev *input, u8 *data)
{
	u8 b8, b9, b10, b11, b13, b14;
	bool lpad_touched, rpad_touched;

	b8 = data[8];
	b9 = data[9];
	b10 = data[10];
	b11 = data[11];
	b13 = data[13];
	b14 = data[14];

	if (!(b9 & BIT(6)) && steam->did_mode_switch) {
		steam->did_mode_switch = false;
		cancel_delayed_work_sync(&steam->mode_switch);
	} else if (!steam->client_opened && (b9 & BIT(6)) && !steam->did_mode_switch) {
		steam->did_mode_switch = true;
		schedule_delayed_work(&steam->mode_switch, 45 * HZ / 100);
	}

	if (!steam->gamepad_mode)
		return;

	lpad_touched = b10 & BIT(3);
	rpad_touched = b10 & BIT(4);

	if (lpad_touched) {
		input_report_abs(input, ABS_HAT0X, steam_le16(data + 16));
		input_report_abs(input, ABS_HAT0Y, steam_le16(data + 18));
	} else {
		input_report_abs(input, ABS_HAT0X, 0);
		input_report_abs(input, ABS_HAT0Y, 0);
	}

	if (rpad_touched) {
		input_report_abs(input, ABS_HAT1X, steam_le16(data + 20));
		input_report_abs(input, ABS_HAT1Y, steam_le16(data + 22));
	} else {
		input_report_abs(input, ABS_HAT1X, 0);
		input_report_abs(input, ABS_HAT1Y, 0);
	}

	input_report_abs(input, ABS_X, steam_le16(data + 48));
	input_report_abs(input, ABS_Y, -steam_le16(data + 50));
	input_report_abs(input, ABS_RX, steam_le16(data + 52));
	input_report_abs(input, ABS_RY, -steam_le16(data + 54));

	input_report_abs(input, ABS_HAT2Y, steam_le16(data + 44));
	input_report_abs(input, ABS_HAT2X, steam_le16(data + 46));

	input_event(input, EV_KEY, BTN_TR2, !!(b8 & BIT(0)));
	input_event(input, EV_KEY, BTN_TL2, !!(b8 & BIT(1)));
	input_event(input, EV_KEY, BTN_TR, !!(b8 & BIT(2)));
	input_event(input, EV_KEY, BTN_TL, !!(b8 & BIT(3)));
	input_event(input, EV_KEY, BTN_Y, !!(b8 & BIT(4)));
	input_event(input, EV_KEY, BTN_B, !!(b8 & BIT(5)));
	input_event(input, EV_KEY, BTN_X, !!(b8 & BIT(6)));
	input_event(input, EV_KEY, BTN_A, !!(b8 & BIT(7)));
	input_event(input, EV_KEY, BTN_SELECT, !!(b9 & BIT(4)));
	input_event(input, EV_KEY, BTN_MODE, !!(b9 & BIT(5)));
	input_event(input, EV_KEY, BTN_START, !!(b9 & BIT(6)));
	input_event(input, EV_KEY, BTN_TRIGGER_HAPPY3, !!(b9 & BIT(7)));
	input_event(input, EV_KEY, BTN_TRIGGER_HAPPY4, !!(b10 & BIT(0)));
	input_event(input, EV_KEY, BTN_THUMBL, !!(b10 & BIT(6)));
	input_event(input, EV_KEY, BTN_THUMBR, !!(b11 & BIT(2)));
	input_event(input, EV_KEY, BTN_DPAD_UP, !!(b9 & BIT(0)));
	input_event(input, EV_KEY, BTN_DPAD_RIGHT, !!(b9 & BIT(1)));
	input_event(input, EV_KEY, BTN_DPAD_LEFT, !!(b9 & BIT(2)));
	input_event(input, EV_KEY, BTN_DPAD_DOWN, !!(b9 & BIT(3)));
	input_event(input, EV_KEY, BTN_THUMB, !!(b10 & BIT(1)));
	input_event(input, EV_KEY, BTN_THUMB2, !!(b10 & BIT(2)));
	input_event(input, EV_KEY, BTN_TRIGGER_HAPPY1, !!(b13 & BIT(1)));
	input_event(input, EV_KEY, BTN_TRIGGER_HAPPY2, !!(b13 & BIT(2)));
	input_event(input, EV_KEY, BTN_BASE, !!(b14 & BIT(2)));

	input_sync(input);
}

static void steam_do_deck_sensors_event(struct steam_device *steam,
		struct input_dev *sensors, u8 *data)
{
	/*
	 * The deck input report is received every 4 ms on average,
	 * with a jitter of +/- 4 ms even though the USB descriptor claims
	 * that it uses 1 kHz.
	 * Since the HID report does not include a sensor timestamp,
	 * use a fixed increment here.
	 */
	steam->sensor_timestamp_us += 4000;

	if (!steam->gamepad_mode)
		return;

	input_event(sensors, EV_MSC, MSC_TIMESTAMP, steam->sensor_timestamp_us);
	input_report_abs(sensors, ABS_X, steam_le16(data + 24));
	input_report_abs(sensors, ABS_Z, -steam_le16(data + 26));
	input_report_abs(sensors, ABS_Y, steam_le16(data + 28));
	input_report_abs(sensors, ABS_RX, steam_le16(data + 30));
	input_report_abs(sensors, ABS_RZ, -steam_le16(data + 32));
	input_report_abs(sensors, ABS_RY, steam_le16(data + 34));

	input_sync(sensors);
}

/*
 * The size for this message payload is 11.
 * The known values are:
 *  Offset| Type  | Meaning
 * -------+-------+---------------------------
 *  4-7   | u32   | sequence number
 *  8-11  | --    | always 0
 *  12-13 | u16   | voltage (mV)
 *  14    | u8    | battery percent
 */
static void steam_do_battery_event(struct steam_device *steam,
		struct power_supply *battery, u8 *data)
{
	unsigned long flags;

	s16 volts = steam_le16(data + 12);
	u8 batt = data[14];

	/* Creating the battery may have failed */
	rcu_read_lock();
	battery = rcu_dereference(steam->battery);
	if (likely(battery)) {
		spin_lock_irqsave(&steam->lock, flags);
		steam->voltage = volts;
		steam->battery_charge = batt;
		spin_unlock_irqrestore(&steam->lock, flags);
		power_supply_changed(battery);
	}
	rcu_read_unlock();
}

static int steam_raw_event(struct hid_device *hdev,
			struct hid_report *report, u8 *data,
			int size)
{
	struct steam_device *steam = hid_get_drvdata(hdev);
	struct input_dev *input;
	struct input_dev *sensors;
	struct power_supply *battery;

	if (!steam)
		return 0;

	if (steam->client_opened)
		hid_input_report(steam->client_hdev, HID_FEATURE_REPORT,
				data, size, 0);
	/*
	 * All messages are size=64, all values little-endian.
	 * The format is:
	 *  Offset| Meaning
	 * -------+--------------------------------------------
	 *  0-1   | always 0x01, 0x00, maybe protocol version?
	 *  2     | type of message
	 *  3     | length of the real payload (not checked)
	 *  4-n   | payload data, depends on the type
	 *
	 * There are these known types of message:
	 *  0x01: input data (60 bytes)
	 *  0x03: wireless connect/disconnect (1 byte)
	 *  0x04: battery status (11 bytes)
	 *  0x09: Steam Deck input data (56 bytes)
	 */

	if (size != 64 || data[0] != 1 || data[1] != 0)
		return 0;

	switch (data[2]) {
	case ID_CONTROLLER_STATE:
		if (steam->client_opened)
			return 0;
		rcu_read_lock();
		input = rcu_dereference(steam->input);
		if (likely(input))
			steam_do_input_event(steam, input, data);
		rcu_read_unlock();
		break;
	case ID_CONTROLLER_DECK_STATE:
		if (steam->client_opened)
			return 0;
		rcu_read_lock();
		input = rcu_dereference(steam->input);
		if (likely(input))
			steam_do_deck_input_event(steam, input, data);
		sensors = rcu_dereference(steam->sensors);
		if (likely(sensors))
			steam_do_deck_sensors_event(steam, sensors, data);
		rcu_read_unlock();
		break;
	case ID_CONTROLLER_WIRELESS:
		/*
		 * The payload of this event is a single byte:
		 *  0x01: disconnected.
		 *  0x02: connected.
		 */
		switch (data[4]) {
		case 0x01:
			steam_do_connect_event(steam, false);
			break;
		case 0x02:
			steam_do_connect_event(steam, true);
			break;
		}
		break;
	case ID_CONTROLLER_STATUS:
		if (steam->quirks & STEAM_QUIRK_WIRELESS) {
			rcu_read_lock();
			battery = rcu_dereference(steam->battery);
			if (likely(battery)) {
				steam_do_battery_event(steam, battery, data);
			} else {
				dbg_hid(
					"%s: battery data without connect event\n",
					__func__);
				steam_do_connect_event(steam, true);
			}
			rcu_read_unlock();
		}
		break;
	}
	return 0;
}

static int steam_param_set_lizard_mode(const char *val,
					const struct kernel_param *kp)
{
	struct steam_device *steam;
	int ret;

	ret = param_set_bool(val, kp);
	if (ret)
		return ret;

	mutex_lock(&steam_devices_lock);
	list_for_each_entry(steam, &steam_devices, list) {
		if (!steam->client_opened)
			steam_set_lizard_mode(steam, lizard_mode);
	}
	mutex_unlock(&steam_devices_lock);
	return 0;
}

static const struct kernel_param_ops steam_lizard_mode_ops = {
	.set	= steam_param_set_lizard_mode,
	.get	= param_get_bool,
};

module_param_cb(lizard_mode, &steam_lizard_mode_ops, &lizard_mode, 0644);
MODULE_PARM_DESC(lizard_mode,
	"Enable mouse and keyboard emulation (lizard mode) when the gamepad is not in use");

static const struct hid_device_id steam_controllers[] = {
	{ /* Wired Steam Controller */
	  HID_USB_DEVICE(USB_VENDOR_ID_VALVE,
		USB_DEVICE_ID_STEAM_CONTROLLER)
	},
	{ /* Wireless Steam Controller */
	  HID_USB_DEVICE(USB_VENDOR_ID_VALVE,
		USB_DEVICE_ID_STEAM_CONTROLLER_WIRELESS),
	  .driver_data = STEAM_QUIRK_WIRELESS
	},
	{ /* Steam Deck */
	  HID_USB_DEVICE(USB_VENDOR_ID_VALVE,
		USB_DEVICE_ID_STEAM_DECK),
	  .driver_data = STEAM_QUIRK_DECK
	},
	{}
};

MODULE_DEVICE_TABLE(hid, steam_controllers);

static struct hid_driver steam_controller_driver = {
	.name = "hid-steam",
	.id_table = steam_controllers,
	.probe = steam_probe,
	.remove = steam_remove,
	.raw_event = steam_raw_event,
};

module_hid_driver(steam_controller_driver);

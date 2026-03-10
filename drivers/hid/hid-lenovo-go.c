// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Lenovo Legion Go series gamepads.
 *
 *  Copyright (c) 2026 Derek J. Clark <derekjohn.clark@gmail.com>
 *  Copyright (c) 2026 Valve Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/hid.h>
#include <linux/jiffies.h>
#include <linux/kstrtox.h>
#include <linux/led-class-multicolor.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/workqueue_types.h>

#include "hid-ids.h"

#define GO_GP_INTF_IN		0x83
#define GO_OUTPUT_REPORT_ID	0x05
#define GO_GP_RESET_SUCCESS	0x01
#define GO_PACKET_SIZE		64

static struct hid_go_cfg {
	struct delayed_work go_cfg_setup;
	struct completion send_cmd_complete;
	struct led_classdev *led_cdev;
	struct hid_device *hdev;
	struct mutex cfg_mutex; /*ensure single synchronous output report*/
	u8 fps_mode;
	u8 gp_left_auto_sleep_time;
	u8 gp_left_gyro_cal_status;
	u8 gp_left_joy_cal_status;
	u8 gp_left_notify_en;
	u8 gp_left_rumble_mode;
	u8 gp_left_trigg_cal_status;
	u32 gp_left_version_firmware;
	u8 gp_left_version_gen;
	u32 gp_left_version_hardware;
	u32 gp_left_version_product;
	u32 gp_left_version_protocol;
	u8 gp_mode;
	u8 gp_right_auto_sleep_time;
	u8 gp_right_gyro_cal_status;
	u8 gp_right_joy_cal_status;
	u8 gp_right_notify_en;
	u8 gp_right_rumble_mode;
	u8 gp_right_trigg_cal_status;
	u32 gp_right_version_firmware;
	u8 gp_right_version_gen;
	u32 gp_right_version_hardware;
	u32 gp_right_version_product;
	u32 gp_right_version_protocol;
	u8 gp_rumble_intensity;
	u8 imu_left_bypass_en;
	u8 imu_left_sensor_en;
	u8 imu_right_bypass_en;
	u8 imu_right_sensor_en;
	u32 mcu_version_firmware;
	u8 mcu_version_gen;
	u32 mcu_version_hardware;
	u32 mcu_version_product;
	u32 mcu_version_protocol;
	u32 mouse_dpi;
	u8 os_mode;
	u8 rgb_effect;
	u8 rgb_en;
	u8 rgb_mode;
	u8 rgb_profile;
	u8 rgb_speed;
	u8 tp_en;
	u8 tp_vibration_en;
	u8 tp_vibration_intensity;
	u32 tx_dongle_version_firmware;
	u8 tx_dongle_version_gen;
	u32 tx_dongle_version_hardware;
	u32 tx_dongle_version_product;
	u32 tx_dongle_version_protocol;
} drvdata;

struct go_cfg_attr {
	u8 index;
};

struct command_report {
	u8 report_id;
	u8 id;
	u8 cmd;
	u8 sub_cmd;
	u8 device_type;
	u8 data[59];
} __packed;

enum command_id {
	MCU_CONFIG_DATA = 0x00,
	OS_MODE_DATA = 0x06,
	GAMEPAD_DATA = 0x3c,
};

enum mcu_command_index {
	GET_VERSION_DATA = 0x02,
	GET_FEATURE_STATUS,
	SET_FEATURE_STATUS,
	GET_MOTOR_CFG,
	SET_MOTOR_CFG,
	GET_DPI_CFG,
	SET_DPI_CFG,
	SET_TRIGGER_CFG = 0x0a,
	SET_JOYSTICK_CFG = 0x0c,
	SET_GYRO_CFG = 0x0e,
	GET_RGB_CFG,
	SET_RGB_CFG,
	GET_DEVICE_STATUS = 0xa0,

};

enum dev_type {
	UNSPECIFIED,
	USB_MCU,
	TX_DONGLE,
	LEFT_CONTROLLER,
	RIGHT_CONTROLLER,
};

enum enabled_status_index {
	FEATURE_UNKNOWN,
	FEATURE_ENABLED,
	FEATURE_DISABLED,
};

static const char *const enabled_status_text[] = {
	[FEATURE_UNKNOWN] = "unknown",
	[FEATURE_ENABLED] = "true",
	[FEATURE_DISABLED] = "false",
};

enum version_data_index {
	PRODUCT_VERSION = 0x02,
	PROTOCOL_VERSION,
	FIRMWARE_VERSION,
	HARDWARE_VERSION,
	HARDWARE_GENERATION,
};

enum feature_status_index {
	FEATURE_RESET_GAMEPAD = 0x02,
	FEATURE_IMU_BYPASS,
	FEATURE_IMU_ENABLE = 0x05,
	FEATURE_TOUCHPAD_ENABLE = 0x07,
	FEATURE_LIGHT_ENABLE,
	FEATURE_AUTO_SLEEP_TIME,
	FEATURE_FPS_SWITCH_STATUS = 0x0b,
	FEATURE_GAMEPAD_MODE = 0x0e,
};

#define FEATURE_OS_MODE 0x69

enum fps_switch_status_index {
	FPS_STATUS_UNKNOWN,
	GAMEPAD,
	FPS,
};

static const char *const fps_switch_text[] = {
	[FPS_STATUS_UNKNOWN] = "unknown",
	[GAMEPAD] = "gamepad",
	[FPS] = "fps",
};

enum gamepad_mode_index {
	GAMEPAD_MODE_UNKNOWN,
	XINPUT,
	DINPUT,
};

static const char *const gamepad_mode_text[] = {
	[GAMEPAD_MODE_UNKNOWN] = "unknown",
	[XINPUT] = "xinput",
	[DINPUT] = "dinput",
};

enum motor_cfg_index {
	MOTOR_CFG_ALL = 0x01,
	MOTOR_INTENSITY,
	VIBRATION_NOTIFY_ENABLE,
	RUMBLE_MODE,
	TP_VIBRATION_ENABLE,
	TP_VIBRATION_INTENSITY,
};

enum intensity_index {
	INTENSITY_UNKNOWN,
	INTENSITY_OFF,
	INTENSITY_LOW,
	INTENSITY_MEDIUM,
	INTENSITY_HIGH,
};

static const char *const intensity_text[] = {
	[INTENSITY_UNKNOWN] = "unknown",
	[INTENSITY_OFF] = "off",
	[INTENSITY_LOW] = "low",
	[INTENSITY_MEDIUM] = "medium",
	[INTENSITY_HIGH] = "high",
};

enum rumble_mode_index {
	RUMBLE_MODE_UNKNOWN,
	RUMBLE_MODE_FPS,
	RUMBLE_MODE_RACE,
	RUMBLE_MODE_AVERAGE,
	RUMBLE_MODE_SPG,
	RUMBLE_MODE_RPG,
};

static const char *const rumble_mode_text[] = {
	[RUMBLE_MODE_UNKNOWN] = "unknown",
	[RUMBLE_MODE_FPS] = "fps",
	[RUMBLE_MODE_RACE] = "racing",
	[RUMBLE_MODE_AVERAGE] = "standard",
	[RUMBLE_MODE_SPG] = "spg",
	[RUMBLE_MODE_RPG] = "rpg",
};

#define FPS_MODE_DPI		0x02
#define TRIGGER_CALIBRATE	0x04
#define JOYSTICK_CALIBRATE	0x04
#define GYRO_CALIBRATE		0x06

enum cal_device_type {
	CALDEV_GYROSCOPE = 0x01,
	CALDEV_JOYSTICK,
	CALDEV_TRIGGER,
	CALDEV_JOY_TRIGGER,
};

enum cal_enable {
	CAL_UNKNOWN,
	CAL_START,
	CAL_STOP,
};

static const char *const cal_enabled_text[] = {
	[CAL_UNKNOWN] = "unknown",
	[CAL_START] = "start",
	[CAL_STOP] = "stop",
};

enum cal_status_index {
	CAL_STAT_UNKNOWN,
	CAL_STAT_SUCCESS,
	CAL_STAT_FAILURE,
};

static const char *const cal_status_text[] = {
	[CAL_STAT_UNKNOWN] = "unknown",
	[CAL_STAT_SUCCESS] = "success",
	[CAL_STAT_FAILURE] = "failure",
};

enum rgb_config_index {
	LIGHT_CFG_ALL = 0x01,
	LIGHT_MODE_SEL,
	LIGHT_PROFILE_SEL,
	USR_LIGHT_PROFILE_1,
	USR_LIGHT_PROFILE_2,
	USR_LIGHT_PROFILE_3,
};

enum rgb_mode_index {
	RGB_MODE_UNKNOWN,
	RGB_MODE_DYNAMIC,
	RGB_MODE_CUSTOM,
};

static const char *const rgb_mode_text[] = {
	[RGB_MODE_UNKNOWN] = "unknown",
	[RGB_MODE_DYNAMIC] = "dynamic",
	[RGB_MODE_CUSTOM] = "custom",
};

enum rgb_effect_index {
	RGB_EFFECT_MONO,
	RGB_EFFECT_BREATHE,
	RGB_EFFECT_CHROMA,
	RGB_EFFECT_RAINBOW,
};

static const char *const rgb_effect_text[] = {
	[RGB_EFFECT_MONO] = "monocolor",
	[RGB_EFFECT_BREATHE] = "breathe",
	[RGB_EFFECT_CHROMA] = "chroma",
	[RGB_EFFECT_RAINBOW] = "rainbow",
};

enum device_status_index {
	GET_CAL_STATUS = 0x02,
	GET_UPGRADE_STATUS,
	GET_MACRO_REC_STATUS,
	GET_HOTKEY_TRIGG_STATUS,
};

enum os_mode_cfg_index {
	SET_OS_MODE = 0x09,
	GET_OS_MODE,
};

enum os_mode_type_index {
	OS_UNKNOWN,
	WINDOWS,
	LINUX,
};

static const char *const os_mode_text[] = {
	[OS_UNKNOWN] = "unknown",
	[WINDOWS] = "windows",
	[LINUX] = "linux",
};

static int hid_go_version_event(struct command_report *cmd_rep)
{
	switch (cmd_rep->sub_cmd) {
	case PRODUCT_VERSION:
		switch (cmd_rep->device_type) {
		case USB_MCU:
			drvdata.mcu_version_product =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case TX_DONGLE:
			drvdata.tx_dongle_version_product =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case LEFT_CONTROLLER:
			drvdata.gp_left_version_product =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_version_product =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		default:
			return -EINVAL;
		}
	case PROTOCOL_VERSION:
		switch (cmd_rep->device_type) {
		case USB_MCU:
			drvdata.mcu_version_protocol =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case TX_DONGLE:
			drvdata.tx_dongle_version_protocol =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case LEFT_CONTROLLER:
			drvdata.gp_left_version_protocol =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_version_protocol =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		default:
			return -EINVAL;
		}
	case FIRMWARE_VERSION:
		switch (cmd_rep->device_type) {
		case USB_MCU:
			drvdata.mcu_version_firmware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case TX_DONGLE:
			drvdata.tx_dongle_version_firmware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case LEFT_CONTROLLER:
			drvdata.gp_left_version_firmware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_version_firmware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		default:
			return -EINVAL;
		}
	case HARDWARE_VERSION:
		switch (cmd_rep->device_type) {
		case USB_MCU:
			drvdata.mcu_version_hardware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case TX_DONGLE:
			drvdata.tx_dongle_version_hardware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case LEFT_CONTROLLER:
			drvdata.gp_left_version_hardware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_version_hardware =
				get_unaligned_be32(cmd_rep->data);
			return 0;
		default:
			return -EINVAL;
		}
	case HARDWARE_GENERATION:
		switch (cmd_rep->device_type) {
		case USB_MCU:
			drvdata.mcu_version_gen = cmd_rep->data[0];
			return 0;
		case TX_DONGLE:
			drvdata.tx_dongle_version_gen = cmd_rep->data[0];
			return 0;
		case LEFT_CONTROLLER:
			drvdata.gp_left_version_gen = cmd_rep->data[0];
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_version_gen = cmd_rep->data[0];
			return 0;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int hid_go_feature_status_event(struct command_report *cmd_rep)
{
	switch (cmd_rep->sub_cmd) {
	case FEATURE_RESET_GAMEPAD:
		return 0;
	case FEATURE_IMU_ENABLE:
		switch (cmd_rep->device_type) {
		case LEFT_CONTROLLER:
			drvdata.imu_left_sensor_en = cmd_rep->data[0];
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.imu_right_sensor_en = cmd_rep->data[0];
			return 0;
		default:
			return -EINVAL;
		}
	case FEATURE_IMU_BYPASS:
		switch (cmd_rep->device_type) {
		case LEFT_CONTROLLER:
			drvdata.imu_left_bypass_en = cmd_rep->data[0];
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.imu_right_bypass_en = cmd_rep->data[0];
			return 0;
		default:
			return -EINVAL;
		}
		break;
	case FEATURE_LIGHT_ENABLE:
		drvdata.rgb_en = cmd_rep->data[0];
		return 0;
	case FEATURE_AUTO_SLEEP_TIME:
		switch (cmd_rep->device_type) {
		case LEFT_CONTROLLER:
			drvdata.gp_left_auto_sleep_time = cmd_rep->data[0];
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_auto_sleep_time = cmd_rep->data[0];
			return 0;
		default:
			return -EINVAL;
		}
		break;
	case FEATURE_TOUCHPAD_ENABLE:
		drvdata.tp_en = cmd_rep->data[0];
		return 0;
	case FEATURE_GAMEPAD_MODE:
		drvdata.gp_mode = cmd_rep->data[0];
		return 0;
	case FEATURE_FPS_SWITCH_STATUS:
		drvdata.fps_mode = cmd_rep->data[0];
		return 0;
	default:
		return -EINVAL;
	}
}

static int hid_go_motor_event(struct command_report *cmd_rep)
{
	switch (cmd_rep->sub_cmd) {
	case MOTOR_CFG_ALL:
		return -EINVAL;
	case MOTOR_INTENSITY:
		drvdata.gp_rumble_intensity = cmd_rep->data[0];
		return 0;
	case VIBRATION_NOTIFY_ENABLE:
		switch (cmd_rep->device_type) {
		case LEFT_CONTROLLER:
			drvdata.gp_left_notify_en = cmd_rep->data[0];
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_notify_en = cmd_rep->data[0];
			return 0;
		default:
			return -EINVAL;
		}
		break;
	case RUMBLE_MODE:
		switch (cmd_rep->device_type) {
		case LEFT_CONTROLLER:
			drvdata.gp_left_rumble_mode = cmd_rep->data[0];
			return 0;
		case RIGHT_CONTROLLER:
			drvdata.gp_right_rumble_mode = cmd_rep->data[0];
			return 0;
		default:
			return -EINVAL;
		}
	case TP_VIBRATION_ENABLE:
		drvdata.tp_vibration_en = cmd_rep->data[0];
		return 0;
	case TP_VIBRATION_INTENSITY:
		drvdata.tp_vibration_intensity = cmd_rep->data[0];
		return 0;
	}
	return -EINVAL;
}

static int hid_go_fps_dpi_event(struct command_report *cmd_rep)
{
	if (cmd_rep->sub_cmd != FPS_MODE_DPI)
		return -EINVAL;

	drvdata.mouse_dpi = get_unaligned_le32(cmd_rep->data);

	return 0;
}

static int hid_go_light_event(struct command_report *cmd_rep)
{
	struct led_classdev_mc *mc_cdev;

	switch (cmd_rep->sub_cmd) {
	case LIGHT_MODE_SEL:
		drvdata.rgb_mode = cmd_rep->data[0];
		return 0;
	case LIGHT_PROFILE_SEL:
		drvdata.rgb_profile = cmd_rep->data[0];
		return 0;
	case USR_LIGHT_PROFILE_1:
	case USR_LIGHT_PROFILE_2:
	case USR_LIGHT_PROFILE_3:
		mc_cdev = lcdev_to_mccdev(drvdata.led_cdev);
		drvdata.rgb_effect = cmd_rep->data[0];
		mc_cdev->subled_info[0].intensity = cmd_rep->data[1];
		mc_cdev->subled_info[1].intensity = cmd_rep->data[2];
		mc_cdev->subled_info[2].intensity = cmd_rep->data[3];
		drvdata.led_cdev->brightness = cmd_rep->data[4];
		drvdata.rgb_speed = 100 - cmd_rep->data[5];
		return 0;
	default:
		return -EINVAL;
	}
}

static int hid_go_device_status_event(struct command_report *cmd_rep)
{
	switch (cmd_rep->device_type) {
	case LEFT_CONTROLLER:
		switch (cmd_rep->data[0]) {
		case CALDEV_GYROSCOPE:
			drvdata.gp_left_gyro_cal_status = cmd_rep->data[1];
			return 0;
		case CALDEV_JOYSTICK:
			drvdata.gp_left_joy_cal_status = cmd_rep->data[1];
			return 0;
		case CALDEV_TRIGGER:
			drvdata.gp_left_trigg_cal_status = cmd_rep->data[1];
			return 0;
		default:
			return -EINVAL;
		}
		break;
	case RIGHT_CONTROLLER:
		switch (cmd_rep->data[0]) {
		case CALDEV_GYROSCOPE:
			drvdata.gp_right_gyro_cal_status = cmd_rep->data[1];
			return 0;
		case CALDEV_JOYSTICK:
			drvdata.gp_right_joy_cal_status = cmd_rep->data[1];
			return 0;
		case CALDEV_TRIGGER:
			drvdata.gp_right_trigg_cal_status = cmd_rep->data[1];
			return 0;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
}

static int hid_go_os_mode_cfg_event(struct command_report *cmd_rep)
{
	switch (cmd_rep->sub_cmd) {
	case SET_OS_MODE:
		if (cmd_rep->data[0] != 1)
			return -EIO;
		return 0;
	case GET_OS_MODE:
		drvdata.os_mode = cmd_rep->data[0];
		return 0;
	default:
		return -EINVAL;
	}
}

static int hid_go_set_event_return(struct command_report *cmd_rep)
{
	if (cmd_rep->data[0] != 0)
		return -EIO;

	return 0;
}

static int get_endpoint_address(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_host_endpoint *ep;

	if (!intf)
		return -ENODEV;

	ep = intf->cur_altsetting->endpoint;
	if (!ep)
		return -ENODEV;

	return ep->desc.bEndpointAddress;
}

static int hid_go_raw_event(struct hid_device *hdev, struct hid_report *report,
			    u8 *data, int size)
{
	struct command_report *cmd_rep;
	int ep, ret;

	if (size != GO_PACKET_SIZE)
		goto passthrough;

	ep = get_endpoint_address(hdev);
	if (ep != GO_GP_INTF_IN)
		goto passthrough;

	cmd_rep = (struct command_report *)data;

	switch (cmd_rep->id) {
	case MCU_CONFIG_DATA:
		switch (cmd_rep->cmd) {
		case GET_VERSION_DATA:
			ret = hid_go_version_event(cmd_rep);
			break;
		case GET_FEATURE_STATUS:
			ret = hid_go_feature_status_event(cmd_rep);
			break;
		case GET_MOTOR_CFG:
			ret = hid_go_motor_event(cmd_rep);
			break;
		case GET_DPI_CFG:
			ret = hid_go_fps_dpi_event(cmd_rep);
			break;
		case GET_RGB_CFG:
			ret = hid_go_light_event(cmd_rep);
			break;
		case GET_DEVICE_STATUS:
			ret = hid_go_device_status_event(cmd_rep);
			break;
		case SET_FEATURE_STATUS:
		case SET_MOTOR_CFG:
		case SET_DPI_CFG:
		case SET_RGB_CFG:
		case SET_TRIGGER_CFG:
		case SET_JOYSTICK_CFG:
		case SET_GYRO_CFG:
			ret = hid_go_set_event_return(cmd_rep);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case OS_MODE_DATA:
		ret = hid_go_os_mode_cfg_event(cmd_rep);
		break;
	default:
		goto passthrough;
	}
	dev_dbg(&hdev->dev, "Rx data as raw input report: [%*ph]\n",
		GO_PACKET_SIZE, data);

	complete(&drvdata.send_cmd_complete);
	return ret;

passthrough:
	/* Forward other HID reports so they generate events */
	hid_input_report(hdev, HID_INPUT_REPORT, data, size, 1);
	return 0;
}

static int mcu_property_out(struct hid_device *hdev, u8 id, u8 command,
			    u8 index, enum dev_type device, u8 *data, size_t len)
{
	unsigned char *dmabuf __free(kfree) = NULL;
	u8 header[] = { GO_OUTPUT_REPORT_ID, id, command, index, device };
	size_t header_size = ARRAY_SIZE(header);
	int timeout = 50;
	int ret;

	if (header_size + len > GO_PACKET_SIZE)
		return -EINVAL;

	guard(mutex)(&drvdata.cfg_mutex);
	/* We can't use a devm_alloc reusable buffer without side effects during suspend */
	dmabuf = kzalloc(GO_PACKET_SIZE, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	memcpy(dmabuf, header, header_size);
	memcpy(dmabuf + header_size, data, len);

	dev_dbg(&hdev->dev, "Send data as raw output report: [%*ph]\n",
		GO_PACKET_SIZE, dmabuf);

	ret = hid_hw_output_report(hdev, dmabuf, GO_PACKET_SIZE);
	if (ret < 0)
		return ret;

	ret = ret == GO_PACKET_SIZE ? 0 : -EINVAL;
	if (ret)
		return ret;

	ret = wait_for_completion_interruptible_timeout(&drvdata.send_cmd_complete,
							msecs_to_jiffies(timeout));

	if (ret == 0) /* timeout occurred */
		ret = -EBUSY;

	reinit_completion(&drvdata.send_cmd_complete);
	return 0;
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf, enum version_data_index index,
			    enum dev_type device_type)
{
	ssize_t count = 0;
	int ret;

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       index, device_type, NULL, 0);
	if (ret)
		return ret;

	switch (index) {
	case PRODUCT_VERSION:
		switch (device_type) {
		case USB_MCU:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.mcu_version_product);
			break;
		case TX_DONGLE:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.tx_dongle_version_product);
			break;
		case LEFT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_left_version_product);
			break;
		case RIGHT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_right_version_product);
			break;
		default:
			return -EINVAL;
		}
		break;
	case PROTOCOL_VERSION:
		switch (device_type) {
		case USB_MCU:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.mcu_version_protocol);
			break;
		case TX_DONGLE:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.tx_dongle_version_protocol);
			break;
		case LEFT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_left_version_protocol);
			break;
		case RIGHT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_right_version_protocol);
			break;
		default:
			return -EINVAL;
		}
		break;
	case FIRMWARE_VERSION:
		switch (device_type) {
		case USB_MCU:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.mcu_version_firmware);
			break;
		case TX_DONGLE:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.tx_dongle_version_firmware);
			break;
		case LEFT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_left_version_firmware);
			break;
		case RIGHT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_right_version_firmware);
			break;
		default:
			return -EINVAL;
		}
		break;
	case HARDWARE_VERSION:
		switch (device_type) {
		case USB_MCU:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.mcu_version_hardware);
			break;
		case TX_DONGLE:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.tx_dongle_version_hardware);
			break;
		case LEFT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_left_version_hardware);
			break;
		case RIGHT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_right_version_hardware);
			break;
		default:
			return -EINVAL;
		}
		break;
	case HARDWARE_GENERATION:
		switch (device_type) {
		case USB_MCU:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.mcu_version_gen);
			break;
		case TX_DONGLE:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.tx_dongle_version_gen);
			break;
		case LEFT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_left_version_gen);
			break;
		case RIGHT_CONTROLLER:
			count = sysfs_emit(buf, "%x\n",
					   drvdata.gp_right_version_gen);
			break;
		default:
			return -EINVAL;
		}
		break;
	}

	return count;
}

static ssize_t feature_status_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count,
				    enum feature_status_index index,
				    enum dev_type device_type)
{
	size_t size = 1;
	u8 val = 0;
	int ret;

	switch (index) {
	case FEATURE_IMU_ENABLE:
	case FEATURE_IMU_BYPASS:
	case FEATURE_LIGHT_ENABLE:
	case FEATURE_TOUCHPAD_ENABLE:
		ret = sysfs_match_string(enabled_status_text, buf);
		val = ret;
		break;
	case FEATURE_AUTO_SLEEP_TIME:
		ret = kstrtou8(buf, 10, &val);
		break;
	case FEATURE_RESET_GAMEPAD:
		ret = kstrtou8(buf, 10, &val);
		if (val != GO_GP_RESET_SUCCESS)
			return -EINVAL;
		break;
	case FEATURE_FPS_SWITCH_STATUS:
		ret = sysfs_match_string(fps_switch_text, buf);
		val = ret;
		break;
	case FEATURE_GAMEPAD_MODE:
		ret = sysfs_match_string(gamepad_mode_text, buf);
		val = ret;
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	if (!val)
		size = 0;

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA,
			       SET_FEATURE_STATUS, index, device_type, &val,
			       size);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t feature_status_show(struct device *dev,
				   struct device_attribute *attr, char *buf,
				   enum feature_status_index index,
				   enum dev_type device_type)
{
	ssize_t count = 0;
	int ret;
	u8 i;

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA,
			       GET_FEATURE_STATUS, index, device_type, NULL, 0);
	if (ret)
		return ret;

	switch (index) {
	case FEATURE_IMU_ENABLE:
		switch (device_type) {
		case LEFT_CONTROLLER:
			i = drvdata.imu_left_sensor_en;
			break;
		case RIGHT_CONTROLLER:
			i = drvdata.imu_right_sensor_en;
			break;
		default:
			return -EINVAL;
		}
		if (i >= ARRAY_SIZE(enabled_status_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", enabled_status_text[i]);
		break;
	case FEATURE_IMU_BYPASS:
		switch (device_type) {
		case LEFT_CONTROLLER:
			i = drvdata.imu_left_bypass_en;
			break;
		case RIGHT_CONTROLLER:
			i = drvdata.imu_right_bypass_en;
			break;
		default:
			return -EINVAL;
		}
		if (i >= ARRAY_SIZE(enabled_status_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", enabled_status_text[i]);
		break;
	case FEATURE_LIGHT_ENABLE:
		i = drvdata.rgb_en;
		if (i >= ARRAY_SIZE(enabled_status_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", enabled_status_text[i]);
		break;
	case FEATURE_TOUCHPAD_ENABLE:
		i = drvdata.tp_en;
		if (i >= ARRAY_SIZE(enabled_status_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", enabled_status_text[i]);
		break;
	case FEATURE_AUTO_SLEEP_TIME:
		switch (device_type) {
		case LEFT_CONTROLLER:
			i = drvdata.gp_left_auto_sleep_time;
			break;
		case RIGHT_CONTROLLER:
			i = drvdata.gp_right_auto_sleep_time;
			break;
		default:
			return -EINVAL;
		}
		count = sysfs_emit(buf, "%u\n", i);
		break;
	case FEATURE_FPS_SWITCH_STATUS:
		i = drvdata.fps_mode;
		if (i >= ARRAY_SIZE(fps_switch_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", fps_switch_text[i]);
		break;
	case FEATURE_GAMEPAD_MODE:
		i = drvdata.gp_mode;
		if (i >= ARRAY_SIZE(gamepad_mode_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", gamepad_mode_text[i]);
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static ssize_t feature_status_options(struct device *dev,
				      struct device_attribute *attr, char *buf,
				      enum feature_status_index index)
{
	ssize_t count = 0;
	unsigned int i;

	switch (index) {
	case FEATURE_IMU_ENABLE:
	case FEATURE_IMU_BYPASS:
	case FEATURE_LIGHT_ENABLE:
	case FEATURE_TOUCHPAD_ENABLE:
		for (i = 1; i < ARRAY_SIZE(enabled_status_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       enabled_status_text[i]);
		}
		break;
	case FEATURE_AUTO_SLEEP_TIME:
		return sysfs_emit(buf, "0-255\n");
	case FEATURE_FPS_SWITCH_STATUS:
		for (i = 1; i < ARRAY_SIZE(fps_switch_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       fps_switch_text[i]);
		}
		break;
	case FEATURE_GAMEPAD_MODE:
		for (i = 1; i < ARRAY_SIZE(gamepad_mode_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       gamepad_mode_text[i]);
		}
		break;
	default:
		return -EINVAL;
	}

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t motor_config_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count,
				  enum motor_cfg_index index,
				  enum dev_type device_type)
{
	size_t size = 1;
	u8 val = 0;
	int ret;

	switch (index) {
	case MOTOR_CFG_ALL:
		return -EINVAL;
	case MOTOR_INTENSITY:
		ret = sysfs_match_string(intensity_text, buf);
		val = ret;
		break;
	case VIBRATION_NOTIFY_ENABLE:
		ret = sysfs_match_string(enabled_status_text, buf);
		val = ret;
		break;
	case RUMBLE_MODE:
		ret = sysfs_match_string(rumble_mode_text, buf);
		val = ret;
		break;
	case TP_VIBRATION_ENABLE:
		ret = sysfs_match_string(enabled_status_text, buf);
		val = ret;
		break;
	case TP_VIBRATION_INTENSITY:
		ret = sysfs_match_string(intensity_text, buf);
		val = ret;
		break;
	}

	if (ret < 0)
		return ret;

	if (!val)
		size = 0;

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, SET_MOTOR_CFG,
			       index, device_type, &val, size);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t motor_config_show(struct device *dev,
				 struct device_attribute *attr, char *buf,
				 enum motor_cfg_index index,
				 enum dev_type device_type)
{
	ssize_t count = 0;
	int ret;
	u8 i;

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_MOTOR_CFG,
			       index, device_type, NULL, 0);
	if (ret)
		return ret;

	switch (index) {
	case MOTOR_CFG_ALL:
		return -EINVAL;
	case MOTOR_INTENSITY:
		i = drvdata.gp_rumble_intensity;
		if (i >= ARRAY_SIZE(intensity_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", intensity_text[i]);
		break;
	case VIBRATION_NOTIFY_ENABLE:
		switch (device_type) {
		case LEFT_CONTROLLER:
			i = drvdata.gp_left_notify_en;
			break;
		case RIGHT_CONTROLLER:
			i = drvdata.gp_right_notify_en;
			break;
		default:
			return -EINVAL;
		}
		if (i >= ARRAY_SIZE(enabled_status_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", enabled_status_text[i]);
		break;
	case RUMBLE_MODE:
		switch (device_type) {
		case LEFT_CONTROLLER:
			i = drvdata.gp_left_rumble_mode;
			break;
		case RIGHT_CONTROLLER:
			i = drvdata.gp_right_rumble_mode;
			break;
		default:
			return -EINVAL;
		}
		if (i >= ARRAY_SIZE(rumble_mode_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", rumble_mode_text[i]);
		break;
	case TP_VIBRATION_ENABLE:
		i = drvdata.tp_vibration_en;
		if (i >= ARRAY_SIZE(enabled_status_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", enabled_status_text[i]);
		break;
	case TP_VIBRATION_INTENSITY:
		i = drvdata.tp_vibration_intensity;
		if (i >= ARRAY_SIZE(intensity_text))
			return -EINVAL;

		count = sysfs_emit(buf, "%s\n", intensity_text[i]);
		break;
	}

	return count;
}

static ssize_t motor_config_options(struct device *dev,
				    struct device_attribute *attr, char *buf,
				    enum motor_cfg_index index)
{
	ssize_t count = 0;
	unsigned int i;

	switch (index) {
	case MOTOR_CFG_ALL:
		break;
	case RUMBLE_MODE:
		for (i = 1; i < ARRAY_SIZE(rumble_mode_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       rumble_mode_text[i]);
		}
		break;
	case MOTOR_INTENSITY:
	case TP_VIBRATION_INTENSITY:
		for (i = 1; i < ARRAY_SIZE(intensity_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       intensity_text[i]);
		}
		break;
	case VIBRATION_NOTIFY_ENABLE:
	case TP_VIBRATION_ENABLE:
		for (i = 1; i < ARRAY_SIZE(enabled_status_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       enabled_status_text[i]);
		}
		break;
	}

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t fps_mode_dpi_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)

{
	size_t size = 4;
	u32 value;
	u8 val[4];
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (value != 500 && value != 800 && value != 1200 && value != 1800)
		return -EINVAL;

	put_unaligned_le32(value, val);

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, SET_DPI_CFG,
			       FPS_MODE_DPI, UNSPECIFIED, val, size);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t fps_mode_dpi_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret;

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_DPI_CFG,
			       FPS_MODE_DPI, UNSPECIFIED, NULL, 0);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%u\n", drvdata.mouse_dpi);
}

static ssize_t fps_mode_dpi_index_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "500 800 1200 1800\n");
}

static ssize_t device_status_show(struct device *dev,
				  struct device_attribute *attr, char *buf,
				  enum device_status_index index,
				  enum dev_type device_type,
				  enum cal_device_type cal_type)
{
	u8 i;

	switch (index) {
	case GET_CAL_STATUS:
		switch (device_type) {
		case LEFT_CONTROLLER:
			switch (cal_type) {
			case CALDEV_GYROSCOPE:
				i = drvdata.gp_left_gyro_cal_status;
				break;
			case CALDEV_JOYSTICK:
				i = drvdata.gp_left_joy_cal_status;
				break;
			case CALDEV_TRIGGER:
				i = drvdata.gp_left_trigg_cal_status;
				break;
			default:
				return -EINVAL;
			}
			break;
		case RIGHT_CONTROLLER:
			switch (cal_type) {
			case CALDEV_GYROSCOPE:
				i = drvdata.gp_right_gyro_cal_status;
				break;
			case CALDEV_JOYSTICK:
				i = drvdata.gp_right_joy_cal_status;
				break;
			case CALDEV_TRIGGER:
				i = drvdata.gp_right_trigg_cal_status;
				break;
			default:
				return -EINVAL;
			}
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (i >= ARRAY_SIZE(cal_status_text))
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", cal_status_text[i]);
}

static ssize_t calibrate_config_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, u8 cmd, u8 sub_cmd,
				      size_t count, enum dev_type device_type)
{
	size_t size = 1;
	u8 val = 0;
	int ret;

	ret = sysfs_match_string(cal_enabled_text, buf);
	if (ret < 0)
		return ret;

	val = ret;
	if (!val)
		size = 0;

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, cmd, sub_cmd,
			       device_type, &val, size);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t calibrate_config_options(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t count = 0;
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(cal_enabled_text); i++)
		count += sysfs_emit_at(buf, count, "%s ", cal_enabled_text[i]);

	buf[count - 1] = '\n';

	return count;
}

static ssize_t os_mode_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	size_t size = 1;
	int ret;
	u8 val;

	ret = sysfs_match_string(os_mode_text, buf);
	if (ret <= 0)
		return ret;

	val = ret;
	ret = mcu_property_out(drvdata.hdev, OS_MODE_DATA, FEATURE_OS_MODE,
			       SET_OS_MODE, USB_MCU, &val, size);
	if (ret < 0)
		return ret;

	drvdata.os_mode = val;

	return count;
}

static ssize_t os_mode_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	ssize_t count = 0;
	int ret;
	u8 i;

	ret = mcu_property_out(drvdata.hdev, OS_MODE_DATA, FEATURE_OS_MODE,
			       GET_OS_MODE, USB_MCU, NULL, 0);
	if (ret)
		return ret;

	i = drvdata.os_mode;
	if (i >= ARRAY_SIZE(os_mode_text))
		return -EINVAL;

	count = sysfs_emit(buf, "%s\n", os_mode_text[i]);

	return count;
}

static ssize_t os_mode_index_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(os_mode_text); i++)
		count += sysfs_emit_at(buf, count, "%s ", os_mode_text[i]);

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static int rgb_cfg_call(struct hid_device *hdev, enum mcu_command_index cmd,
			enum rgb_config_index index, u8 *val, size_t size)
{
	if (cmd != SET_RGB_CFG && cmd != GET_RGB_CFG)
		return -EINVAL;

	if (index < LIGHT_CFG_ALL || index > USR_LIGHT_PROFILE_3)
		return -EINVAL;

	return mcu_property_out(hdev, MCU_CONFIG_DATA, cmd, index, UNSPECIFIED,
				val, size);
}

static int rgb_attr_show(void)
{
	enum rgb_config_index index;

	index = drvdata.rgb_profile + 3;

	return rgb_cfg_call(drvdata.hdev, GET_RGB_CFG, index, NULL, 0);
}

static ssize_t rgb_effect_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(drvdata.led_cdev);
	enum rgb_config_index index;
	u8 effect;
	int ret;

	ret = sysfs_match_string(rgb_effect_text, buf);
	if (ret < 0)
		return ret;

	effect = ret;
	index = drvdata.rgb_profile + 3;
	u8 rgb_profile[6] = { effect,
			      mc_cdev->subled_info[0].intensity,
			      mc_cdev->subled_info[1].intensity,
			      mc_cdev->subled_info[2].intensity,
			      drvdata.led_cdev->brightness,
			      drvdata.rgb_speed };

	ret = rgb_cfg_call(drvdata.hdev, SET_RGB_CFG, index, rgb_profile, 6);
	if (ret)
		return ret;

	drvdata.rgb_effect = effect;
	return count;
}

static ssize_t rgb_effect_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int ret;

	ret = rgb_attr_show();
	if (ret)
		return ret;

	if (drvdata.rgb_effect >= ARRAY_SIZE(rgb_effect_text))
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", rgb_effect_text[drvdata.rgb_effect]);
}

static ssize_t rgb_effect_index_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rgb_effect_text); i++)
		count += sysfs_emit_at(buf, count, "%s ", rgb_effect_text[i]);

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t rgb_speed_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(drvdata.led_cdev);
	enum rgb_config_index index;
	int val = 0;
	int ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if (val < 0 || val > 100)
		return -EINVAL;

	/* This is a delay setting, invert logic for consistency with other drivers */
	val = 100 - val;

	index = drvdata.rgb_profile + 3;
	u8 rgb_profile[6] = { drvdata.rgb_effect,
			      mc_cdev->subled_info[0].intensity,
			      mc_cdev->subled_info[1].intensity,
			      mc_cdev->subled_info[2].intensity,
			      drvdata.led_cdev->brightness,
			      val };

	ret = rgb_cfg_call(drvdata.hdev, SET_RGB_CFG, index, rgb_profile, 6);
	if (ret)
		return ret;

	drvdata.rgb_speed = val;

	return count;
}

static ssize_t rgb_speed_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	int ret, val;

	ret = rgb_attr_show();
	if (ret)
		return ret;

	if (drvdata.rgb_speed > 100)
		return -EINVAL;

	val = drvdata.rgb_speed;

	return sysfs_emit(buf, "%hhu\n", val);
}

static ssize_t rgb_speed_range_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0-100\n");
}

static ssize_t rgb_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret;
	u8 val;

	ret = sysfs_match_string(rgb_mode_text, buf);
	if (ret <= 0)
		return ret;

	val = ret;

	ret = rgb_cfg_call(drvdata.hdev, SET_RGB_CFG, LIGHT_MODE_SEL, &val, 1);
	if (ret)
		return ret;

	drvdata.rgb_mode = val;

	return count;
}

static ssize_t rgb_mode_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int ret;

	ret = rgb_cfg_call(drvdata.hdev, GET_RGB_CFG, LIGHT_MODE_SEL, NULL, 0);
	if (ret)
		return ret;

	if (drvdata.rgb_mode >= ARRAY_SIZE(rgb_mode_text))
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", rgb_mode_text[drvdata.rgb_mode]);
}

static ssize_t rgb_mode_index_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(rgb_mode_text); i++)
		count += sysfs_emit_at(buf, count, "%s ", rgb_mode_text[i]);

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t rgb_profile_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	size_t size = 1;
	int ret;
	u8 val;

	ret = kstrtou8(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val < 1 || val > 3)
		return -EINVAL;

	ret = rgb_cfg_call(drvdata.hdev, SET_RGB_CFG, LIGHT_PROFILE_SEL, &val, size);
	if (ret)
		return ret;

	drvdata.rgb_profile = val;

	return count;
}

static ssize_t rgb_profile_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;

	ret = rgb_cfg_call(drvdata.hdev, GET_RGB_CFG, LIGHT_PROFILE_SEL, NULL, 0);
	if (ret)
		return ret;

	if (drvdata.rgb_profile < 1 || drvdata.rgb_profile > 3)
		return -EINVAL;

	return sysfs_emit(buf, "%hhu\n", drvdata.rgb_profile);
}

static ssize_t rgb_profile_range_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "1-3\n");
}

static void hid_go_brightness_set(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(drvdata.led_cdev);
	enum rgb_config_index index;
	int ret;

	if (brightness > led_cdev->max_brightness) {
		dev_err(led_cdev->dev, "Invalid argument\n");
		return;
	}

	index = drvdata.rgb_profile + 3;
	u8 rgb_profile[6] = { drvdata.rgb_effect,
			      mc_cdev->subled_info[0].intensity,
			      mc_cdev->subled_info[1].intensity,
			      mc_cdev->subled_info[2].intensity,
			      brightness,
			      drvdata.rgb_speed };

	ret = rgb_cfg_call(drvdata.hdev, SET_RGB_CFG, index, rgb_profile, 6);
	switch (ret) {
	case 0:
		led_cdev->brightness = brightness;
		break;
	case -ENODEV: /* during switch to IAP -ENODEV is expected */
	case -ENOSYS: /* during rmmod -ENOSYS is expected */
		dev_dbg(led_cdev->dev, "Failed to write RGB profile: %i\n", ret);
		break;
	default:
		dev_err(led_cdev->dev, "Failed to write RGB profile: %i\n", ret);
	}
}

#define LEGO_DEVICE_ATTR_RW(_name, _attrname, _dtype, _rtype, _group)         \
	static ssize_t _name##_store(struct device *dev,                      \
				     struct device_attribute *attr,           \
				     const char *buf, size_t count)           \
	{                                                                     \
		return _group##_store(dev, attr, buf, count, _name.index,     \
				      _dtype);                                \
	}                                                                     \
	static ssize_t _name##_show(struct device *dev,                       \
				    struct device_attribute *attr, char *buf) \
	{                                                                     \
		return _group##_show(dev, attr, buf, _name.index, _dtype);    \
	}                                                                     \
	static ssize_t _name##_##_rtype##_show(                               \
		struct device *dev, struct device_attribute *attr, char *buf) \
	{                                                                     \
		return _group##_options(dev, attr, buf, _name.index);         \
	}                                                                     \
	static DEVICE_ATTR_RW_NAMED(_name, _attrname)

#define LEGO_DEVICE_ATTR_WO(_name, _attrname, _dtype, _group)             \
	static ssize_t _name##_store(struct device *dev,                  \
				     struct device_attribute *attr,       \
				     const char *buf, size_t count)       \
	{                                                                 \
		return _group##_store(dev, attr, buf, count, _name.index, \
				      _dtype);                            \
	}                                                                 \
	static DEVICE_ATTR_WO_NAMED(_name, _attrname)

#define LEGO_DEVICE_ATTR_RO(_name, _attrname, _dtype, _group)                 \
	static ssize_t _name##_show(struct device *dev,                       \
				    struct device_attribute *attr, char *buf) \
	{                                                                     \
		return _group##_show(dev, attr, buf, _name.index, _dtype);    \
	}                                                                     \
	static DEVICE_ATTR_RO_NAMED(_name, _attrname)

#define LEGO_CAL_DEVICE_ATTR(_name, _attrname, _scmd, _dtype, _rtype)         \
	static ssize_t _name##_store(struct device *dev,                      \
				     struct device_attribute *attr,           \
				     const char *buf, size_t count)           \
	{                                                                     \
		return calibrate_config_store(dev, attr, buf, _name.index,    \
					      _scmd, count, _dtype);          \
	}                                                                     \
	static ssize_t _name##_##_rtype##_show(                               \
		struct device *dev, struct device_attribute *attr, char *buf) \
	{                                                                     \
		return calibrate_config_options(dev, attr, buf);              \
	}                                                                     \
	static DEVICE_ATTR_WO_NAMED(_name, _attrname)

#define LEGO_DEVICE_STATUS_ATTR(_name, _attrname, _scmd, _dtype)              \
	static ssize_t _name##_show(struct device *dev,                       \
				    struct device_attribute *attr, char *buf) \
	{                                                                     \
		return device_status_show(dev, attr, buf, _name.index, _scmd, \
					  _dtype);                            \
	}                                                                     \
	static DEVICE_ATTR_RO_NAMED(_name, _attrname)

/* Gamepad - MCU */
static struct go_cfg_attr version_product_mcu = { PRODUCT_VERSION };
LEGO_DEVICE_ATTR_RO(version_product_mcu, "product_version", USB_MCU, version);

static struct go_cfg_attr version_protocol_mcu = { PROTOCOL_VERSION };
LEGO_DEVICE_ATTR_RO(version_protocol_mcu, "protocol_version", USB_MCU, version);

static struct go_cfg_attr version_firmware_mcu = { FIRMWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_firmware_mcu, "firmware_version", USB_MCU, version);

static struct go_cfg_attr version_hardware_mcu = { HARDWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_hardware_mcu, "hardware_version", USB_MCU, version);

static struct go_cfg_attr version_gen_mcu = { HARDWARE_GENERATION };
LEGO_DEVICE_ATTR_RO(version_gen_mcu, "hardware_generation", USB_MCU, version);

static struct go_cfg_attr fps_switch_status = { FEATURE_FPS_SWITCH_STATUS };
LEGO_DEVICE_ATTR_RO(fps_switch_status, "fps_switch_status", UNSPECIFIED,
		    feature_status);

static struct go_cfg_attr gamepad_mode = { FEATURE_GAMEPAD_MODE };
LEGO_DEVICE_ATTR_RW(gamepad_mode, "mode", UNSPECIFIED, index, feature_status);
static DEVICE_ATTR_RO_NAMED(gamepad_mode_index, "mode_index");

static struct go_cfg_attr reset_mcu = { FEATURE_RESET_GAMEPAD };
LEGO_DEVICE_ATTR_WO(reset_mcu, "reset_mcu", USB_MCU, feature_status);

static struct go_cfg_attr gamepad_rumble_intensity = { MOTOR_INTENSITY };
LEGO_DEVICE_ATTR_RW(gamepad_rumble_intensity, "rumble_intensity", UNSPECIFIED,
		    index, motor_config);
static DEVICE_ATTR_RO_NAMED(gamepad_rumble_intensity_index,
			    "rumble_intensity_index");

static DEVICE_ATTR_RW(fps_mode_dpi);
static DEVICE_ATTR_RO(fps_mode_dpi_index);

static DEVICE_ATTR_RW(os_mode);
static DEVICE_ATTR_RO(os_mode_index);

static struct attribute *mcu_attrs[] = {
	&dev_attr_fps_mode_dpi.attr,
	&dev_attr_fps_mode_dpi_index.attr,
	&dev_attr_fps_switch_status.attr,
	&dev_attr_gamepad_mode.attr,
	&dev_attr_gamepad_mode_index.attr,
	&dev_attr_gamepad_rumble_intensity.attr,
	&dev_attr_gamepad_rumble_intensity_index.attr,
	&dev_attr_os_mode.attr,
	&dev_attr_os_mode_index.attr,
	&dev_attr_reset_mcu.attr,
	&dev_attr_version_firmware_mcu.attr,
	&dev_attr_version_gen_mcu.attr,
	&dev_attr_version_hardware_mcu.attr,
	&dev_attr_version_product_mcu.attr,
	&dev_attr_version_protocol_mcu.attr,
	NULL,
};

static const struct attribute_group mcu_attr_group = {
	.attrs = mcu_attrs,
};

/* Gamepad - TX Dongle */
static struct go_cfg_attr version_product_tx_dongle = { PRODUCT_VERSION };
LEGO_DEVICE_ATTR_RO(version_product_tx_dongle, "product_version", TX_DONGLE, version);

static struct go_cfg_attr version_protocol_tx_dongle = { PROTOCOL_VERSION };
LEGO_DEVICE_ATTR_RO(version_protocol_tx_dongle, "protocol_version", TX_DONGLE, version);

static struct go_cfg_attr version_firmware_tx_dongle = { FIRMWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_firmware_tx_dongle, "firmware_version", TX_DONGLE, version);

static struct go_cfg_attr version_hardware_tx_dongle = { HARDWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_hardware_tx_dongle, "hardware_version", TX_DONGLE, version);

static struct go_cfg_attr version_gen_tx_dongle = { HARDWARE_GENERATION };
LEGO_DEVICE_ATTR_RO(version_gen_tx_dongle, "hardware_generation", TX_DONGLE, version);

static struct go_cfg_attr reset_tx_dongle = { FEATURE_RESET_GAMEPAD };
LEGO_DEVICE_ATTR_RO(reset_tx_dongle, "reset", TX_DONGLE, feature_status);

static struct attribute *tx_dongle_attrs[] = {
	&dev_attr_reset_tx_dongle.attr,
	&dev_attr_version_hardware_tx_dongle.attr,
	&dev_attr_version_firmware_tx_dongle.attr,
	&dev_attr_version_gen_tx_dongle.attr,
	&dev_attr_version_product_tx_dongle.attr,
	&dev_attr_version_protocol_tx_dongle.attr,
	NULL,
};

static const struct attribute_group tx_dongle_attr_group = {
	.name = "tx_dongle",
	.attrs = tx_dongle_attrs,
};

/* Gamepad - Left */
static struct go_cfg_attr version_product_left = { PRODUCT_VERSION };
LEGO_DEVICE_ATTR_RO(version_product_left, "product_version", LEFT_CONTROLLER, version);

static struct go_cfg_attr version_protocol_left = { PROTOCOL_VERSION };
LEGO_DEVICE_ATTR_RO(version_protocol_left, "protocol_version", LEFT_CONTROLLER, version);

static struct go_cfg_attr version_firmware_left = { FIRMWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_firmware_left, "firmware_version", LEFT_CONTROLLER, version);

static struct go_cfg_attr version_hardware_left = { HARDWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_hardware_left, "hardware_version", LEFT_CONTROLLER, version);

static struct go_cfg_attr version_gen_left = { HARDWARE_GENERATION };
LEGO_DEVICE_ATTR_RO(version_gen_left, "hardware_generation", LEFT_CONTROLLER, version);

static struct go_cfg_attr auto_sleep_time_left = { FEATURE_AUTO_SLEEP_TIME };
LEGO_DEVICE_ATTR_RW(auto_sleep_time_left, "auto_sleep_time", LEFT_CONTROLLER,
		    range, feature_status);
static DEVICE_ATTR_RO_NAMED(auto_sleep_time_left_range,
			    "auto_sleep_time_range");

static struct go_cfg_attr imu_bypass_left = { FEATURE_IMU_BYPASS };
LEGO_DEVICE_ATTR_RW(imu_bypass_left, "imu_bypass_enabled", LEFT_CONTROLLER,
		    index, feature_status);
static DEVICE_ATTR_RO_NAMED(imu_bypass_left_index, "imu_bypass_enabled_index");

static struct go_cfg_attr imu_enabled_left = { FEATURE_IMU_ENABLE };
LEGO_DEVICE_ATTR_RW(imu_enabled_left, "imu_enabled", LEFT_CONTROLLER, index,
		    feature_status);
static DEVICE_ATTR_RO_NAMED(imu_enabled_left_index, "imu_enabled_index");

static struct go_cfg_attr reset_left = { FEATURE_RESET_GAMEPAD };
LEGO_DEVICE_ATTR_WO(reset_left, "reset", LEFT_CONTROLLER, feature_status);

static struct go_cfg_attr rumble_mode_left = { RUMBLE_MODE };
LEGO_DEVICE_ATTR_RW(rumble_mode_left, "rumble_mode", LEFT_CONTROLLER, index,
		    motor_config);
static DEVICE_ATTR_RO_NAMED(rumble_mode_left_index, "rumble_mode_index");

static struct go_cfg_attr rumble_notification_left = { VIBRATION_NOTIFY_ENABLE };
LEGO_DEVICE_ATTR_RW(rumble_notification_left, "rumble_notification",
		    LEFT_CONTROLLER, index, motor_config);
static DEVICE_ATTR_RO_NAMED(rumble_notification_left_index,
			    "rumble_notification_index");

static struct go_cfg_attr cal_trigg_left = { TRIGGER_CALIBRATE };
LEGO_CAL_DEVICE_ATTR(cal_trigg_left, "calibrate_trigger", SET_TRIGGER_CFG,
		     LEFT_CONTROLLER, index);
static DEVICE_ATTR_RO_NAMED(cal_trigg_left_index, "calibrate_trigger_index");

static struct go_cfg_attr cal_joy_left = { JOYSTICK_CALIBRATE };
LEGO_CAL_DEVICE_ATTR(cal_joy_left, "calibrate_joystick", SET_JOYSTICK_CFG,
		     LEFT_CONTROLLER, index);
static DEVICE_ATTR_RO_NAMED(cal_joy_left_index, "calibrate_joystick_index");

static struct go_cfg_attr cal_gyro_left = { GYRO_CALIBRATE };
LEGO_CAL_DEVICE_ATTR(cal_gyro_left, "calibrate_gyro", SET_GYRO_CFG,
		     LEFT_CONTROLLER, index);
static DEVICE_ATTR_RO_NAMED(cal_gyro_left_index, "calibrate_gyro_index");

static struct go_cfg_attr cal_trigg_left_status = { GET_CAL_STATUS };
LEGO_DEVICE_STATUS_ATTR(cal_trigg_left_status, "calibrate_trigger_status",
			LEFT_CONTROLLER, CALDEV_TRIGGER);

static struct go_cfg_attr cal_joy_left_status = { GET_CAL_STATUS };
LEGO_DEVICE_STATUS_ATTR(cal_joy_left_status, "calibrate_joystick_status",
			LEFT_CONTROLLER, CALDEV_JOYSTICK);

static struct go_cfg_attr cal_gyro_left_status = { GET_CAL_STATUS };
LEGO_DEVICE_STATUS_ATTR(cal_gyro_left_status, "calibrate_gyro_status",
			LEFT_CONTROLLER, CALDEV_GYROSCOPE);

static struct attribute *left_gamepad_attrs[] = {
	&dev_attr_auto_sleep_time_left.attr,
	&dev_attr_auto_sleep_time_left_range.attr,
	&dev_attr_cal_gyro_left.attr,
	&dev_attr_cal_gyro_left_index.attr,
	&dev_attr_cal_gyro_left_status.attr,
	&dev_attr_cal_joy_left.attr,
	&dev_attr_cal_joy_left_index.attr,
	&dev_attr_cal_joy_left_status.attr,
	&dev_attr_cal_trigg_left.attr,
	&dev_attr_cal_trigg_left_index.attr,
	&dev_attr_cal_trigg_left_status.attr,
	&dev_attr_imu_bypass_left.attr,
	&dev_attr_imu_bypass_left_index.attr,
	&dev_attr_imu_enabled_left.attr,
	&dev_attr_imu_enabled_left_index.attr,
	&dev_attr_reset_left.attr,
	&dev_attr_rumble_mode_left.attr,
	&dev_attr_rumble_mode_left_index.attr,
	&dev_attr_rumble_notification_left.attr,
	&dev_attr_rumble_notification_left_index.attr,
	&dev_attr_version_hardware_left.attr,
	&dev_attr_version_firmware_left.attr,
	&dev_attr_version_gen_left.attr,
	&dev_attr_version_product_left.attr,
	&dev_attr_version_protocol_left.attr,
	NULL,
};

static const struct attribute_group left_gamepad_attr_group = {
	.name = "left_handle",
	.attrs = left_gamepad_attrs,
};

/* Gamepad - Right */
static struct go_cfg_attr version_product_right = { PRODUCT_VERSION };
LEGO_DEVICE_ATTR_RO(version_product_right, "product_version", RIGHT_CONTROLLER, version);

static struct go_cfg_attr version_protocol_right = { PROTOCOL_VERSION };
LEGO_DEVICE_ATTR_RO(version_protocol_right, "protocol_version", RIGHT_CONTROLLER, version);

static struct go_cfg_attr version_firmware_right = { FIRMWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_firmware_right, "firmware_version", RIGHT_CONTROLLER, version);

static struct go_cfg_attr version_hardware_right = { HARDWARE_VERSION };
LEGO_DEVICE_ATTR_RO(version_hardware_right, "hardware_version", RIGHT_CONTROLLER, version);

static struct go_cfg_attr version_gen_right = { HARDWARE_GENERATION };
LEGO_DEVICE_ATTR_RO(version_gen_right, "hardware_generation", RIGHT_CONTROLLER, version);

static struct go_cfg_attr auto_sleep_time_right = { FEATURE_AUTO_SLEEP_TIME };
LEGO_DEVICE_ATTR_RW(auto_sleep_time_right, "auto_sleep_time", RIGHT_CONTROLLER,
		    range, feature_status);
static DEVICE_ATTR_RO_NAMED(auto_sleep_time_right_range,
			    "auto_sleep_time_range");

static struct go_cfg_attr imu_bypass_right = { FEATURE_IMU_BYPASS };
LEGO_DEVICE_ATTR_RW(imu_bypass_right, "imu_bypass_enabled", RIGHT_CONTROLLER,
		    index, feature_status);
static DEVICE_ATTR_RO_NAMED(imu_bypass_right_index, "imu_bypass_enabled_index");

static struct go_cfg_attr imu_enabled_right = { FEATURE_IMU_BYPASS };
LEGO_DEVICE_ATTR_RW(imu_enabled_right, "imu_enabled", RIGHT_CONTROLLER, index,
		    feature_status);
static DEVICE_ATTR_RO_NAMED(imu_enabled_right_index, "imu_enabled_index");

static struct go_cfg_attr reset_right = { FEATURE_RESET_GAMEPAD };
LEGO_DEVICE_ATTR_WO(reset_right, "reset", LEFT_CONTROLLER, feature_status);

static struct go_cfg_attr rumble_mode_right = { RUMBLE_MODE };
LEGO_DEVICE_ATTR_RW(rumble_mode_right, "rumble_mode", RIGHT_CONTROLLER, index,
		    motor_config);
static DEVICE_ATTR_RO_NAMED(rumble_mode_right_index, "rumble_mode_index");

static struct go_cfg_attr rumble_notification_right = { VIBRATION_NOTIFY_ENABLE };
LEGO_DEVICE_ATTR_RW(rumble_notification_right, "rumble_notification",
		    RIGHT_CONTROLLER, index, motor_config);
static DEVICE_ATTR_RO_NAMED(rumble_notification_right_index,
			    "rumble_notification_index");

static struct go_cfg_attr cal_trigg_right = { TRIGGER_CALIBRATE };
LEGO_CAL_DEVICE_ATTR(cal_trigg_right, "calibrate_trigger", SET_TRIGGER_CFG,
		     RIGHT_CONTROLLER, index);
static DEVICE_ATTR_RO_NAMED(cal_trigg_right_index, "calibrate_trigger_index");

static struct go_cfg_attr cal_joy_right = { JOYSTICK_CALIBRATE };
LEGO_CAL_DEVICE_ATTR(cal_joy_right, "calibrate_joystick", SET_JOYSTICK_CFG,
		     RIGHT_CONTROLLER, index);
static DEVICE_ATTR_RO_NAMED(cal_joy_right_index, "calibrate_joystick_index");

static struct go_cfg_attr cal_gyro_right = { GYRO_CALIBRATE };
LEGO_CAL_DEVICE_ATTR(cal_gyro_right, "calibrate_gyro", SET_GYRO_CFG,
		     RIGHT_CONTROLLER, index);
static DEVICE_ATTR_RO_NAMED(cal_gyro_right_index, "calibrate_gyro_index");

static struct go_cfg_attr cal_trigg_right_status = { GET_CAL_STATUS };
LEGO_DEVICE_STATUS_ATTR(cal_trigg_right_status, "calibrate_trigger_status",
			RIGHT_CONTROLLER, CALDEV_TRIGGER);

static struct go_cfg_attr cal_joy_right_status = { GET_CAL_STATUS };
LEGO_DEVICE_STATUS_ATTR(cal_joy_right_status, "calibrate_joystick_status",
			RIGHT_CONTROLLER, CALDEV_JOYSTICK);

static struct go_cfg_attr cal_gyro_right_status = { GET_CAL_STATUS };
LEGO_DEVICE_STATUS_ATTR(cal_gyro_right_status, "calibrate_gyro_status",
			RIGHT_CONTROLLER, CALDEV_GYROSCOPE);

static struct attribute *right_gamepad_attrs[] = {
	&dev_attr_auto_sleep_time_right.attr,
	&dev_attr_auto_sleep_time_right_range.attr,
	&dev_attr_cal_gyro_right.attr,
	&dev_attr_cal_gyro_right_index.attr,
	&dev_attr_cal_gyro_right_status.attr,
	&dev_attr_cal_joy_right.attr,
	&dev_attr_cal_joy_right_index.attr,
	&dev_attr_cal_joy_right_status.attr,
	&dev_attr_cal_trigg_right.attr,
	&dev_attr_cal_trigg_right_index.attr,
	&dev_attr_cal_trigg_right_status.attr,
	&dev_attr_imu_bypass_right.attr,
	&dev_attr_imu_bypass_right_index.attr,
	&dev_attr_imu_enabled_right.attr,
	&dev_attr_imu_enabled_right_index.attr,
	&dev_attr_reset_right.attr,
	&dev_attr_rumble_mode_right.attr,
	&dev_attr_rumble_mode_right_index.attr,
	&dev_attr_rumble_notification_right.attr,
	&dev_attr_rumble_notification_right_index.attr,
	&dev_attr_version_hardware_right.attr,
	&dev_attr_version_firmware_right.attr,
	&dev_attr_version_gen_right.attr,
	&dev_attr_version_product_right.attr,
	&dev_attr_version_protocol_right.attr,
	NULL,
};

static const struct attribute_group right_gamepad_attr_group = {
	.name = "right_handle",
	.attrs = right_gamepad_attrs,
};

/* Touchpad */
static struct go_cfg_attr touchpad_enabled = { FEATURE_TOUCHPAD_ENABLE };
LEGO_DEVICE_ATTR_RW(touchpad_enabled, "enabled", UNSPECIFIED, index,
		    feature_status);
static DEVICE_ATTR_RO_NAMED(touchpad_enabled_index, "enabled_index");

static struct go_cfg_attr touchpad_vibration_enabled = { TP_VIBRATION_ENABLE };
LEGO_DEVICE_ATTR_RW(touchpad_vibration_enabled, "vibration_enabled", UNSPECIFIED,
		    index, motor_config);
static DEVICE_ATTR_RO_NAMED(touchpad_vibration_enabled_index,
			    "vibration_enabled_index");

static struct go_cfg_attr touchpad_vibration_intensity = { TP_VIBRATION_INTENSITY };
LEGO_DEVICE_ATTR_RW(touchpad_vibration_intensity, "vibration_intensity",
		    UNSPECIFIED, index, motor_config);
static DEVICE_ATTR_RO_NAMED(touchpad_vibration_intensity_index,
			    "vibration_intensity_index");

static struct attribute *touchpad_attrs[] = {
	&dev_attr_touchpad_enabled.attr,
	&dev_attr_touchpad_enabled_index.attr,
	&dev_attr_touchpad_vibration_enabled.attr,
	&dev_attr_touchpad_vibration_enabled_index.attr,
	&dev_attr_touchpad_vibration_intensity.attr,
	&dev_attr_touchpad_vibration_intensity_index.attr,
	NULL,
};

static const struct attribute_group touchpad_attr_group = {
	.name = "touchpad",
	.attrs = touchpad_attrs,
};

static const struct attribute_group *top_level_attr_groups[] = {
	&mcu_attr_group,	  &tx_dongle_attr_group,
	&left_gamepad_attr_group, &right_gamepad_attr_group,
	&touchpad_attr_group,	  NULL,
};

/* RGB */
static struct go_cfg_attr rgb_enabled = { FEATURE_LIGHT_ENABLE };

LEGO_DEVICE_ATTR_RW(rgb_enabled, "enabled", UNSPECIFIED, index, feature_status);
static DEVICE_ATTR_RO_NAMED(rgb_effect_index, "effect_index");
static DEVICE_ATTR_RO_NAMED(rgb_enabled_index, "enabled_index");
static DEVICE_ATTR_RO_NAMED(rgb_mode_index, "mode_index");
static DEVICE_ATTR_RO_NAMED(rgb_profile_range, "profile_range");
static DEVICE_ATTR_RO_NAMED(rgb_speed_range, "speed_range");
static DEVICE_ATTR_RW_NAMED(rgb_effect, "effect");
static DEVICE_ATTR_RW_NAMED(rgb_mode, "mode");
static DEVICE_ATTR_RW_NAMED(rgb_profile, "profile");
static DEVICE_ATTR_RW_NAMED(rgb_speed, "speed");

static struct attribute *go_rgb_attrs[] = {
	&dev_attr_rgb_effect.attr,
	&dev_attr_rgb_effect_index.attr,
	&dev_attr_rgb_enabled.attr,
	&dev_attr_rgb_enabled_index.attr,
	&dev_attr_rgb_mode.attr,
	&dev_attr_rgb_mode_index.attr,
	&dev_attr_rgb_profile.attr,
	&dev_attr_rgb_profile_range.attr,
	&dev_attr_rgb_speed.attr,
	&dev_attr_rgb_speed_range.attr,
	NULL,
};

static struct attribute_group rgb_attr_group = {
	.attrs = go_rgb_attrs,
};

static struct mc_subled go_rgb_subled_info[] = {
	{
		.color_index = LED_COLOR_ID_RED,
		.brightness = 0x50,
		.intensity = 0x24,
		.channel = 0x1,
	},
	{
		.color_index = LED_COLOR_ID_GREEN,
		.brightness = 0x50,
		.intensity = 0x22,
		.channel = 0x2,
	},
	{
		.color_index = LED_COLOR_ID_BLUE,
		.brightness = 0x50,
		.intensity = 0x99,
		.channel = 0x3,
	},
};

static struct led_classdev_mc go_cdev_rgb = {
	.led_cdev = {
		.name = "go:rgb:joystick_rings",
		.color = LED_COLOR_ID_RGB,
		.brightness = 0x50,
		.max_brightness = 0x64,
		.brightness_set = hid_go_brightness_set,
	},
	.num_colors = ARRAY_SIZE(go_rgb_subled_info),
	.subled_info = go_rgb_subled_info,
};

static void cfg_setup(struct work_struct *work)
{
	int ret;

	/* MCU Version Attrs */
	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PRODUCT_VERSION, USB_MCU, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve USB_MCU Product Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PROTOCOL_VERSION, USB_MCU, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve USB_MCU Protocol Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       FIRMWARE_VERSION, USB_MCU, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve USB_MCU Firmware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_VERSION, USB_MCU, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve USB_MCU Hardware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_GENERATION, USB_MCU, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve USB_MCU Hardware Generation: %i\n", ret);
		return;
	}

	/* TX Dongle Version Attrs */
	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PRODUCT_VERSION, TX_DONGLE, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve TX_DONGLE Product Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PROTOCOL_VERSION, TX_DONGLE, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve TX_DONGLE Protocol Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       FIRMWARE_VERSION, TX_DONGLE, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve TX_DONGLE Firmware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_VERSION, TX_DONGLE, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve TX_DONGLE Hardware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_GENERATION, TX_DONGLE, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve TX_DONGLE Hardware Generation: %i\n", ret);
		return;
	}

	/* Left Handle Version Attrs */
	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PRODUCT_VERSION, LEFT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve LEFT_CONTROLLER Product Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PROTOCOL_VERSION, LEFT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve LEFT_CONTROLLER Protocol Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       FIRMWARE_VERSION, LEFT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve LEFT_CONTROLLER Firmware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_VERSION, LEFT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve LEFT_CONTROLLER Hardware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_GENERATION, LEFT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve LEFT_CONTROLLER Hardware Generation: %i\n", ret);
		return;
	}

	/* Right Handle Version Attrs */
	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PRODUCT_VERSION, RIGHT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve RIGHT_CONTROLLER Product Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       PROTOCOL_VERSION, RIGHT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve RIGHT_CONTROLLER Protocol Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       FIRMWARE_VERSION, RIGHT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve RIGHT_CONTROLLER Firmware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_VERSION, RIGHT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve RIGHT_CONTROLLER Hardware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, MCU_CONFIG_DATA, GET_VERSION_DATA,
			       HARDWARE_GENERATION, RIGHT_CONTROLLER, NULL, 0);
	if (ret < 0) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve RIGHT_CONTROLLER Hardware Generation: %i\n", ret);
		return;
	}
}

static int hid_go_cfg_probe(struct hid_device *hdev,
			    const struct hid_device_id *_id)
{
	unsigned char *buf;
	int ret;

	buf = devm_kzalloc(&hdev->dev, GO_PACKET_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	hid_set_drvdata(hdev, &drvdata);
	drvdata.hdev = hdev;
	mutex_init(&drvdata.cfg_mutex);

	ret = sysfs_create_groups(&hdev->dev.kobj, top_level_attr_groups);
	if (ret) {
		dev_err_probe(&hdev->dev, ret,
			      "Failed to create gamepad configuration attributes\n");
		return ret;
	}

	ret = devm_led_classdev_multicolor_register(&hdev->dev, &go_cdev_rgb);
	if (ret) {
		dev_err_probe(&hdev->dev, ret, "Failed to create RGB device\n");
		return ret;
	}

	ret = devm_device_add_group(go_cdev_rgb.led_cdev.dev, &rgb_attr_group);
	if (ret) {
		dev_err_probe(&hdev->dev, ret,
			      "Failed to create RGB configuration attributes\n");
		return ret;
	}

	drvdata.led_cdev = &go_cdev_rgb.led_cdev;

	init_completion(&drvdata.send_cmd_complete);

	/* Executing calls prior to returning from probe will lock the MCU. Schedule
	 * initial data call after probe has completed and MCU can accept calls.
	 */
	INIT_DELAYED_WORK(&drvdata.go_cfg_setup, &cfg_setup);
	ret = schedule_delayed_work(&drvdata.go_cfg_setup, msecs_to_jiffies(2));
	if (!ret) {
		dev_err(&hdev->dev,
			"Failed to schedule startup delayed work\n");
		return -ENODEV;
	}
	return 0;
}

static void hid_go_cfg_remove(struct hid_device *hdev)
{
	guard(mutex)(&drvdata.cfg_mutex);
	sysfs_remove_groups(&hdev->dev.kobj, top_level_attr_groups);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	hid_set_drvdata(hdev, NULL);
}

static int hid_go_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret, ep;

	hdev->quirks |= HID_QUIRK_INPUT_PER_APP | HID_QUIRK_MULTI_INPUT;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "Failed to start HID device\n");
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "Failed to open HID device\n");
		hid_hw_stop(hdev);
		return ret;
	}

	ep = get_endpoint_address(hdev);
	if (ep != GO_GP_INTF_IN) {
		dev_dbg(&hdev->dev, "Started interface %x as generic HID device\n", ep);
		return 0;
	}

	ret = hid_go_cfg_probe(hdev, id);
	if (ret)
		dev_err_probe(&hdev->dev, ret, "Failed to start configuration interface\n");

	dev_dbg(&hdev->dev, "Started Legion Go HID Device: %x\n", ep);

	return ret;
}

static void hid_go_remove(struct hid_device *hdev)
{
	int ep = get_endpoint_address(hdev);

	if (ep <= 0)
		return;

	switch (ep) {
	case GO_GP_INTF_IN:
		hid_go_cfg_remove(hdev);
		break;
	default:
		hid_hw_close(hdev);
		hid_hw_stop(hdev);
		break;
	}
}

static const struct hid_device_id hid_go_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO,
			 USB_DEVICE_ID_LENOVO_LEGION_GO2_XINPUT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO,
			 USB_DEVICE_ID_LENOVO_LEGION_GO2_DINPUT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO,
			 USB_DEVICE_ID_LENOVO_LEGION_GO2_DUAL_DINPUT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO,
			 USB_DEVICE_ID_LENOVO_LEGION_GO2_FPS) },
	{}
};
MODULE_DEVICE_TABLE(hid, hid_go_devices);

static struct hid_driver hid_lenovo_go = {
	.name = "hid-lenovo-go",
	.id_table = hid_go_devices,
	.probe = hid_go_probe,
	.remove = hid_go_remove,
	.raw_event = hid_go_raw_event,
};
module_hid_driver(hid_lenovo_go);

MODULE_AUTHOR("Derek J. Clark");
MODULE_DESCRIPTION("HID Driver for Lenovo Legion Go Series Gamepads.");
MODULE_LICENSE("GPL");

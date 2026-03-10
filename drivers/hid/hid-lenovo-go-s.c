// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Lenovo Legion Go S devices.
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
#include <linux/hid.h>
#include <linux/jiffies.h>
#include <linux/kstrtox.h>
#include <linux/led-class-multicolor.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/workqueue_types.h>

#include "hid-ids.h"

#define GO_S_CFG_INTF_IN	0x84
#define GO_S_PACKET_SIZE	64

static struct hid_gos_cfg {
	struct delayed_work gos_cfg_setup;
	struct completion send_cmd_complete;
	struct led_classdev *led_cdev;
	struct hid_device *hdev;
	struct mutex cfg_mutex; /*ensure single synchronous output report*/
	u8 gp_auto_sleep_time;
	u8 gp_dpad_mode;
	u8 gp_mode;
	u8 gp_poll_rate;
	u8 imu_bypass_en;
	u8 imu_manufacturer;
	u8 imu_sensor_en;
	u8 mcu_id[12];
	u8 mouse_step;
	u8 os_mode;
	u8 rgb_effect;
	u8 rgb_en;
	u8 rgb_mode;
	u8 rgb_profile;
	u8 rgb_speed;
	u8 tp_en;
	u8 tp_linux_mode;
	u8 tp_windows_mode;
	u8 tp_version;
	u8 tp_manufacturer;
} drvdata;

struct gos_cfg_attr {
	u8 index;
};

struct command_report {
	u8 cmd;
	u8 sub_cmd;
	u8 data[63];
} __packed;

struct version_report {
	u8 cmd;
	u32 version;
	u8 reserved[59];
} __packed;

enum mcu_command_index {
	GET_VERSION = 0x01,
	GET_MCU_ID,
	GET_GAMEPAD_CFG,
	SET_GAMEPAD_CFG,
	GET_TP_PARAM,
	SET_TP_PARAM,
	GET_RGB_CFG = 0x0f,
	SET_RGB_CFG,
	GET_PL_TEST = 0xdf,
};

enum feature_enabled_index {
	FEATURE_DISABLED,
	FEATURE_ENABLED,
};

static const char *const feature_enabled_text[] = {
	[FEATURE_DISABLED] = "false",
	[FEATURE_ENABLED] = "true",
};

enum feature_status_index {
	FEATURE_NONE = 0x00,
	FEATURE_GAMEPAD_MODE = 0x01,
	FEATURE_AUTO_SLEEP_TIME = 0x04,
	FEATURE_IMU_BYPASS,
	FEATURE_RGB_ENABLE,
	FEATURE_IMU_ENABLE,
	FEATURE_TOUCHPAD_ENABLE,
	FEATURE_OS_MODE = 0x0A,
	FEATURE_POLL_RATE = 0x10,
	FEATURE_DPAD_MODE,
	FEATURE_MOUSE_WHEEL_STEP,
};

enum gamepad_mode_index {
	XINPUT,
	DINPUT,
};

static const char *const gamepad_mode_text[] = {
	[XINPUT] = "xinput",
	[DINPUT] = "dinput",
};

enum os_type_index {
	WINDOWS,
	LINUX,
};

static const char *const os_type_text[] = {
	[WINDOWS] = "windows",
	[LINUX] = "linux",
};

enum poll_rate_index {
	HZ125,
	HZ250,
	HZ500,
	HZ1000,
};

static const char *const poll_rate_text[] = {
	[HZ125] = "125",
	[HZ250] = "250",
	[HZ500] = "500",
	[HZ1000] = "1000",
};

enum dpad_mode_index {
	DIR8,
	DIR4,
};

static const char *const dpad_mode_text[] = {
	[DIR8] = "8-way",
	[DIR4] = "4-way",
};

enum touchpad_mode_index {
	TP_REL,
	TP_ABS,
};

static const char *const touchpad_mode_text[] = {
	[TP_REL] = "relative",
	[TP_ABS] = "absolute",
};

enum touchpad_config_index {
	CFG_WINDOWS_MODE = 0x03,
	CFG_LINUX_MODE,

};

enum rgb_mode_index {
	RGB_MODE_DYNAMIC,
	RGB_MODE_CUSTOM,
};

static const char *const rgb_mode_text[] = {
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

enum rgb_config_index {
	LIGHT_MODE_SEL = 0x01,
	LIGHT_PROFILE_SEL,
	USR_LIGHT_PROFILE_1,
	USR_LIGHT_PROFILE_2,
	USR_LIGHT_PROFILE_3,
};

enum test_command_index {
	TEST_TP_MFR = 0x02,
	TEST_IMU_MFR,
	TEST_TP_VER,
};

enum tp_mfr_index {
	TP_NONE,
	TP_BETTERLIFE,
	TP_SIPO,
};

static const char *const touchpad_manufacturer_text[] = {
	[TP_NONE] = "none",
	[TP_BETTERLIFE] = "BetterLife",
	[TP_SIPO] = "SIPO",
};

enum imu_mfr_index {
	IMU_NONE,
	IMU_BOSCH,
	IMU_ST,
};

static const char *const imu_manufacturer_text[] = {
	[IMU_NONE] = "none",
	[IMU_BOSCH] = "Bosch",
	[IMU_ST] = "ST",
};

static int hid_gos_version_event(u8 *data)
{
	struct version_report *ver_rep = (struct version_report *)data;

	drvdata.hdev->firmware_version = get_unaligned_le32(&ver_rep->version);
	return 0;
}

static int hid_gos_mcu_id_event(struct command_report *cmd_rep)
{
	drvdata.mcu_id[0] = cmd_rep->sub_cmd;
	memcpy(&drvdata.mcu_id[1], cmd_rep->data, 11);

	return 0;
}

static int hid_gos_gamepad_cfg_event(struct command_report *cmd_rep)
{
	int ret = 0;

	switch (cmd_rep->sub_cmd) {
	case FEATURE_GAMEPAD_MODE:
		drvdata.gp_mode = cmd_rep->data[0];
		break;
	case FEATURE_AUTO_SLEEP_TIME:
		drvdata.gp_auto_sleep_time = cmd_rep->data[0];
		break;
	case FEATURE_IMU_BYPASS:
		drvdata.imu_bypass_en = cmd_rep->data[0];
		break;
	case FEATURE_RGB_ENABLE:
		drvdata.rgb_en = cmd_rep->data[0];
		break;
	case FEATURE_IMU_ENABLE:
		drvdata.imu_sensor_en = cmd_rep->data[0];
		break;
	case FEATURE_TOUCHPAD_ENABLE:
		drvdata.tp_en = cmd_rep->data[0];
		break;
	case FEATURE_OS_MODE:
		drvdata.os_mode = cmd_rep->data[0];
		break;
	case FEATURE_POLL_RATE:
		drvdata.gp_poll_rate = cmd_rep->data[0];
		break;
	case FEATURE_DPAD_MODE:
		drvdata.gp_dpad_mode = cmd_rep->data[0];
		break;
	case FEATURE_MOUSE_WHEEL_STEP:
		drvdata.mouse_step = cmd_rep->data[0];
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int hid_gos_touchpad_event(struct command_report *cmd_rep)
{
	int ret = 0;

	switch (cmd_rep->sub_cmd) {
	case CFG_LINUX_MODE:
		drvdata.tp_linux_mode = cmd_rep->data[0];
		break;
	case CFG_WINDOWS_MODE:
		drvdata.tp_windows_mode = cmd_rep->data[0];
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int hid_gos_pl_test_event(struct command_report *cmd_rep)
{
	int ret = 0;

	switch (cmd_rep->sub_cmd) {
	case TEST_TP_MFR:
		drvdata.tp_manufacturer = cmd_rep->data[0];
		ret = 0;
		break;
	case TEST_IMU_MFR:
		drvdata.imu_manufacturer = cmd_rep->data[0];
		ret = 0;
		break;
	case TEST_TP_VER:
		drvdata.tp_version = cmd_rep->data[0];
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int hid_gos_light_event(struct command_report *cmd_rep)
{
	struct led_classdev_mc *mc_cdev;
	int ret = 0;

	switch (cmd_rep->sub_cmd) {
	case LIGHT_MODE_SEL:
		drvdata.rgb_mode = cmd_rep->data[0];
		ret = 0;
		break;
	case LIGHT_PROFILE_SEL:
		drvdata.rgb_profile = cmd_rep->data[0];
		ret = 0;
		break;
	case USR_LIGHT_PROFILE_1:
	case USR_LIGHT_PROFILE_2:
	case USR_LIGHT_PROFILE_3:
		mc_cdev = lcdev_to_mccdev(drvdata.led_cdev);
		drvdata.rgb_effect = cmd_rep->data[0];
		mc_cdev->subled_info[0].intensity = cmd_rep->data[1];
		mc_cdev->subled_info[1].intensity = cmd_rep->data[2];
		mc_cdev->subled_info[2].intensity = cmd_rep->data[3];
		drvdata.led_cdev->brightness = cmd_rep->data[4];
		drvdata.rgb_speed = cmd_rep->data[5];
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int hid_gos_set_event_return(struct command_report *cmd_rep)
{
	if (cmd_rep->data[0] != 0)
		return -EIO;

	return 0;
}

static int get_endpoint_address(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_host_endpoint *ep;

	if (intf) {
		ep = intf->cur_altsetting->endpoint;
		if (ep)
			return ep->desc.bEndpointAddress;
	}

	return -ENODEV;
}

static int hid_gos_raw_event(struct hid_device *hdev, struct hid_report *report,
			     u8 *data, int size)
{
	struct command_report *cmd_rep;
	int ep, ret;

	ep = get_endpoint_address(hdev);
	if (ep != GO_S_CFG_INTF_IN)
		return 0;

	if (size != GO_S_PACKET_SIZE)
		return -EINVAL;

	cmd_rep = (struct command_report *)data;

	switch (cmd_rep->cmd) {
	case GET_VERSION:
		ret = hid_gos_version_event(data);
		break;
	case GET_MCU_ID:
		ret = hid_gos_mcu_id_event(cmd_rep);
		break;
	case GET_GAMEPAD_CFG:
		ret = hid_gos_gamepad_cfg_event(cmd_rep);
		break;
	case GET_TP_PARAM:
		ret = hid_gos_touchpad_event(cmd_rep);
		break;
	case GET_PL_TEST:
		ret = hid_gos_pl_test_event(cmd_rep);
		break;
	case GET_RGB_CFG:
		ret = hid_gos_light_event(cmd_rep);
		break;
	case SET_GAMEPAD_CFG:
	case SET_RGB_CFG:
	case SET_TP_PARAM:
		ret = hid_gos_set_event_return(cmd_rep);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	dev_dbg(&hdev->dev, "Rx data as raw input report: [%*ph]\n",
		GO_S_PACKET_SIZE, data);

	complete(&drvdata.send_cmd_complete);
	return ret;
}

static int mcu_property_out(struct hid_device *hdev, u8 command, u8 index,
			    u8 *data, size_t len)
{
	unsigned char *dmabuf __free(kfree) = NULL;
	u8 header[] = { command, index };
	size_t header_size = ARRAY_SIZE(header);
	int timeout, ret;

	if (header_size + len > GO_S_PACKET_SIZE)
		return -EINVAL;

	guard(mutex)(&drvdata.cfg_mutex);
	/* We can't use a devm_alloc reusable buffer without side effects during suspend */
	dmabuf = kzalloc(GO_S_PACKET_SIZE, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	memcpy(dmabuf, header, header_size);
	memcpy(dmabuf + header_size, data, len);

	dev_dbg(&hdev->dev, "Send data as raw output report: [%*ph]\n",
		GO_S_PACKET_SIZE, dmabuf);

	ret = hid_hw_output_report(hdev, dmabuf, GO_S_PACKET_SIZE);
	if (ret < 0)
		return ret;

	ret = ret == GO_S_PACKET_SIZE ? 0 : -EINVAL;
	if (ret)
		return ret;

	/* PL_TEST commands can take longer because they go out to another device */
	timeout = (command == GET_PL_TEST) ? 200 : 5;
	ret = wait_for_completion_interruptible_timeout(&drvdata.send_cmd_complete,
							msecs_to_jiffies(timeout));

	if (ret == 0) /* timeout occurred */
		ret = -EBUSY;

	reinit_completion(&drvdata.send_cmd_complete);
	return 0;
}

static ssize_t gamepad_property_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count,
				      enum feature_status_index index)
{
	size_t size = 1;
	u8 val = 0;
	int ret;

	switch (index) {
	case FEATURE_GAMEPAD_MODE:
		ret = sysfs_match_string(gamepad_mode_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_AUTO_SLEEP_TIME:
		ret = kstrtou8(buf, 10, &val);
		if (ret)
			return ret;
		break;
	case FEATURE_IMU_ENABLE:
		ret = sysfs_match_string(feature_enabled_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_IMU_BYPASS:
		ret = sysfs_match_string(feature_enabled_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_RGB_ENABLE:
		ret = sysfs_match_string(feature_enabled_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_TOUCHPAD_ENABLE:
		ret = sysfs_match_string(feature_enabled_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_OS_MODE:
		ret = sysfs_match_string(os_type_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_POLL_RATE:
		ret = sysfs_match_string(poll_rate_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_DPAD_MODE:
		ret = sysfs_match_string(dpad_mode_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case FEATURE_MOUSE_WHEEL_STEP:
		ret = kstrtou8(buf, 10, &val);
		if (ret)
			return ret;
		if (val < 1 || val > 127)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (!val)
		size = 0;

	ret = mcu_property_out(drvdata.hdev, SET_GAMEPAD_CFG, index, &val,
			       size);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t gamepad_property_show(struct device *dev,
				     struct device_attribute *attr, char *buf,
				     enum feature_status_index index)
{
	ssize_t count = 0;
	u8 i;

	count = mcu_property_out(drvdata.hdev, GET_GAMEPAD_CFG, index, NULL, 0);
	if (count < 0)
		return count;

	switch (index) {
	case FEATURE_GAMEPAD_MODE:
		i = drvdata.gp_mode;
		if (i >= ARRAY_SIZE(gamepad_mode_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", gamepad_mode_text[i]);
		break;
	case FEATURE_AUTO_SLEEP_TIME:
		count = sysfs_emit(buf, "%u\n", drvdata.gp_auto_sleep_time);
		break;
	case FEATURE_IMU_ENABLE:
		i = drvdata.imu_sensor_en;
		if (i >= ARRAY_SIZE(feature_enabled_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", feature_enabled_text[i]);
		break;
	case FEATURE_IMU_BYPASS:
		i = drvdata.imu_bypass_en;
		if (i >= ARRAY_SIZE(feature_enabled_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", feature_enabled_text[i]);
		break;
	case FEATURE_RGB_ENABLE:
		i = drvdata.rgb_en;
		if (i >= ARRAY_SIZE(feature_enabled_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", feature_enabled_text[i]);
		break;
	case FEATURE_TOUCHPAD_ENABLE:
		i = drvdata.tp_en;
		if (i >= ARRAY_SIZE(feature_enabled_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", feature_enabled_text[i]);
		break;
	case FEATURE_OS_MODE:
		i = drvdata.os_mode;
		if (i >= ARRAY_SIZE(os_type_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", os_type_text[i]);
		break;
	case FEATURE_POLL_RATE:
		i = drvdata.gp_poll_rate;
		if (i >= ARRAY_SIZE(poll_rate_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", poll_rate_text[i]);
		break;
	case FEATURE_DPAD_MODE:
		i = drvdata.gp_dpad_mode;
		if (i >= ARRAY_SIZE(dpad_mode_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", dpad_mode_text[i]);
		break;
	case FEATURE_MOUSE_WHEEL_STEP:
		i = drvdata.mouse_step;
		if (i < 1 || i > 127)
			return -EINVAL;
		count = sysfs_emit(buf, "%u\n", i);
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static ssize_t gamepad_property_options(struct device *dev,
					struct device_attribute *attr,
					char *buf,
					enum feature_status_index index)
{
	size_t count = 0;
	unsigned int i;

	switch (index) {
	case FEATURE_GAMEPAD_MODE:
		for (i = 0; i < ARRAY_SIZE(gamepad_mode_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       gamepad_mode_text[i]);
		}
		break;
	case FEATURE_AUTO_SLEEP_TIME:
		return sysfs_emit(buf, "0-255\n");
	case FEATURE_IMU_ENABLE:
		for (i = 0; i < ARRAY_SIZE(feature_enabled_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       feature_enabled_text[i]);
		}
		break;
	case FEATURE_IMU_BYPASS:
	case FEATURE_RGB_ENABLE:
	case FEATURE_TOUCHPAD_ENABLE:
		for (i = 0; i < ARRAY_SIZE(feature_enabled_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       feature_enabled_text[i]);
		}
		break;
	case FEATURE_OS_MODE:
		for (i = 0; i < ARRAY_SIZE(os_type_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       os_type_text[i]);
		}
		break;
	case FEATURE_POLL_RATE:
		for (i = 0; i < ARRAY_SIZE(poll_rate_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       poll_rate_text[i]);
		}
		break;
	case FEATURE_DPAD_MODE:
		for (i = 0; i < ARRAY_SIZE(dpad_mode_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       dpad_mode_text[i]);
		}
		break;
	case FEATURE_MOUSE_WHEEL_STEP:
		return sysfs_emit(buf, "1-127\n");
	default:
		return count;
	}

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t touchpad_property_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count,
				       enum touchpad_config_index index)
{
	size_t size = 1;
	u8 val = 0;
	int ret;

	switch (index) {
	case CFG_WINDOWS_MODE:
		ret = sysfs_match_string(touchpad_mode_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	case CFG_LINUX_MODE:
		ret = sysfs_match_string(touchpad_mode_text, buf);
		if (ret < 0)
			return ret;
		val = ret;
		break;
	default:
		return -EINVAL;
	}
	if (!val)
		size = 0;

	ret = mcu_property_out(drvdata.hdev, SET_TP_PARAM, index, &val, size);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t touchpad_property_show(struct device *dev,
				      struct device_attribute *attr, char *buf,
				      enum touchpad_config_index index)
{
	int ret = 0;
	u8 i;

	ret = mcu_property_out(drvdata.hdev, GET_TP_PARAM, index, NULL, 0);
	if (ret < 0)
		return ret;

	switch (index) {
	case CFG_WINDOWS_MODE:
		i = drvdata.tp_windows_mode;
		break;
	case CFG_LINUX_MODE:
		i = drvdata.tp_linux_mode;
		break;
	default:
		return -EINVAL;
	}

	if (i >= ARRAY_SIZE(touchpad_mode_text))
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", touchpad_mode_text[i]);
}

static ssize_t touchpad_property_options(struct device *dev,
					 struct device_attribute *attr,
					 char *buf,
					 enum touchpad_config_index index)
{
	size_t count = 0;
	unsigned int i;

	switch (index) {
	case CFG_WINDOWS_MODE:
	case CFG_LINUX_MODE:
		for (i = 0; i < ARRAY_SIZE(touchpad_mode_text); i++) {
			count += sysfs_emit_at(buf, count, "%s ",
					       touchpad_mode_text[i]);
		}
		break;
	default:
		return count;
	}

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t test_property_show(struct device *dev,
				  struct device_attribute *attr, char *buf,
				  enum test_command_index index)
{
	size_t count = 0;
	u8 i;

	switch (index) {
	case TEST_TP_MFR:
		i = drvdata.tp_manufacturer;
		if (i >= ARRAY_SIZE(touchpad_manufacturer_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", touchpad_manufacturer_text[i]);
		break;
	case TEST_IMU_MFR:
		i = drvdata.imu_manufacturer;
		if (i >= ARRAY_SIZE(imu_manufacturer_text))
			return -EINVAL;
		count = sysfs_emit(buf, "%s\n", imu_manufacturer_text[i]);
		break;
	case TEST_TP_VER:
		count = sysfs_emit(buf, "%u\n", drvdata.tp_version);
		break;
	default:
		count = -EINVAL;
		break;
	}

	return count;
}

static ssize_t mcu_id_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return sysfs_emit(buf, "%*phN\n", 12, &drvdata.mcu_id);
}

static int rgb_cfg_call(struct hid_device *hdev, enum mcu_command_index cmd,
			enum rgb_config_index index, u8 *val, size_t size)
{
	if (cmd != SET_RGB_CFG && cmd != GET_RGB_CFG)
		return -EINVAL;

	if (index < LIGHT_MODE_SEL || index > USR_LIGHT_PROFILE_3)
		return -EINVAL;

	return mcu_property_out(hdev, cmd, index, val, size);
}

static int rgb_attr_show(void)
{
	enum rgb_config_index index;

	index = drvdata.rgb_profile + 2;

	return rgb_cfg_call(drvdata.hdev, GET_RGB_CFG, index, NULL, 0);
};

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
	index = drvdata.rgb_profile + 2;
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
};

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

	index = drvdata.rgb_profile + 2;
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
};

static ssize_t rgb_speed_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	int ret;

	ret = rgb_attr_show();
	if (ret)
		return ret;

	if (drvdata.rgb_speed > 100)
		return -EINVAL;

	return sysfs_emit(buf, "%hhu\n", drvdata.rgb_speed);
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

	ret = rgb_cfg_call(drvdata.hdev, SET_RGB_CFG, LIGHT_MODE_SEL, &val,
			   1);
	if (ret)
		return ret;

	drvdata.rgb_mode = val;

	return count;
};

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
};

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
};

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
};

static ssize_t rgb_profile_range_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "1-3\n");
}

static void hid_gos_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(drvdata.led_cdev);
	enum rgb_config_index index;
	int ret;

	if (brightness > led_cdev->max_brightness) {
		dev_err(led_cdev->dev, "Invalid argument\n");
		return;
	}

	index = drvdata.rgb_profile + 2;
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
		dev_dbg(led_cdev->dev, "Failed to write RGB profile: %i\n",
			ret);
		break;
	default:
		dev_err(led_cdev->dev, "Failed to write RGB profile: %i\n",
			ret);
	}
}

#define LEGOS_DEVICE_ATTR_RW(_name, _attrname, _rtype, _group)                 \
	static ssize_t _name##_store(struct device *dev,                       \
				     struct device_attribute *attr,            \
				     const char *buf, size_t count)            \
	{                                                                      \
		return _group##_property_store(dev, attr, buf, count,          \
					       _name.index);                   \
	}                                                                      \
	static ssize_t _name##_show(struct device *dev,                        \
				    struct device_attribute *attr, char *buf)  \
	{                                                                      \
		return _group##_property_show(dev, attr, buf, _name.index);    \
	}                                                                      \
	static ssize_t _name##_##_rtype##_show(                                \
		struct device *dev, struct device_attribute *attr, char *buf)  \
	{                                                                      \
		return _group##_property_options(dev, attr, buf, _name.index); \
	}                                                                      \
	static DEVICE_ATTR_RW_NAMED(_name, _attrname)

#define LEGOS_DEVICE_ATTR_RO(_name, _attrname, _group)                        \
	static ssize_t _name##_show(struct device *dev,                       \
				    struct device_attribute *attr, char *buf) \
	{                                                                     \
		return _group##_property_show(dev, attr, buf, _name.index);   \
	}                                                                     \
	static DEVICE_ATTR_RO_NAMED(_name, _attrname)

/* Gamepad */
static struct gos_cfg_attr auto_sleep_time = { FEATURE_AUTO_SLEEP_TIME };
LEGOS_DEVICE_ATTR_RW(auto_sleep_time, "auto_sleep_time", range, gamepad);
static DEVICE_ATTR_RO(auto_sleep_time_range);

static struct gos_cfg_attr dpad_mode = { FEATURE_DPAD_MODE };
LEGOS_DEVICE_ATTR_RW(dpad_mode, "dpad_mode", index, gamepad);
static DEVICE_ATTR_RO(dpad_mode_index);

static struct gos_cfg_attr gamepad_mode = { FEATURE_GAMEPAD_MODE };
LEGOS_DEVICE_ATTR_RW(gamepad_mode, "mode", index, gamepad);
static DEVICE_ATTR_RO_NAMED(gamepad_mode_index, "mode_index");

static struct gos_cfg_attr gamepad_poll_rate = { FEATURE_POLL_RATE };
LEGOS_DEVICE_ATTR_RW(gamepad_poll_rate, "poll_rate", index, gamepad);
static DEVICE_ATTR_RO_NAMED(gamepad_poll_rate_index, "poll_rate_index");

static struct attribute *legos_gamepad_attrs[] = {
	&dev_attr_auto_sleep_time.attr,
	&dev_attr_auto_sleep_time_range.attr,
	&dev_attr_dpad_mode.attr,
	&dev_attr_dpad_mode_index.attr,
	&dev_attr_gamepad_mode.attr,
	&dev_attr_gamepad_mode_index.attr,
	&dev_attr_gamepad_poll_rate.attr,
	&dev_attr_gamepad_poll_rate_index.attr,
	NULL,
};

static const struct attribute_group gamepad_attr_group = {
	.name = "gamepad",
	.attrs = legos_gamepad_attrs,
};

/* IMU */
static struct gos_cfg_attr imu_bypass_enabled = { FEATURE_IMU_BYPASS };
LEGOS_DEVICE_ATTR_RW(imu_bypass_enabled, "bypass_enabled", index, gamepad);
static DEVICE_ATTR_RO_NAMED(imu_bypass_enabled_index, "bypass_enabled_index");

static struct gos_cfg_attr imu_manufacturer = { TEST_IMU_MFR };
LEGOS_DEVICE_ATTR_RO(imu_manufacturer, "manufacturer", test);

static struct gos_cfg_attr imu_sensor_enabled = { FEATURE_IMU_ENABLE };
LEGOS_DEVICE_ATTR_RW(imu_sensor_enabled, "sensor_enabled", index, gamepad);
static DEVICE_ATTR_RO_NAMED(imu_sensor_enabled_index, "sensor_enabled_index");

static struct attribute *legos_imu_attrs[] = {
	&dev_attr_imu_bypass_enabled.attr,
	&dev_attr_imu_bypass_enabled_index.attr,
	&dev_attr_imu_manufacturer.attr,
	&dev_attr_imu_sensor_enabled.attr,
	&dev_attr_imu_sensor_enabled_index.attr,
	NULL,
};

static const struct attribute_group imu_attr_group = {
	.name = "imu",
	.attrs = legos_imu_attrs,
};

/* MCU */
static DEVICE_ATTR_RO(mcu_id);

static struct gos_cfg_attr os_mode = { FEATURE_OS_MODE };
LEGOS_DEVICE_ATTR_RW(os_mode, "os_mode", index, gamepad);
static DEVICE_ATTR_RO(os_mode_index);

static struct attribute *legos_mcu_attrs[] = {
	&dev_attr_mcu_id.attr,
	&dev_attr_os_mode.attr,
	&dev_attr_os_mode_index.attr,
	NULL,
};

static const struct attribute_group mcu_attr_group = {
	.attrs = legos_mcu_attrs,
};

/* Mouse */
static struct gos_cfg_attr mouse_wheel_step = { FEATURE_MOUSE_WHEEL_STEP };
LEGOS_DEVICE_ATTR_RW(mouse_wheel_step, "step", range, gamepad);
static DEVICE_ATTR_RO_NAMED(mouse_wheel_step_range, "step_range");

static struct attribute *legos_mouse_attrs[] = {
	&dev_attr_mouse_wheel_step.attr,
	&dev_attr_mouse_wheel_step_range.attr,
	NULL,
};

static const struct attribute_group mouse_attr_group = {
	.name = "mouse",
	.attrs = legos_mouse_attrs,
};

/* Touchpad */
static struct gos_cfg_attr touchpad_enabled = { FEATURE_TOUCHPAD_ENABLE };
LEGOS_DEVICE_ATTR_RW(touchpad_enabled, "enabled", index, gamepad);
static DEVICE_ATTR_RO_NAMED(touchpad_enabled_index, "enabled_index");

static struct gos_cfg_attr touchpad_linux_mode = { CFG_LINUX_MODE };
LEGOS_DEVICE_ATTR_RW(touchpad_linux_mode, "linux_mode", index, touchpad);
static DEVICE_ATTR_RO_NAMED(touchpad_linux_mode_index, "linux_mode_index");

static struct gos_cfg_attr touchpad_manufacturer = { TEST_TP_MFR };
LEGOS_DEVICE_ATTR_RO(touchpad_manufacturer, "manufacturer", test);

static struct gos_cfg_attr touchpad_version = { TEST_TP_VER };
LEGOS_DEVICE_ATTR_RO(touchpad_version, "version", test);

static struct gos_cfg_attr touchpad_windows_mode = { CFG_WINDOWS_MODE };
LEGOS_DEVICE_ATTR_RW(touchpad_windows_mode, "windows_mode", index, touchpad);
static DEVICE_ATTR_RO_NAMED(touchpad_windows_mode_index, "windows_mode_index");

static struct attribute *legos_touchpad_attrs[] = {
	&dev_attr_touchpad_enabled.attr,
	&dev_attr_touchpad_enabled_index.attr,
	&dev_attr_touchpad_linux_mode.attr,
	&dev_attr_touchpad_linux_mode_index.attr,
	&dev_attr_touchpad_manufacturer.attr,
	&dev_attr_touchpad_version.attr,
	&dev_attr_touchpad_windows_mode.attr,
	&dev_attr_touchpad_windows_mode_index.attr,
	NULL,
};

static const struct attribute_group touchpad_attr_group = {
	.name = "touchpad",
	.attrs = legos_touchpad_attrs,
};

static const struct attribute_group *top_level_attr_groups[] = {
	&gamepad_attr_group,
	&imu_attr_group,
	&mcu_attr_group,
	&mouse_attr_group,
	&touchpad_attr_group,
	NULL,
};

/* RGB */
static struct gos_cfg_attr rgb_enabled = { FEATURE_RGB_ENABLE };
LEGOS_DEVICE_ATTR_RW(rgb_enabled, "enabled", index, gamepad);
static DEVICE_ATTR_RO_NAMED(rgb_enabled_index, "enabled_index");

static DEVICE_ATTR_RW_NAMED(rgb_effect, "effect");
static DEVICE_ATTR_RO_NAMED(rgb_effect_index, "effect_index");
static DEVICE_ATTR_RW_NAMED(rgb_mode, "mode");
static DEVICE_ATTR_RO_NAMED(rgb_mode_index, "mode_index");
static DEVICE_ATTR_RW_NAMED(rgb_profile, "profile");
static DEVICE_ATTR_RO_NAMED(rgb_profile_range, "profile_range");
static DEVICE_ATTR_RW_NAMED(rgb_speed, "speed");
static DEVICE_ATTR_RO_NAMED(rgb_speed_range, "speed_range");

static struct attribute *gos_rgb_attrs[] = {
	&dev_attr_rgb_enabled.attr,
	&dev_attr_rgb_enabled_index.attr,
	&dev_attr_rgb_effect.attr,
	&dev_attr_rgb_effect_index.attr,
	&dev_attr_rgb_mode.attr,
	&dev_attr_rgb_mode_index.attr,
	&dev_attr_rgb_profile.attr,
	&dev_attr_rgb_profile_range.attr,
	&dev_attr_rgb_speed.attr,
	&dev_attr_rgb_speed_range.attr,
	NULL,
};

static struct attribute_group rgb_attr_group = {
	.attrs = gos_rgb_attrs,
};

static struct mc_subled gos_rgb_subled_info[] = {
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

static struct led_classdev_mc gos_cdev_rgb = {
	.led_cdev = {
		.name = "go_s:rgb:joystick_rings",
		.brightness = 0x50,
		.max_brightness = 0x64,
		.brightness_set = hid_gos_brightness_set,
	},
	.num_colors = ARRAY_SIZE(gos_rgb_subled_info),
	.subled_info = gos_rgb_subled_info,
};

static void cfg_setup(struct work_struct *work)
{
	int ret;

	/* MCU */
	ret = mcu_property_out(drvdata.hdev, GET_MCU_ID, FEATURE_NONE, NULL, 0);
	if (ret) {
		dev_err(&drvdata.hdev->dev, "Failed to retrieve MCU ID: %i\n",
			ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, GET_VERSION, FEATURE_NONE, NULL, 0);
	if (ret) {
		dev_err(&drvdata.hdev->dev, "Failed to retrieve MCU Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, GET_PL_TEST, TEST_TP_MFR, NULL, 0);
	if (ret) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve Touchpad Manufacturer: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, GET_PL_TEST, TEST_TP_VER, NULL, 0);
	if (ret) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve Touchpad Firmware Version: %i\n", ret);
		return;
	}

	ret = mcu_property_out(drvdata.hdev, GET_PL_TEST, TEST_IMU_MFR, NULL, 0);
	if (ret) {
		dev_err(&drvdata.hdev->dev,
			"Failed to retrieve IMU Manufacturer: %i\n", ret);
		return;
	}
}

static int hid_gos_cfg_probe(struct hid_device *hdev,
			     const struct hid_device_id *_id)
{
	int ret;

	hid_set_drvdata(hdev, &drvdata);
	drvdata.hdev = hdev;
	mutex_init(&drvdata.cfg_mutex);

	ret = sysfs_create_groups(&hdev->dev.kobj, top_level_attr_groups);
	if (ret) {
		dev_err_probe(&hdev->dev, ret,
			      "Failed to create gamepad configuration attributes\n");
		return ret;
	}

	ret = devm_led_classdev_multicolor_register(&hdev->dev, &gos_cdev_rgb);
	if (ret) {
		dev_err_probe(&hdev->dev, ret, "Failed to create RGB device\n");
		return ret;
	}

	ret = devm_device_add_group(gos_cdev_rgb.led_cdev.dev, &rgb_attr_group);
	if (ret) {
		dev_err_probe(&hdev->dev, ret,
			      "Failed to create RGB configuration attributes\n");
		return ret;
	}

	drvdata.led_cdev = &gos_cdev_rgb.led_cdev;

	init_completion(&drvdata.send_cmd_complete);

	/* Executing calls prior to returning from probe will lock the MCU. Schedule
	 * initial data call after probe has completed and MCU can accept calls.
	 */
	INIT_DELAYED_WORK(&drvdata.gos_cfg_setup, &cfg_setup);
	ret = schedule_delayed_work(&drvdata.gos_cfg_setup, msecs_to_jiffies(2));
	if (!ret) {
		dev_err(&hdev->dev, "Failed to schedule startup delayed work\n");
		return -ENODEV;
	}

	return 0;
}

static void hid_gos_cfg_remove(struct hid_device *hdev)
{
	guard(mutex)(&drvdata.cfg_mutex);
	cancel_delayed_work_sync(&drvdata.gos_cfg_setup);
	sysfs_remove_groups(&hdev->dev.kobj, top_level_attr_groups);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	hid_set_drvdata(hdev, NULL);
}

static int hid_gos_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	int ret, ep;

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
		hid_hw_stop(hdev);
		return ret;
	}

	ep = get_endpoint_address(hdev);
	if (ep != GO_S_CFG_INTF_IN) {
		dev_dbg(&hdev->dev, "Started interface %x as generic HID device.\n", ep);
		return 0;
	}

	ret = hid_gos_cfg_probe(hdev, id);
	if (ret)
		dev_err_probe(&hdev->dev, ret, "Failed to start configuration interface");

	dev_dbg(&hdev->dev, "Started interface %x as Go S configuration interface\n", ep);
	return ret;
}

static void hid_gos_remove(struct hid_device *hdev)
{
	int ep = get_endpoint_address(hdev);

	switch (ep) {
	case GO_S_CFG_INTF_IN:
		hid_gos_cfg_remove(hdev);
		break;
	default:
		hid_hw_close(hdev);
		hid_hw_stop(hdev);

		break;
	}
}

static const struct hid_device_id hid_gos_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_QHE,
			 USB_DEVICE_ID_LENOVO_LEGION_GO_S_XINPUT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_QHE,
			 USB_DEVICE_ID_LENOVO_LEGION_GO_S_DINPUT) },
	{}
};

MODULE_DEVICE_TABLE(hid, hid_gos_devices);
static struct hid_driver hid_lenovo_go_s = {
	.name = "hid-lenovo-go-s",
	.id_table = hid_gos_devices,
	.probe = hid_gos_probe,
	.remove = hid_gos_remove,
	.raw_event = hid_gos_raw_event,
};
module_hid_driver(hid_lenovo_go_s);

MODULE_AUTHOR("Derek J. Clark");
MODULE_DESCRIPTION("HID Driver for Lenovo Legion Go S Series gamepad.");
MODULE_LICENSE("GPL");

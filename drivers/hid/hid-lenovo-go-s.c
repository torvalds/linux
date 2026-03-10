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
	struct hid_device *hdev;
	struct mutex cfg_mutex; /*ensure single synchronous output report*/
	u8 gp_auto_sleep_time;
	u8 gp_dpad_mode;
	u8 gp_mode;
	u8 gp_poll_rate;
	u8 imu_bypass_en;
	u8 imu_sensor_en;
	u8 mcu_id[12];
	u8 mouse_step;
	u8 os_mode;
	u8 rgb_en;
	u8 tp_en;
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
	case SET_GAMEPAD_CFG:
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

static ssize_t mcu_id_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return sysfs_emit(buf, "%*phN\n", 12, &drvdata.mcu_id);
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

static struct gos_cfg_attr imu_sensor_enabled = { FEATURE_IMU_ENABLE };
LEGOS_DEVICE_ATTR_RW(imu_sensor_enabled, "sensor_enabled", index, gamepad);
static DEVICE_ATTR_RO_NAMED(imu_sensor_enabled_index, "sensor_enabled_index");

static struct attribute *legos_imu_attrs[] = {
	&dev_attr_imu_bypass_enabled.attr,
	&dev_attr_imu_bypass_enabled_index.attr,
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

static struct attribute *legos_touchpad_attrs[] = {
	&dev_attr_touchpad_enabled.attr,
	&dev_attr_touchpad_enabled_index.attr,
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

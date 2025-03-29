// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Alienware WMAX WMI device driver
 *
 * Copyright (C) 2014 Dell Inc <Dell.Client.Kernel@dell.com>
 * Copyright (C) 2025 Kurt Borja <kuurtb@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/dmi.h>
#include <linux/moduleparam.h>
#include <linux/platform_profile.h>
#include <linux/wmi.h>
#include "alienware-wmi.h"

#define WMAX_METHOD_HDMI_SOURCE			0x1
#define WMAX_METHOD_HDMI_STATUS			0x2
#define WMAX_METHOD_HDMI_CABLE			0x5
#define WMAX_METHOD_AMPLIFIER_CABLE		0x6
#define WMAX_METHOD_DEEP_SLEEP_CONTROL		0x0B
#define WMAX_METHOD_DEEP_SLEEP_STATUS		0x0C
#define WMAX_METHOD_BRIGHTNESS			0x3
#define WMAX_METHOD_ZONE_CONTROL		0x4

#define AWCC_METHOD_THERMAL_INFORMATION		0x14
#define AWCC_METHOD_THERMAL_CONTROL		0x15
#define AWCC_METHOD_GAME_SHIFT_STATUS		0x25

#define AWCC_THERMAL_MODE_GMODE			0xAB

#define AWCC_FAILURE_CODE			0xFFFFFFFF
#define AWCC_FAILURE_CODE_2			0xFFFFFFFE

#define AWCC_SENSOR_ID_FLAG			BIT(8)
#define AWCC_THERMAL_MODE_MASK			GENMASK(3, 0)
#define AWCC_THERMAL_TABLE_MASK			GENMASK(7, 4)
#define AWCC_RESOURCE_ID_MASK			GENMASK(7, 0)

static bool force_platform_profile;
module_param_unsafe(force_platform_profile, bool, 0);
MODULE_PARM_DESC(force_platform_profile, "Forces auto-detecting thermal profiles without checking if WMI thermal backend is available");

static bool force_gmode;
module_param_unsafe(force_gmode, bool, 0);
MODULE_PARM_DESC(force_gmode, "Forces G-Mode when performance profile is selected");

struct awcc_quirks {
	bool pprof;
	bool gmode;
};

static struct awcc_quirks g_series_quirks = {
	.pprof = true,
	.gmode = true,
};

static struct awcc_quirks generic_quirks = {
	.pprof = true,
	.gmode = false,
};

static struct awcc_quirks empty_quirks;

static const struct dmi_system_id awcc_dmi_table[] __initconst = {
	{
		.ident = "Alienware m16 R1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware m16 R1 AMD"),
		},
		.driver_data = &generic_quirks,
	},
	{
		.ident = "Alienware m17 R5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware m17 R5 AMD"),
		},
		.driver_data = &generic_quirks,
	},
	{
		.ident = "Alienware m18 R2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware m18 R2"),
		},
		.driver_data = &generic_quirks,
	},
	{
		.ident = "Alienware x15 R1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware x15 R1"),
		},
		.driver_data = &generic_quirks,
	},
	{
		.ident = "Alienware x17 R2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware x17 R2"),
		},
		.driver_data = &generic_quirks,
	},
	{
		.ident = "Dell Inc. G15 5510",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell G15 5510"),
		},
		.driver_data = &g_series_quirks,
	},
	{
		.ident = "Dell Inc. G15 5511",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell G15 5511"),
		},
		.driver_data = &g_series_quirks,
	},
	{
		.ident = "Dell Inc. G15 5515",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell G15 5515"),
		},
		.driver_data = &g_series_quirks,
	},
	{
		.ident = "Dell Inc. G3 3500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "G3 3500"),
		},
		.driver_data = &g_series_quirks,
	},
	{
		.ident = "Dell Inc. G3 3590",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "G3 3590"),
		},
		.driver_data = &g_series_quirks,
	},
	{
		.ident = "Dell Inc. G5 5500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "G5 5500"),
		},
		.driver_data = &g_series_quirks,
	},
};

enum AWCC_THERMAL_INFORMATION_OPERATIONS {
	AWCC_OP_GET_SYSTEM_DESCRIPTION		= 0x02,
	AWCC_OP_GET_RESOURCE_ID			= 0x03,
	AWCC_OP_GET_CURRENT_PROFILE		= 0x0B,
};

enum AWCC_THERMAL_CONTROL_OPERATIONS {
	AWCC_OP_ACTIVATE_PROFILE		= 0x01,
};

enum AWCC_GAME_SHIFT_STATUS_OPERATIONS {
	AWCC_OP_TOGGLE_GAME_SHIFT		= 0x01,
	AWCC_OP_GET_GAME_SHIFT_STATUS		= 0x02,
};

enum AWCC_THERMAL_TABLES {
	AWCC_THERMAL_TABLE_LEGACY		= 0x9,
	AWCC_THERMAL_TABLE_USTT			= 0xA,
};

enum awcc_thermal_profile {
	AWCC_PROFILE_USTT_BALANCED,
	AWCC_PROFILE_USTT_BALANCED_PERFORMANCE,
	AWCC_PROFILE_USTT_COOL,
	AWCC_PROFILE_USTT_QUIET,
	AWCC_PROFILE_USTT_PERFORMANCE,
	AWCC_PROFILE_USTT_LOW_POWER,
	AWCC_PROFILE_LEGACY_QUIET,
	AWCC_PROFILE_LEGACY_BALANCED,
	AWCC_PROFILE_LEGACY_BALANCED_PERFORMANCE,
	AWCC_PROFILE_LEGACY_PERFORMANCE,
	AWCC_PROFILE_LAST,
};

struct wmax_led_args {
	u32 led_mask;
	struct color_platform colors;
	u8 state;
} __packed;

struct wmax_brightness_args {
	u32 led_mask;
	u32 percentage;
};

struct wmax_basic_args {
	u8 arg;
};

struct wmax_u32_args {
	u8 operation;
	u8 arg1;
	u8 arg2;
	u8 arg3;
};

struct awcc_priv {
	struct wmi_device *wdev;
	struct device *ppdev;
	u8 supported_profiles[PLATFORM_PROFILE_LAST];
};

static const enum platform_profile_option awcc_mode_to_platform_profile[AWCC_PROFILE_LAST] = {
	[AWCC_PROFILE_USTT_BALANCED]			= PLATFORM_PROFILE_BALANCED,
	[AWCC_PROFILE_USTT_BALANCED_PERFORMANCE]	= PLATFORM_PROFILE_BALANCED_PERFORMANCE,
	[AWCC_PROFILE_USTT_COOL]			= PLATFORM_PROFILE_COOL,
	[AWCC_PROFILE_USTT_QUIET]			= PLATFORM_PROFILE_QUIET,
	[AWCC_PROFILE_USTT_PERFORMANCE]			= PLATFORM_PROFILE_PERFORMANCE,
	[AWCC_PROFILE_USTT_LOW_POWER]			= PLATFORM_PROFILE_LOW_POWER,
	[AWCC_PROFILE_LEGACY_QUIET]			= PLATFORM_PROFILE_QUIET,
	[AWCC_PROFILE_LEGACY_BALANCED]			= PLATFORM_PROFILE_BALANCED,
	[AWCC_PROFILE_LEGACY_BALANCED_PERFORMANCE]	= PLATFORM_PROFILE_BALANCED_PERFORMANCE,
	[AWCC_PROFILE_LEGACY_PERFORMANCE]		= PLATFORM_PROFILE_PERFORMANCE,
};

static struct awcc_quirks *awcc;

/*
 *	The HDMI mux sysfs node indicates the status of the HDMI input mux.
 *	It can toggle between standard system GPU output and HDMI input.
 */
static ssize_t cable_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct alienfx_platdata *pdata = dev_get_platdata(dev);
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	u32 out_data;
	int ret;

	ret = alienware_wmi_command(pdata->wdev, WMAX_METHOD_HDMI_CABLE,
				    &in_args, sizeof(in_args), &out_data);
	if (!ret) {
		if (out_data == 0)
			return sysfs_emit(buf, "[unconnected] connected unknown\n");
		else if (out_data == 1)
			return sysfs_emit(buf, "unconnected [connected] unknown\n");
	}

	pr_err("alienware-wmi: unknown HDMI cable status: %d\n", ret);
	return sysfs_emit(buf, "unconnected connected [unknown]\n");
}

static ssize_t source_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct alienfx_platdata *pdata = dev_get_platdata(dev);
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	u32 out_data;
	int ret;

	ret = alienware_wmi_command(pdata->wdev, WMAX_METHOD_HDMI_STATUS,
				    &in_args, sizeof(in_args), &out_data);
	if (!ret) {
		if (out_data == 1)
			return sysfs_emit(buf, "[input] gpu unknown\n");
		else if (out_data == 2)
			return sysfs_emit(buf, "input [gpu] unknown\n");
	}

	pr_err("alienware-wmi: unknown HDMI source status: %u\n", ret);
	return sysfs_emit(buf, "input gpu [unknown]\n");
}

static ssize_t source_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct alienfx_platdata *pdata = dev_get_platdata(dev);
	struct wmax_basic_args args;
	int ret;

	if (strcmp(buf, "gpu\n") == 0)
		args.arg = 1;
	else if (strcmp(buf, "input\n") == 0)
		args.arg = 2;
	else
		args.arg = 3;
	pr_debug("alienware-wmi: setting hdmi to %d : %s", args.arg, buf);

	ret = alienware_wmi_command(pdata->wdev, WMAX_METHOD_HDMI_SOURCE, &args,
				    sizeof(args), NULL);
	if (ret < 0)
		pr_err("alienware-wmi: HDMI toggle failed: results: %u\n", ret);

	return count;
}

static DEVICE_ATTR_RO(cable);
static DEVICE_ATTR_RW(source);

static bool hdmi_group_visible(struct kobject *kobj)
{
	return alienware_interface == WMAX && alienfx->hdmi_mux;
}
DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(hdmi);

static struct attribute *hdmi_attrs[] = {
	&dev_attr_cable.attr,
	&dev_attr_source.attr,
	NULL,
};

const struct attribute_group wmax_hdmi_attribute_group = {
	.name = "hdmi",
	.is_visible = SYSFS_GROUP_VISIBLE(hdmi),
	.attrs = hdmi_attrs,
};

/*
 * Alienware GFX amplifier support
 * - Currently supports reading cable status
 * - Leaving expansion room to possibly support dock/undock events later
 */
static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct alienfx_platdata *pdata = dev_get_platdata(dev);
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	u32 out_data;
	int ret;

	ret = alienware_wmi_command(pdata->wdev, WMAX_METHOD_AMPLIFIER_CABLE,
				    &in_args, sizeof(in_args), &out_data);
	if (!ret) {
		if (out_data == 0)
			return sysfs_emit(buf, "[unconnected] connected unknown\n");
		else if (out_data == 1)
			return sysfs_emit(buf, "unconnected [connected] unknown\n");
	}

	pr_err("alienware-wmi: unknown amplifier cable status: %d\n", ret);
	return sysfs_emit(buf, "unconnected connected [unknown]\n");
}

static DEVICE_ATTR_RO(status);

static bool amplifier_group_visible(struct kobject *kobj)
{
	return alienware_interface == WMAX && alienfx->amplifier;
}
DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(amplifier);

static struct attribute *amplifier_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};

const struct attribute_group wmax_amplifier_attribute_group = {
	.name = "amplifier",
	.is_visible = SYSFS_GROUP_VISIBLE(amplifier),
	.attrs = amplifier_attrs,
};

/*
 * Deep Sleep Control support
 * - Modifies BIOS setting for deep sleep control allowing extra wakeup events
 */
static ssize_t deepsleep_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct alienfx_platdata *pdata = dev_get_platdata(dev);
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	u32 out_data;
	int ret;

	ret = alienware_wmi_command(pdata->wdev, WMAX_METHOD_DEEP_SLEEP_STATUS,
				    &in_args, sizeof(in_args), &out_data);
	if (!ret) {
		if (out_data == 0)
			return sysfs_emit(buf, "[disabled] s5 s5_s4\n");
		else if (out_data == 1)
			return sysfs_emit(buf, "disabled [s5] s5_s4\n");
		else if (out_data == 2)
			return sysfs_emit(buf, "disabled s5 [s5_s4]\n");
	}

	pr_err("alienware-wmi: unknown deep sleep status: %d\n", ret);
	return sysfs_emit(buf, "disabled s5 s5_s4 [unknown]\n");
}

static ssize_t deepsleep_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct alienfx_platdata *pdata = dev_get_platdata(dev);
	struct wmax_basic_args args;
	int ret;

	if (strcmp(buf, "disabled\n") == 0)
		args.arg = 0;
	else if (strcmp(buf, "s5\n") == 0)
		args.arg = 1;
	else
		args.arg = 2;
	pr_debug("alienware-wmi: setting deep sleep to %d : %s", args.arg, buf);

	ret = alienware_wmi_command(pdata->wdev, WMAX_METHOD_DEEP_SLEEP_CONTROL,
				    &args, sizeof(args), NULL);
	if (!ret)
		pr_err("alienware-wmi: deep sleep control failed: results: %u\n", ret);

	return count;
}

static DEVICE_ATTR_RW(deepsleep);

static bool deepsleep_group_visible(struct kobject *kobj)
{
	return alienware_interface == WMAX && alienfx->deepslp;
}
DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(deepsleep);

static struct attribute *deepsleep_attrs[] = {
	&dev_attr_deepsleep.attr,
	NULL,
};

const struct attribute_group wmax_deepsleep_attribute_group = {
	.name = "deepsleep",
	.is_visible = SYSFS_GROUP_VISIBLE(deepsleep),
	.attrs = deepsleep_attrs,
};

/*
 * AWCC Helpers
 */
static bool is_awcc_thermal_profile_id(u8 code)
{
	u8 table = FIELD_GET(AWCC_THERMAL_TABLE_MASK, code);
	u8 mode = FIELD_GET(AWCC_THERMAL_MODE_MASK, code);

	if (mode >= AWCC_PROFILE_LAST)
		return false;

	if (table == AWCC_THERMAL_TABLE_LEGACY && mode >= AWCC_PROFILE_LEGACY_QUIET)
		return true;

	if (table == AWCC_THERMAL_TABLE_USTT && mode <= AWCC_PROFILE_USTT_LOW_POWER)
		return true;

	return false;
}

static int awcc_wmi_command(struct wmi_device *wdev, u32 method_id,
			    struct wmax_u32_args *args, u32 *out)
{
	int ret;

	ret = alienware_wmi_command(wdev, method_id, args, sizeof(*args), out);
	if (ret)
		return ret;

	if (*out == AWCC_FAILURE_CODE || *out == AWCC_FAILURE_CODE_2)
		return -EBADRQC;

	return 0;
}

static int awcc_thermal_information(struct wmi_device *wdev, u8 operation, u8 arg,
				    u32 *out)
{
	struct wmax_u32_args args = {
		.operation = operation,
		.arg1 = arg,
		.arg2 = 0,
		.arg3 = 0,
	};

	return awcc_wmi_command(wdev, AWCC_METHOD_THERMAL_INFORMATION, &args, out);
}

static int awcc_game_shift_status(struct wmi_device *wdev, u8 operation,
				  u32 *out)
{
	struct wmax_u32_args args = {
		.operation = operation,
		.arg1 = 0,
		.arg2 = 0,
		.arg3 = 0,
	};

	return awcc_wmi_command(wdev, AWCC_METHOD_GAME_SHIFT_STATUS, &args, out);
}

/**
 * awcc_op_get_resource_id - Get the resource ID at a given index
 * @wdev: AWCC WMI device
 * @index: Index
 * @out: Value returned by the WMI call
 *
 * Get the resource ID at a given @index. Resource IDs are listed in the
 * following order:
 *
 *	- Fan IDs
 *	- Sensor IDs
 *	- Unknown IDs
 *	- Thermal Profile IDs
 *
 * The total number of IDs of a given type can be obtained with
 * AWCC_OP_GET_SYSTEM_DESCRIPTION.
 *
 * Return: 0 on success, -errno on failure
 */
static int awcc_op_get_resource_id(struct wmi_device *wdev, u8 index, u8 *out)
{
	struct wmax_u32_args args = {
		.operation = AWCC_OP_GET_RESOURCE_ID,
		.arg1 = index,
		.arg2 = 0,
		.arg3 = 0,
	};
	u32 out_data;
	int ret;

	ret = awcc_wmi_command(wdev, AWCC_METHOD_THERMAL_INFORMATION, &args, &out_data);
	if (ret)
		return ret;

	*out = FIELD_GET(AWCC_RESOURCE_ID_MASK, out_data);

	return 0;
}

static int awcc_op_get_current_profile(struct wmi_device *wdev, u32 *out)
{
	struct wmax_u32_args args = {
		.operation = AWCC_OP_GET_CURRENT_PROFILE,
		.arg1 = 0,
		.arg2 = 0,
		.arg3 = 0,
	};

	return awcc_wmi_command(wdev, AWCC_METHOD_THERMAL_INFORMATION, &args, out);
}

static int awcc_op_activate_profile(struct wmi_device *wdev, u8 profile)
{
	struct wmax_u32_args args = {
		.operation = AWCC_OP_ACTIVATE_PROFILE,
		.arg1 = profile,
		.arg2 = 0,
		.arg3 = 0,
	};
	u32 out;

	return awcc_wmi_command(wdev, AWCC_METHOD_THERMAL_CONTROL, &args, &out);
}

/*
 * Thermal Profile control
 *  - Provides thermal profile control through the Platform Profile API
 */
static int awcc_platform_profile_get(struct device *dev,
				     enum platform_profile_option *profile)
{
	struct awcc_priv *priv = dev_get_drvdata(dev);
	u32 out_data;
	int ret;

	ret = awcc_op_get_current_profile(priv->wdev, &out_data);
	if (ret)
		return ret;

	if (out_data == AWCC_THERMAL_MODE_GMODE) {
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		return 0;
	}

	if (!is_awcc_thermal_profile_id(out_data))
		return -ENODATA;

	out_data = FIELD_GET(AWCC_THERMAL_MODE_MASK, out_data);
	*profile = awcc_mode_to_platform_profile[out_data];

	return 0;
}

static int awcc_platform_profile_set(struct device *dev,
				     enum platform_profile_option profile)
{
	struct awcc_priv *priv = dev_get_drvdata(dev);

	if (awcc->gmode) {
		u32 gmode_status;
		int ret;

		ret = awcc_game_shift_status(priv->wdev,
					     AWCC_OP_GET_GAME_SHIFT_STATUS,
					     &gmode_status);

		if (ret < 0)
			return ret;

		if ((profile == PLATFORM_PROFILE_PERFORMANCE && !gmode_status) ||
		    (profile != PLATFORM_PROFILE_PERFORMANCE && gmode_status)) {
			ret = awcc_game_shift_status(priv->wdev,
						     AWCC_OP_TOGGLE_GAME_SHIFT,
						     &gmode_status);

			if (ret < 0)
				return ret;
		}
	}

	return awcc_op_activate_profile(priv->wdev, priv->supported_profiles[profile]);
}

static int awcc_platform_profile_probe(void *drvdata, unsigned long *choices)
{
	enum platform_profile_option profile;
	struct awcc_priv *priv = drvdata;
	enum awcc_thermal_profile mode;
	u8 sys_desc[4];
	u32 first_mode;
	int ret;
	u8 id;

	ret = awcc_thermal_information(priv->wdev, AWCC_OP_GET_SYSTEM_DESCRIPTION,
				       0, (u32 *) &sys_desc);
	if (ret < 0)
		return ret;

	first_mode = sys_desc[0] + sys_desc[1];

	for (u32 i = 0; i < sys_desc[3]; i++) {
		ret = awcc_op_get_resource_id(priv->wdev, i + first_mode, &id);

		if (ret == -EIO)
			return ret;

		if (ret == -EBADRQC)
			break;

		if (!is_awcc_thermal_profile_id(id))
			continue;

		mode = FIELD_GET(AWCC_THERMAL_MODE_MASK, id);
		profile = awcc_mode_to_platform_profile[mode];
		priv->supported_profiles[profile] = id;

		set_bit(profile, choices);
	}

	if (bitmap_empty(choices, PLATFORM_PROFILE_LAST))
		return -ENODEV;

	if (awcc->gmode) {
		priv->supported_profiles[PLATFORM_PROFILE_PERFORMANCE] =
			AWCC_THERMAL_MODE_GMODE;

		set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);
	}

	return 0;
}

static const struct platform_profile_ops awcc_platform_profile_ops = {
	.probe = awcc_platform_profile_probe,
	.profile_get = awcc_platform_profile_get,
	.profile_set = awcc_platform_profile_set,
};

static int awcc_platform_profile_init(struct wmi_device *wdev)
{
	struct awcc_priv *priv = dev_get_drvdata(&wdev->dev);

	priv->ppdev = devm_platform_profile_register(&wdev->dev, "alienware-wmi",
						     priv, &awcc_platform_profile_ops);

	return PTR_ERR_OR_ZERO(priv->ppdev);
}

static int alienware_awcc_setup(struct wmi_device *wdev)
{
	struct awcc_priv *priv;
	int ret;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	if (awcc->pprof) {
		ret = awcc_platform_profile_init(wdev);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * WMAX WMI driver
 */
static int wmax_wmi_update_led(struct alienfx_priv *priv,
			       struct wmi_device *wdev, u8 location)
{
	struct wmax_led_args in_args = {
		.led_mask = 1 << location,
		.colors = priv->colors[location],
		.state = priv->lighting_control_state,
	};

	return alienware_wmi_command(wdev, WMAX_METHOD_ZONE_CONTROL, &in_args,
				     sizeof(in_args), NULL);
}

static int wmax_wmi_update_brightness(struct alienfx_priv *priv,
				      struct wmi_device *wdev, u8 brightness)
{
	struct wmax_brightness_args in_args = {
		.led_mask = 0xFF,
		.percentage = brightness,
	};

	return alienware_wmi_command(wdev, WMAX_METHOD_BRIGHTNESS, &in_args,
				     sizeof(in_args), NULL);
}

static int wmax_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct alienfx_platdata pdata = {
		.wdev = wdev,
		.ops = {
			.upd_led = wmax_wmi_update_led,
			.upd_brightness = wmax_wmi_update_brightness,
		},
	};
	int ret;

	if (awcc)
		ret = alienware_awcc_setup(wdev);
	else
		ret = alienware_alienfx_setup(&pdata);

	return ret;
}

static const struct wmi_device_id alienware_wmax_device_id_table[] = {
	{ WMAX_CONTROL_GUID, NULL },
	{ },
};
MODULE_DEVICE_TABLE(wmi, alienware_wmax_device_id_table);

static struct wmi_driver alienware_wmax_wmi_driver = {
	.driver = {
		.name = "alienware-wmi-wmax",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = alienware_wmax_device_id_table,
	.probe = wmax_wmi_probe,
	.no_singleton = true,
};

int __init alienware_wmax_wmi_init(void)
{
	const struct dmi_system_id *id;

	id = dmi_first_match(awcc_dmi_table);
	if (id)
		awcc = id->driver_data;

	if (force_platform_profile) {
		if (!awcc)
			awcc = &empty_quirks;

		awcc->pprof = true;
	}

	if (force_gmode) {
		if (awcc)
			awcc->gmode = true;
		else
			pr_warn("force_gmode requires platform profile support\n");
	}

	return wmi_driver_register(&alienware_wmax_wmi_driver);
}

void __exit alienware_wmax_wmi_exit(void)
{
	wmi_driver_unregister(&alienware_wmax_wmi_driver);
}

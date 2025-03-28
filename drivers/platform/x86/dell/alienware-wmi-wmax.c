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
#define WMAX_METHOD_THERMAL_INFORMATION		0x14
#define WMAX_METHOD_THERMAL_CONTROL		0x15
#define WMAX_METHOD_GAME_SHIFT_STATUS		0x25

#define WMAX_THERMAL_MODE_GMODE			0xAB

#define WMAX_FAILURE_CODE			0xFFFFFFFF
#define WMAX_THERMAL_TABLE_MASK			GENMASK(7, 4)
#define WMAX_THERMAL_MODE_MASK			GENMASK(3, 0)
#define WMAX_SENSOR_ID_MASK			BIT(8)

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

enum WMAX_THERMAL_INFORMATION_OPERATIONS {
	WMAX_OPERATION_SYS_DESCRIPTION		= 0x02,
	WMAX_OPERATION_LIST_IDS			= 0x03,
	WMAX_OPERATION_CURRENT_PROFILE		= 0x0B,
};

enum WMAX_THERMAL_CONTROL_OPERATIONS {
	WMAX_OPERATION_ACTIVATE_PROFILE		= 0x01,
};

enum WMAX_GAME_SHIFT_STATUS_OPERATIONS {
	WMAX_OPERATION_TOGGLE_GAME_SHIFT	= 0x01,
	WMAX_OPERATION_GET_GAME_SHIFT_STATUS	= 0x02,
};

enum WMAX_THERMAL_TABLES {
	WMAX_THERMAL_TABLE_BASIC		= 0x90,
	WMAX_THERMAL_TABLE_USTT			= 0xA0,
};

enum wmax_thermal_mode {
	THERMAL_MODE_USTT_BALANCED,
	THERMAL_MODE_USTT_BALANCED_PERFORMANCE,
	THERMAL_MODE_USTT_COOL,
	THERMAL_MODE_USTT_QUIET,
	THERMAL_MODE_USTT_PERFORMANCE,
	THERMAL_MODE_USTT_LOW_POWER,
	THERMAL_MODE_BASIC_QUIET,
	THERMAL_MODE_BASIC_BALANCED,
	THERMAL_MODE_BASIC_BALANCED_PERFORMANCE,
	THERMAL_MODE_BASIC_PERFORMANCE,
	THERMAL_MODE_LAST,
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
	enum wmax_thermal_mode supported_thermal_profiles[PLATFORM_PROFILE_LAST];
};

static const enum platform_profile_option wmax_mode_to_platform_profile[THERMAL_MODE_LAST] = {
	[THERMAL_MODE_USTT_BALANCED]			= PLATFORM_PROFILE_BALANCED,
	[THERMAL_MODE_USTT_BALANCED_PERFORMANCE]	= PLATFORM_PROFILE_BALANCED_PERFORMANCE,
	[THERMAL_MODE_USTT_COOL]			= PLATFORM_PROFILE_COOL,
	[THERMAL_MODE_USTT_QUIET]			= PLATFORM_PROFILE_QUIET,
	[THERMAL_MODE_USTT_PERFORMANCE]			= PLATFORM_PROFILE_PERFORMANCE,
	[THERMAL_MODE_USTT_LOW_POWER]			= PLATFORM_PROFILE_LOW_POWER,
	[THERMAL_MODE_BASIC_QUIET]			= PLATFORM_PROFILE_QUIET,
	[THERMAL_MODE_BASIC_BALANCED]			= PLATFORM_PROFILE_BALANCED,
	[THERMAL_MODE_BASIC_BALANCED_PERFORMANCE]	= PLATFORM_PROFILE_BALANCED_PERFORMANCE,
	[THERMAL_MODE_BASIC_PERFORMANCE]		= PLATFORM_PROFILE_PERFORMANCE,
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
 * Thermal Profile control
 *  - Provides thermal profile control through the Platform Profile API
 */
static bool is_wmax_thermal_code(u32 code)
{
	if (code & WMAX_SENSOR_ID_MASK)
		return false;

	if ((code & WMAX_THERMAL_MODE_MASK) >= THERMAL_MODE_LAST)
		return false;

	if ((code & WMAX_THERMAL_TABLE_MASK) == WMAX_THERMAL_TABLE_BASIC &&
	    (code & WMAX_THERMAL_MODE_MASK) >= THERMAL_MODE_BASIC_QUIET)
		return true;

	if ((code & WMAX_THERMAL_TABLE_MASK) == WMAX_THERMAL_TABLE_USTT &&
	    (code & WMAX_THERMAL_MODE_MASK) <= THERMAL_MODE_USTT_LOW_POWER)
		return true;

	return false;
}

static int wmax_thermal_information(struct wmi_device *wdev, u8 operation,
				    u8 arg, u32 *out_data)
{
	struct wmax_u32_args in_args = {
		.operation = operation,
		.arg1 = arg,
		.arg2 = 0,
		.arg3 = 0,
	};
	int ret;

	ret = alienware_wmi_command(wdev, WMAX_METHOD_THERMAL_INFORMATION,
				    &in_args, sizeof(in_args), out_data);
	if (ret < 0)
		return ret;

	if (*out_data == WMAX_FAILURE_CODE)
		return -EBADRQC;

	return 0;
}

static int wmax_thermal_control(struct wmi_device *wdev, u8 profile)
{
	struct wmax_u32_args in_args = {
		.operation = WMAX_OPERATION_ACTIVATE_PROFILE,
		.arg1 = profile,
		.arg2 = 0,
		.arg3 = 0,
	};
	u32 out_data;
	int ret;

	ret = alienware_wmi_command(wdev, WMAX_METHOD_THERMAL_CONTROL,
				    &in_args, sizeof(in_args), &out_data);
	if (ret)
		return ret;

	if (out_data == WMAX_FAILURE_CODE)
		return -EBADRQC;

	return 0;
}

static int wmax_game_shift_status(struct wmi_device *wdev, u8 operation,
				  u32 *out_data)
{
	struct wmax_u32_args in_args = {
		.operation = operation,
		.arg1 = 0,
		.arg2 = 0,
		.arg3 = 0,
	};
	int ret;

	ret = alienware_wmi_command(wdev, WMAX_METHOD_GAME_SHIFT_STATUS,
				    &in_args, sizeof(in_args), out_data);
	if (ret < 0)
		return ret;

	if (*out_data == WMAX_FAILURE_CODE)
		return -EOPNOTSUPP;

	return 0;
}

static int thermal_profile_get(struct device *dev,
			       enum platform_profile_option *profile)
{
	struct awcc_priv *priv = dev_get_drvdata(dev);
	u32 out_data;
	int ret;

	ret = wmax_thermal_information(priv->wdev, WMAX_OPERATION_CURRENT_PROFILE,
				       0, &out_data);

	if (ret < 0)
		return ret;

	if (out_data == WMAX_THERMAL_MODE_GMODE) {
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		return 0;
	}

	if (!is_wmax_thermal_code(out_data))
		return -ENODATA;

	out_data &= WMAX_THERMAL_MODE_MASK;
	*profile = wmax_mode_to_platform_profile[out_data];

	return 0;
}

static int thermal_profile_set(struct device *dev,
			       enum platform_profile_option profile)
{
	struct awcc_priv *priv = dev_get_drvdata(dev);

	if (awcc->gmode) {
		u32 gmode_status;
		int ret;

		ret = wmax_game_shift_status(priv->wdev,
					     WMAX_OPERATION_GET_GAME_SHIFT_STATUS,
					     &gmode_status);

		if (ret < 0)
			return ret;

		if ((profile == PLATFORM_PROFILE_PERFORMANCE && !gmode_status) ||
		    (profile != PLATFORM_PROFILE_PERFORMANCE && gmode_status)) {
			ret = wmax_game_shift_status(priv->wdev,
						     WMAX_OPERATION_TOGGLE_GAME_SHIFT,
						     &gmode_status);

			if (ret < 0)
				return ret;
		}
	}

	return wmax_thermal_control(priv->wdev,
				    priv->supported_thermal_profiles[profile]);
}

static int thermal_profile_probe(void *drvdata, unsigned long *choices)
{
	enum platform_profile_option profile;
	struct awcc_priv *priv = drvdata;
	enum wmax_thermal_mode mode;
	u8 sys_desc[4];
	u32 first_mode;
	u32 out_data;
	int ret;

	ret = wmax_thermal_information(priv->wdev, WMAX_OPERATION_SYS_DESCRIPTION,
				       0, (u32 *) &sys_desc);
	if (ret < 0)
		return ret;

	first_mode = sys_desc[0] + sys_desc[1];

	for (u32 i = 0; i < sys_desc[3]; i++) {
		ret = wmax_thermal_information(priv->wdev, WMAX_OPERATION_LIST_IDS,
					       i + first_mode, &out_data);

		if (ret == -EIO)
			return ret;

		if (ret == -EBADRQC)
			break;

		if (!is_wmax_thermal_code(out_data))
			continue;

		mode = out_data & WMAX_THERMAL_MODE_MASK;
		profile = wmax_mode_to_platform_profile[mode];
		priv->supported_thermal_profiles[profile] = out_data;

		set_bit(profile, choices);
	}

	if (bitmap_empty(choices, PLATFORM_PROFILE_LAST))
		return -ENODEV;

	if (awcc->gmode) {
		priv->supported_thermal_profiles[PLATFORM_PROFILE_PERFORMANCE] =
			WMAX_THERMAL_MODE_GMODE;

		set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);
	}

	return 0;
}

static const struct platform_profile_ops awcc_platform_profile_ops = {
	.probe = thermal_profile_probe,
	.profile_get = thermal_profile_get,
	.profile_set = thermal_profile_set,
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

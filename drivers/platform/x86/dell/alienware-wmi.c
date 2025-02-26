// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Alienware AlienFX control
 *
 * Copyright (C) 2014 Dell Inc <Dell.Client.Kernel@dell.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_profile.h>
#include <linux/dmi.h>
#include <linux/leds.h>

#define LEGACY_CONTROL_GUID		"A90597CE-A997-11DA-B012-B622A1EF5492"
#define LEGACY_POWER_CONTROL_GUID	"A80593CE-A997-11DA-B012-B622A1EF5492"
#define WMAX_CONTROL_GUID		"A70591CE-A997-11DA-B012-B622A1EF5492"

#define WMAX_METHOD_HDMI_SOURCE		0x1
#define WMAX_METHOD_HDMI_STATUS		0x2
#define WMAX_METHOD_BRIGHTNESS		0x3
#define WMAX_METHOD_ZONE_CONTROL	0x4
#define WMAX_METHOD_HDMI_CABLE		0x5
#define WMAX_METHOD_AMPLIFIER_CABLE	0x6
#define WMAX_METHOD_DEEP_SLEEP_CONTROL	0x0B
#define WMAX_METHOD_DEEP_SLEEP_STATUS	0x0C
#define WMAX_METHOD_THERMAL_INFORMATION	0x14
#define WMAX_METHOD_THERMAL_CONTROL	0x15
#define WMAX_METHOD_GAME_SHIFT_STATUS	0x25

#define WMAX_THERMAL_MODE_GMODE		0xAB

#define WMAX_FAILURE_CODE		0xFFFFFFFF

MODULE_AUTHOR("Mario Limonciello <mario.limonciello@outlook.com>");
MODULE_DESCRIPTION("Alienware special feature control");
MODULE_LICENSE("GPL");
MODULE_ALIAS("wmi:" LEGACY_CONTROL_GUID);
MODULE_ALIAS("wmi:" WMAX_CONTROL_GUID);

static bool force_platform_profile;
module_param_unsafe(force_platform_profile, bool, 0);
MODULE_PARM_DESC(force_platform_profile, "Forces auto-detecting thermal profiles without checking if WMI thermal backend is available");

static bool force_gmode;
module_param_unsafe(force_gmode, bool, 0);
MODULE_PARM_DESC(force_gmode, "Forces G-Mode when performance profile is selected");

enum INTERFACE_FLAGS {
	LEGACY,
	WMAX,
};

enum LEGACY_CONTROL_STATES {
	LEGACY_RUNNING = 1,
	LEGACY_BOOTING = 0,
	LEGACY_SUSPEND = 3,
};

enum WMAX_CONTROL_STATES {
	WMAX_RUNNING = 0xFF,
	WMAX_BOOTING = 0,
	WMAX_SUSPEND = 3,
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

struct quirk_entry {
	u8 num_zones;
	u8 hdmi_mux;
	u8 amplifier;
	u8 deepslp;
	bool thermal;
	bool gmode;
};

static struct quirk_entry *quirks;


static struct quirk_entry quirk_inspiron5675 = {
	.num_zones = 2,
	.hdmi_mux = 0,
	.amplifier = 0,
	.deepslp = 0,
	.thermal = false,
	.gmode = false,
};

static struct quirk_entry quirk_unknown = {
	.num_zones = 2,
	.hdmi_mux = 0,
	.amplifier = 0,
	.deepslp = 0,
	.thermal = false,
	.gmode = false,
};

static struct quirk_entry quirk_x51_r1_r2 = {
	.num_zones = 3,
	.hdmi_mux = 0,
	.amplifier = 0,
	.deepslp = 0,
	.thermal = false,
	.gmode = false,
};

static struct quirk_entry quirk_x51_r3 = {
	.num_zones = 4,
	.hdmi_mux = 0,
	.amplifier = 1,
	.deepslp = 0,
	.thermal = false,
	.gmode = false,
};

static struct quirk_entry quirk_asm100 = {
	.num_zones = 2,
	.hdmi_mux = 1,
	.amplifier = 0,
	.deepslp = 0,
	.thermal = false,
	.gmode = false,
};

static struct quirk_entry quirk_asm200 = {
	.num_zones = 2,
	.hdmi_mux = 1,
	.amplifier = 0,
	.deepslp = 1,
	.thermal = false,
	.gmode = false,
};

static struct quirk_entry quirk_asm201 = {
	.num_zones = 2,
	.hdmi_mux = 1,
	.amplifier = 1,
	.deepslp = 1,
	.thermal = false,
	.gmode = false,
};

static struct quirk_entry quirk_g_series = {
	.num_zones = 0,
	.hdmi_mux = 0,
	.amplifier = 0,
	.deepslp = 0,
	.thermal = true,
	.gmode = true,
};

static struct quirk_entry quirk_x_series = {
	.num_zones = 0,
	.hdmi_mux = 0,
	.amplifier = 0,
	.deepslp = 0,
	.thermal = true,
	.gmode = false,
};

static int __init dmi_matched(const struct dmi_system_id *dmi)
{
	quirks = dmi->driver_data;
	return 1;
}

static const struct dmi_system_id alienware_quirks[] __initconst = {
	{
		.callback = dmi_matched,
		.ident = "Alienware ASM100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ASM100"),
		},
		.driver_data = &quirk_asm100,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware ASM200",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ASM200"),
		},
		.driver_data = &quirk_asm200,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware ASM201",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ASM201"),
		},
		.driver_data = &quirk_asm201,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware m16 R1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware m16 R1 AMD"),
		},
		.driver_data = &quirk_x_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware m17 R5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware m17 R5 AMD"),
		},
		.driver_data = &quirk_x_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware m18 R2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware m18 R2"),
		},
		.driver_data = &quirk_x_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware x15 R1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware x15 R1"),
		},
		.driver_data = &quirk_x_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware x17 R2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware x17 R2"),
		},
		.driver_data = &quirk_x_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware X51 R1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware X51"),
		},
		.driver_data = &quirk_x51_r1_r2,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware X51 R2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware X51 R2"),
		},
		.driver_data = &quirk_x51_r1_r2,
	},
	{
		.callback = dmi_matched,
		.ident = "Alienware X51 R3",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alienware X51 R3"),
		},
		.driver_data = &quirk_x51_r3,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inc. G15 5510",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell G15 5510"),
		},
		.driver_data = &quirk_g_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inc. G15 5511",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell G15 5511"),
		},
		.driver_data = &quirk_g_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inc. G15 5515",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell G15 5515"),
		},
		.driver_data = &quirk_g_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inc. G3 3500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "G3 3500"),
		},
		.driver_data = &quirk_g_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inc. G3 3590",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "G3 3590"),
		},
		.driver_data = &quirk_g_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inc. G5 5500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "G5 5500"),
		},
		.driver_data = &quirk_g_series,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inc. Inspiron 5675",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 5675"),
		},
		.driver_data = &quirk_inspiron5675,
	},
	{}
};

struct color_platform {
	u8 blue;
	u8 green;
	u8 red;
} __packed;

struct wmax_brightness_args {
	u32 led_mask;
	u32 percentage;
};

struct wmax_basic_args {
	u8 arg;
};

struct legacy_led_args {
	struct color_platform colors;
	u8 brightness;
	u8 state;
} __packed;

struct wmax_led_args {
	u32 led_mask;
	struct color_platform colors;
	u8 state;
} __packed;

struct wmax_u32_args {
	u8 operation;
	u8 arg1;
	u8 arg2;
	u8 arg3;
};

static struct platform_device *platform_device;
static struct color_platform colors[4];
static enum wmax_thermal_mode supported_thermal_profiles[PLATFORM_PROFILE_LAST];

static u8 interface;
static u8 lighting_control_state;
static u8 global_brightness;

/*
 * Helpers used for zone control
 */
static int parse_rgb(const char *buf, struct color_platform *colors)
{
	long unsigned int rgb;
	int ret;
	union color_union {
		struct color_platform cp;
		int package;
	} repackager;

	ret = kstrtoul(buf, 16, &rgb);
	if (ret)
		return ret;

	/* RGB triplet notation is 24-bit hexadecimal */
	if (rgb > 0xFFFFFF)
		return -EINVAL;

	repackager.package = rgb & 0x0f0f0f0f;
	pr_debug("alienware-wmi: r: %d g:%d b: %d\n",
		 repackager.cp.red, repackager.cp.green, repackager.cp.blue);
	*colors = repackager.cp;
	return 0;
}

/*
 * Individual RGB zone control
 */
static int alienware_update_led(u8 location)
{
	int method_id;
	acpi_status status;
	char *guid;
	struct acpi_buffer input;
	struct legacy_led_args legacy_args;
	struct wmax_led_args wmax_basic_args;
	if (interface == WMAX) {
		wmax_basic_args.led_mask = 1 << location;
		wmax_basic_args.colors = colors[location];
		wmax_basic_args.state = lighting_control_state;
		guid = WMAX_CONTROL_GUID;
		method_id = WMAX_METHOD_ZONE_CONTROL;

		input.length = sizeof(wmax_basic_args);
		input.pointer = &wmax_basic_args;
	} else {
		legacy_args.colors = colors[location];
		legacy_args.brightness = global_brightness;
		legacy_args.state = 0;
		if (lighting_control_state == LEGACY_BOOTING ||
		    lighting_control_state == LEGACY_SUSPEND) {
			guid = LEGACY_POWER_CONTROL_GUID;
			legacy_args.state = lighting_control_state;
		} else
			guid = LEGACY_CONTROL_GUID;
		method_id = location + 1;

		input.length = sizeof(legacy_args);
		input.pointer = &legacy_args;
	}
	pr_debug("alienware-wmi: guid %s method %d\n", guid, method_id);

	status = wmi_evaluate_method(guid, 0, method_id, &input, NULL);
	if (ACPI_FAILURE(status))
		pr_err("alienware-wmi: zone set failure: %u\n", status);
	return ACPI_FAILURE(status);
}

static ssize_t zone_show(struct device *dev, struct device_attribute *attr,
			 char *buf, u8 location)
{
	return sprintf(buf, "red: %d, green: %d, blue: %d\n",
		       colors[location].red, colors[location].green,
		       colors[location].blue);

}

static ssize_t zone_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count, u8 location)
{
	int ret;

	ret = parse_rgb(buf, &colors[location]);
	if (ret)
		return ret;

	ret = alienware_update_led(location);

	return ret ? ret : count;
}

static ssize_t zone00_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return zone_show(dev, attr, buf, 0);
}

static ssize_t zone00_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return zone_store(dev, attr, buf, count, 0);
}

static DEVICE_ATTR_RW(zone00);

static ssize_t zone01_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return zone_show(dev, attr, buf, 1);
}

static ssize_t zone01_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return zone_store(dev, attr, buf, count, 1);
}

static DEVICE_ATTR_RW(zone01);

static ssize_t zone02_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return zone_show(dev, attr, buf, 2);
}

static ssize_t zone02_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return zone_store(dev, attr, buf, count, 2);
}

static DEVICE_ATTR_RW(zone02);

static ssize_t zone03_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return zone_show(dev, attr, buf, 3);
}

static ssize_t zone03_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return zone_store(dev, attr, buf, count, 3);
}

static DEVICE_ATTR_RW(zone03);

/*
 * Lighting control state device attribute (Global)
 */
static ssize_t lighting_control_state_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	if (lighting_control_state == LEGACY_BOOTING)
		return sysfs_emit(buf, "[booting] running suspend\n");
	else if (lighting_control_state == LEGACY_SUSPEND)
		return sysfs_emit(buf, "booting running [suspend]\n");

	return sysfs_emit(buf, "booting [running] suspend\n");
}

static ssize_t lighting_control_state_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	u8 val;

	if (strcmp(buf, "booting\n") == 0)
		val = LEGACY_BOOTING;
	else if (strcmp(buf, "suspend\n") == 0)
		val = LEGACY_SUSPEND;
	else if (interface == LEGACY)
		val = LEGACY_RUNNING;
	else
		val = WMAX_RUNNING;

	lighting_control_state = val;
	pr_debug("alienware-wmi: updated control state to %d\n",
		 lighting_control_state);

	return count;
}

static DEVICE_ATTR_RW(lighting_control_state);

static umode_t zone_attr_visible(struct kobject *kobj,
				 struct attribute *attr, int n)
{
	if (n < quirks->num_zones + 1)
		return attr->mode;

	return 0;
}

static bool zone_group_visible(struct kobject *kobj)
{
	return quirks->num_zones > 0;
}
DEFINE_SYSFS_GROUP_VISIBLE(zone);

static struct attribute *zone_attrs[] = {
	&dev_attr_lighting_control_state.attr,
	&dev_attr_zone00.attr,
	&dev_attr_zone01.attr,
	&dev_attr_zone02.attr,
	&dev_attr_zone03.attr,
	NULL
};

static struct attribute_group zone_attribute_group = {
	.name = "rgb_zones",
	.is_visible = SYSFS_GROUP_VISIBLE(zone),
	.attrs = zone_attrs,
};

/*
 * LED Brightness (Global)
 */
static int wmax_brightness(int brightness)
{
	acpi_status status;
	struct acpi_buffer input;
	struct wmax_brightness_args args = {
		.led_mask = 0xFF,
		.percentage = brightness,
	};
	input.length = sizeof(args);
	input.pointer = &args;
	status = wmi_evaluate_method(WMAX_CONTROL_GUID, 0,
				     WMAX_METHOD_BRIGHTNESS, &input, NULL);
	if (ACPI_FAILURE(status))
		pr_err("alienware-wmi: brightness set failure: %u\n", status);
	return ACPI_FAILURE(status);
}

static void global_led_set(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	int ret;
	global_brightness = brightness;
	if (interface == WMAX)
		ret = wmax_brightness(brightness);
	else
		ret = alienware_update_led(0);
	if (ret)
		pr_err("LED brightness update failed\n");
}

static enum led_brightness global_led_get(struct led_classdev *led_cdev)
{
	return global_brightness;
}

static struct led_classdev global_led = {
	.brightness_set = global_led_set,
	.brightness_get = global_led_get,
	.name = "alienware::global_brightness",
};

static int alienware_zone_init(struct platform_device *dev)
{
	if (interface == WMAX) {
		lighting_control_state = WMAX_RUNNING;
	} else if (interface == LEGACY) {
		lighting_control_state = LEGACY_RUNNING;
	}
	global_led.max_brightness = 0x0F;
	global_brightness = global_led.max_brightness;

	return led_classdev_register(&dev->dev, &global_led);
}

static void alienware_zone_exit(struct platform_device *dev)
{
	if (!quirks->num_zones)
		return;

	led_classdev_unregister(&global_led);
}

static acpi_status alienware_wmax_command(void *in_args, size_t in_size,
					  u32 command, u32 *out_data)
{
	acpi_status status;
	union acpi_object *obj;
	struct acpi_buffer input;
	struct acpi_buffer output;

	input.length = in_size;
	input.pointer = in_args;
	if (out_data) {
		output.length = ACPI_ALLOCATE_BUFFER;
		output.pointer = NULL;
		status = wmi_evaluate_method(WMAX_CONTROL_GUID, 0,
					     command, &input, &output);
		if (ACPI_SUCCESS(status)) {
			obj = (union acpi_object *)output.pointer;
			if (obj && obj->type == ACPI_TYPE_INTEGER)
				*out_data = (u32)obj->integer.value;
		}
		kfree(output.pointer);
	} else {
		status = wmi_evaluate_method(WMAX_CONTROL_GUID, 0,
					     command, &input, NULL);
	}
	return status;
}

/*
 *	The HDMI mux sysfs node indicates the status of the HDMI input mux.
 *	It can toggle between standard system GPU output and HDMI input.
 */
static ssize_t cable_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	acpi_status status;
	u32 out_data;

	status =
	    alienware_wmax_command(&in_args, sizeof(in_args),
				   WMAX_METHOD_HDMI_CABLE, &out_data);
	if (ACPI_SUCCESS(status)) {
		if (out_data == 0)
			return sysfs_emit(buf, "[unconnected] connected unknown\n");
		else if (out_data == 1)
			return sysfs_emit(buf, "unconnected [connected] unknown\n");
	}
	pr_err("alienware-wmi: unknown HDMI cable status: %d\n", status);
	return sysfs_emit(buf, "unconnected connected [unknown]\n");
}

static ssize_t source_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	acpi_status status;
	u32 out_data;

	status =
	    alienware_wmax_command(&in_args, sizeof(in_args),
				   WMAX_METHOD_HDMI_STATUS, &out_data);

	if (ACPI_SUCCESS(status)) {
		if (out_data == 1)
			return sysfs_emit(buf, "[input] gpu unknown\n");
		else if (out_data == 2)
			return sysfs_emit(buf, "input [gpu] unknown\n");
	}
	pr_err("alienware-wmi: unknown HDMI source status: %u\n", status);
	return sysfs_emit(buf, "input gpu [unknown]\n");
}

static ssize_t source_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct wmax_basic_args args;
	acpi_status status;

	if (strcmp(buf, "gpu\n") == 0)
		args.arg = 1;
	else if (strcmp(buf, "input\n") == 0)
		args.arg = 2;
	else
		args.arg = 3;
	pr_debug("alienware-wmi: setting hdmi to %d : %s", args.arg, buf);

	status = alienware_wmax_command(&args, sizeof(args),
					WMAX_METHOD_HDMI_SOURCE, NULL);

	if (ACPI_FAILURE(status))
		pr_err("alienware-wmi: HDMI toggle failed: results: %u\n",
		       status);
	return count;
}

static DEVICE_ATTR_RO(cable);
static DEVICE_ATTR_RW(source);

static bool hdmi_group_visible(struct kobject *kobj)
{
	return quirks->hdmi_mux;
}
DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(hdmi);

static struct attribute *hdmi_attrs[] = {
	&dev_attr_cable.attr,
	&dev_attr_source.attr,
	NULL,
};

static const struct attribute_group hdmi_attribute_group = {
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
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	acpi_status status;
	u32 out_data;

	status =
	    alienware_wmax_command(&in_args, sizeof(in_args),
				   WMAX_METHOD_AMPLIFIER_CABLE, &out_data);
	if (ACPI_SUCCESS(status)) {
		if (out_data == 0)
			return sysfs_emit(buf, "[unconnected] connected unknown\n");
		else if (out_data == 1)
			return sysfs_emit(buf, "unconnected [connected] unknown\n");
	}
	pr_err("alienware-wmi: unknown amplifier cable status: %d\n", status);
	return sysfs_emit(buf, "unconnected connected [unknown]\n");
}

static DEVICE_ATTR_RO(status);

static bool amplifier_group_visible(struct kobject *kobj)
{
	return quirks->amplifier;
}
DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(amplifier);

static struct attribute *amplifier_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group amplifier_attribute_group = {
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
	struct wmax_basic_args in_args = {
		.arg = 0,
	};
	acpi_status status;
	u32 out_data;

	status = alienware_wmax_command(&in_args, sizeof(in_args),
					WMAX_METHOD_DEEP_SLEEP_STATUS, &out_data);
	if (ACPI_SUCCESS(status)) {
		if (out_data == 0)
			return sysfs_emit(buf, "[disabled] s5 s5_s4\n");
		else if (out_data == 1)
			return sysfs_emit(buf, "disabled [s5] s5_s4\n");
		else if (out_data == 2)
			return sysfs_emit(buf, "disabled s5 [s5_s4]\n");
	}
	pr_err("alienware-wmi: unknown deep sleep status: %d\n", status);
	return sysfs_emit(buf, "disabled s5 s5_s4 [unknown]\n");
}

static ssize_t deepsleep_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct wmax_basic_args args;
	acpi_status status;

	if (strcmp(buf, "disabled\n") == 0)
		args.arg = 0;
	else if (strcmp(buf, "s5\n") == 0)
		args.arg = 1;
	else
		args.arg = 2;
	pr_debug("alienware-wmi: setting deep sleep to %d : %s", args.arg, buf);

	status = alienware_wmax_command(&args, sizeof(args),
					WMAX_METHOD_DEEP_SLEEP_CONTROL, NULL);

	if (ACPI_FAILURE(status))
		pr_err("alienware-wmi: deep sleep control failed: results: %u\n",
			status);
	return count;
}

static DEVICE_ATTR_RW(deepsleep);

static bool deepsleep_group_visible(struct kobject *kobj)
{
	return quirks->deepslp;
}
DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(deepsleep);

static struct attribute *deepsleep_attrs[] = {
	&dev_attr_deepsleep.attr,
	NULL,
};

static const struct attribute_group deepsleep_attribute_group = {
	.name = "deepsleep",
	.is_visible = SYSFS_GROUP_VISIBLE(deepsleep),
	.attrs = deepsleep_attrs,
};

/*
 * Thermal Profile control
 *  - Provides thermal profile control through the Platform Profile API
 */
#define WMAX_THERMAL_TABLE_MASK		GENMASK(7, 4)
#define WMAX_THERMAL_MODE_MASK		GENMASK(3, 0)
#define WMAX_SENSOR_ID_MASK		BIT(8)

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

static int wmax_thermal_information(u8 operation, u8 arg, u32 *out_data)
{
	struct wmax_u32_args in_args = {
		.operation = operation,
		.arg1 = arg,
		.arg2 = 0,
		.arg3 = 0,
	};
	acpi_status status;

	status = alienware_wmax_command(&in_args, sizeof(in_args),
					WMAX_METHOD_THERMAL_INFORMATION,
					out_data);

	if (ACPI_FAILURE(status))
		return -EIO;

	if (*out_data == WMAX_FAILURE_CODE)
		return -EBADRQC;

	return 0;
}

static int wmax_thermal_control(u8 profile)
{
	struct wmax_u32_args in_args = {
		.operation = WMAX_OPERATION_ACTIVATE_PROFILE,
		.arg1 = profile,
		.arg2 = 0,
		.arg3 = 0,
	};
	acpi_status status;
	u32 out_data;

	status = alienware_wmax_command(&in_args, sizeof(in_args),
					WMAX_METHOD_THERMAL_CONTROL,
					&out_data);

	if (ACPI_FAILURE(status))
		return -EIO;

	if (out_data == WMAX_FAILURE_CODE)
		return -EBADRQC;

	return 0;
}

static int wmax_game_shift_status(u8 operation, u32 *out_data)
{
	struct wmax_u32_args in_args = {
		.operation = operation,
		.arg1 = 0,
		.arg2 = 0,
		.arg3 = 0,
	};
	acpi_status status;

	status = alienware_wmax_command(&in_args, sizeof(in_args),
					WMAX_METHOD_GAME_SHIFT_STATUS,
					out_data);

	if (ACPI_FAILURE(status))
		return -EIO;

	if (*out_data == WMAX_FAILURE_CODE)
		return -EOPNOTSUPP;

	return 0;
}

static int thermal_profile_get(struct device *dev,
			       enum platform_profile_option *profile)
{
	u32 out_data;
	int ret;

	ret = wmax_thermal_information(WMAX_OPERATION_CURRENT_PROFILE,
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
	if (quirks->gmode) {
		u32 gmode_status;
		int ret;

		ret = wmax_game_shift_status(WMAX_OPERATION_GET_GAME_SHIFT_STATUS,
					     &gmode_status);

		if (ret < 0)
			return ret;

		if ((profile == PLATFORM_PROFILE_PERFORMANCE && !gmode_status) ||
		    (profile != PLATFORM_PROFILE_PERFORMANCE && gmode_status)) {
			ret = wmax_game_shift_status(WMAX_OPERATION_TOGGLE_GAME_SHIFT,
						     &gmode_status);

			if (ret < 0)
				return ret;
		}
	}

	return wmax_thermal_control(supported_thermal_profiles[profile]);
}

static int thermal_profile_probe(void *drvdata, unsigned long *choices)
{
	enum platform_profile_option profile;
	enum wmax_thermal_mode mode;
	u8 sys_desc[4];
	u32 first_mode;
	u32 out_data;
	int ret;

	ret = wmax_thermal_information(WMAX_OPERATION_SYS_DESCRIPTION,
				       0, (u32 *) &sys_desc);
	if (ret < 0)
		return ret;

	first_mode = sys_desc[0] + sys_desc[1];

	for (u32 i = 0; i < sys_desc[3]; i++) {
		ret = wmax_thermal_information(WMAX_OPERATION_LIST_IDS,
					       i + first_mode, &out_data);

		if (ret == -EIO)
			return ret;

		if (ret == -EBADRQC)
			break;

		if (!is_wmax_thermal_code(out_data))
			continue;

		mode = out_data & WMAX_THERMAL_MODE_MASK;
		profile = wmax_mode_to_platform_profile[mode];
		supported_thermal_profiles[profile] = out_data;

		set_bit(profile, choices);
	}

	if (bitmap_empty(choices, PLATFORM_PROFILE_LAST))
		return -ENODEV;

	if (quirks->gmode) {
		supported_thermal_profiles[PLATFORM_PROFILE_PERFORMANCE] =
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

static int create_thermal_profile(struct platform_device *platform_device)
{
	struct device *ppdev;

	ppdev = devm_platform_profile_register(&platform_device->dev, "alienware-wmi",
					       NULL, &awcc_platform_profile_ops);

	return PTR_ERR_OR_ZERO(ppdev);
}

/*
 * Platform Driver
 */
static const struct attribute_group *alienfx_groups[] = {
	&zone_attribute_group,
	&hdmi_attribute_group,
	&amplifier_attribute_group,
	&deepsleep_attribute_group,
	NULL
};

static struct platform_driver platform_driver = {
	.driver = {
		.name = "alienware-wmi",
		.dev_groups = alienfx_groups,
	},
};

static int __init alienware_wmi_init(void)
{
	int ret;

	if (wmi_has_guid(LEGACY_CONTROL_GUID))
		interface = LEGACY;
	else if (wmi_has_guid(WMAX_CONTROL_GUID))
		interface = WMAX;
	else {
		pr_warn("alienware-wmi: No known WMI GUID found\n");
		return -ENODEV;
	}

	dmi_check_system(alienware_quirks);
	if (quirks == NULL)
		quirks = &quirk_unknown;

	if (force_platform_profile)
		quirks->thermal = true;

	if (force_gmode) {
		if (quirks->thermal)
			quirks->gmode = true;
		else
			pr_warn("force_gmode requires platform profile support\n");
	}

	ret = platform_driver_register(&platform_driver);
	if (ret)
		goto fail_platform_driver;
	platform_device = platform_device_alloc("alienware-wmi", PLATFORM_DEVID_NONE);
	if (!platform_device) {
		ret = -ENOMEM;
		goto fail_platform_device1;
	}
	ret = platform_device_add(platform_device);
	if (ret)
		goto fail_platform_device2;

	if (quirks->thermal) {
		ret = create_thermal_profile(platform_device);
		if (ret)
			goto fail_prep_thermal_profile;
	}

	if (quirks->num_zones > 0) {
		ret = alienware_zone_init(platform_device);
		if (ret)
			goto fail_prep_zones;
	}

	return 0;

fail_prep_zones:
	alienware_zone_exit(platform_device);
fail_prep_thermal_profile:
	platform_device_del(platform_device);
fail_platform_device2:
	platform_device_put(platform_device);
fail_platform_device1:
	platform_driver_unregister(&platform_driver);
fail_platform_driver:
	return ret;
}

module_init(alienware_wmi_init);

static void __exit alienware_wmi_exit(void)
{
	alienware_zone_exit(platform_device);
	platform_device_unregister(platform_device);
	platform_driver_unregister(&platform_driver);
}

module_exit(alienware_wmi_exit);

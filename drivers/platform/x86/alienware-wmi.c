/*
 * Alienware AlienFX control
 *
 * Copyright (C) 2014 Dell Inc <mario_limonciello@dell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/leds.h>

#define LEGACY_CONTROL_GUID		"A90597CE-A997-11DA-B012-B622A1EF5492"
#define LEGACY_POWER_CONTROL_GUID	"A80593CE-A997-11DA-B012-B622A1EF5492"
#define WMAX_CONTROL_GUID		"A70591CE-A997-11DA-B012-B622A1EF5492"

#define WMAX_METHOD_HDMI_SOURCE		0x1
#define WMAX_METHOD_HDMI_STATUS		0x2
#define WMAX_METHOD_BRIGHTNESS		0x3
#define WMAX_METHOD_ZONE_CONTROL	0x4
#define WMAX_METHOD_HDMI_CABLE		0x5

MODULE_AUTHOR("Mario Limonciello <mario_limonciello@dell.com>");
MODULE_DESCRIPTION("Alienware special feature control");
MODULE_LICENSE("GPL");
MODULE_ALIAS("wmi:" LEGACY_CONTROL_GUID);
MODULE_ALIAS("wmi:" WMAX_CONTROL_GUID);

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

struct quirk_entry {
	u8 num_zones;
};

static struct quirk_entry *quirks;

static struct quirk_entry quirk_unknown = {
	.num_zones = 2,
};

static struct quirk_entry quirk_x51_family = {
	.num_zones = 3,
};

static int dmi_matched(const struct dmi_system_id *dmi)
{
	quirks = dmi->driver_data;
	return 1;
}

static struct dmi_system_id alienware_quirks[] = {
	{
	 .callback = dmi_matched,
	 .ident = "Alienware X51 R1",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Alienware X51"),
		     },
	 .driver_data = &quirk_x51_family,
	 },
	{
	 .callback = dmi_matched,
	 .ident = "Alienware X51 R2",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Alienware X51 R2"),
		     },
	 .driver_data = &quirk_x51_family,
	 },
	{}
};

struct color_platform {
	u8 blue;
	u8 green;
	u8 red;
} __packed;

struct platform_zone {
	u8 location;
	struct device_attribute *attr;
	struct color_platform colors;
};

struct wmax_brightness_args {
	u32 led_mask;
	u32 percentage;
};

struct hdmi_args {
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

static struct platform_device *platform_device;
static struct device_attribute *zone_dev_attrs;
static struct attribute **zone_attrs;
static struct platform_zone *zone_data;

static struct platform_driver platform_driver = {
	.driver = {
		   .name = "alienware-wmi",
		   .owner = THIS_MODULE,
		   }
};

static struct attribute_group zone_attribute_group = {
	.name = "rgb_zones",
};

static u8 interface;
static u8 lighting_control_state;
static u8 global_brightness;

/*
 * Helpers used for zone control
*/
static int parse_rgb(const char *buf, struct platform_zone *zone)
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
	zone->colors = repackager.cp;
	return 0;
}

static struct platform_zone *match_zone(struct device_attribute *attr)
{
	int i;
	for (i = 0; i < quirks->num_zones; i++) {
		if ((struct device_attribute *)zone_data[i].attr == attr) {
			pr_debug("alienware-wmi: matched zone location: %d\n",
				 zone_data[i].location);
			return &zone_data[i];
		}
	}
	return NULL;
}

/*
 * Individual RGB zone control
*/
static int alienware_update_led(struct platform_zone *zone)
{
	int method_id;
	acpi_status status;
	char *guid;
	struct acpi_buffer input;
	struct legacy_led_args legacy_args;
	struct wmax_led_args wmax_args;
	if (interface == WMAX) {
		wmax_args.led_mask = 1 << zone->location;
		wmax_args.colors = zone->colors;
		wmax_args.state = lighting_control_state;
		guid = WMAX_CONTROL_GUID;
		method_id = WMAX_METHOD_ZONE_CONTROL;

		input.length = (acpi_size) sizeof(wmax_args);
		input.pointer = &wmax_args;
	} else {
		legacy_args.colors = zone->colors;
		legacy_args.brightness = global_brightness;
		legacy_args.state = 0;
		if (lighting_control_state == LEGACY_BOOTING ||
		    lighting_control_state == LEGACY_SUSPEND) {
			guid = LEGACY_POWER_CONTROL_GUID;
			legacy_args.state = lighting_control_state;
		} else
			guid = LEGACY_CONTROL_GUID;
		method_id = zone->location + 1;

		input.length = (acpi_size) sizeof(legacy_args);
		input.pointer = &legacy_args;
	}
	pr_debug("alienware-wmi: guid %s method %d\n", guid, method_id);

	status = wmi_evaluate_method(guid, 1, method_id, &input, NULL);
	if (ACPI_FAILURE(status))
		pr_err("alienware-wmi: zone set failure: %u\n", status);
	return ACPI_FAILURE(status);
}

static ssize_t zone_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct platform_zone *target_zone;
	target_zone = match_zone(attr);
	if (target_zone == NULL)
		return sprintf(buf, "red: -1, green: -1, blue: -1\n");
	return sprintf(buf, "red: %d, green: %d, blue: %d\n",
		       target_zone->colors.red,
		       target_zone->colors.green, target_zone->colors.blue);

}

static ssize_t zone_set(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct platform_zone *target_zone;
	int ret;
	target_zone = match_zone(attr);
	if (target_zone == NULL) {
		pr_err("alienware-wmi: invalid target zone\n");
		return 1;
	}
	ret = parse_rgb(buf, target_zone);
	if (ret)
		return ret;
	ret = alienware_update_led(target_zone);
	return ret ? ret : count;
}

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
	input.length = (acpi_size) sizeof(args);
	input.pointer = &args;
	status = wmi_evaluate_method(WMAX_CONTROL_GUID, 1,
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
		ret = alienware_update_led(&zone_data[0]);
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

/*
 * Lighting control state device attribute (Global)
*/
static ssize_t show_control_state(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (lighting_control_state == LEGACY_BOOTING)
		return scnprintf(buf, PAGE_SIZE, "[booting] running suspend\n");
	else if (lighting_control_state == LEGACY_SUSPEND)
		return scnprintf(buf, PAGE_SIZE, "booting running [suspend]\n");
	return scnprintf(buf, PAGE_SIZE, "booting [running] suspend\n");
}

static ssize_t store_control_state(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	long unsigned int val;
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

static DEVICE_ATTR(lighting_control_state, 0644, show_control_state,
		   store_control_state);

static int alienware_zone_init(struct platform_device *dev)
{
	int i;
	char buffer[10];
	char *name;

	if (interface == WMAX) {
		lighting_control_state = WMAX_RUNNING;
	} else if (interface == LEGACY) {
		lighting_control_state = LEGACY_RUNNING;
	}
	global_led.max_brightness = 0x0F;
	global_brightness = global_led.max_brightness;

	/*
	 *      - zone_dev_attrs num_zones + 1 is for individual zones and then
	 *        null terminated
	 *      - zone_attrs num_zones + 2 is for all attrs in zone_dev_attrs +
	 *        the lighting control + null terminated
	 *      - zone_data num_zones is for the distinct zones
	 */
	zone_dev_attrs =
	    kzalloc(sizeof(struct device_attribute) * (quirks->num_zones + 1),
		    GFP_KERNEL);
	if (!zone_dev_attrs)
		return -ENOMEM;

	zone_attrs =
	    kzalloc(sizeof(struct attribute *) * (quirks->num_zones + 2),
		    GFP_KERNEL);
	if (!zone_attrs)
		return -ENOMEM;

	zone_data =
	    kzalloc(sizeof(struct platform_zone) * (quirks->num_zones),
		    GFP_KERNEL);
	if (!zone_data)
		return -ENOMEM;

	for (i = 0; i < quirks->num_zones; i++) {
		sprintf(buffer, "zone%02X", i);
		name = kstrdup(buffer, GFP_KERNEL);
		if (name == NULL)
			return 1;
		sysfs_attr_init(&zone_dev_attrs[i].attr);
		zone_dev_attrs[i].attr.name = name;
		zone_dev_attrs[i].attr.mode = 0644;
		zone_dev_attrs[i].show = zone_show;
		zone_dev_attrs[i].store = zone_set;
		zone_data[i].location = i;
		zone_attrs[i] = &zone_dev_attrs[i].attr;
		zone_data[i].attr = &zone_dev_attrs[i];
	}
	zone_attrs[quirks->num_zones] = &dev_attr_lighting_control_state.attr;
	zone_attribute_group.attrs = zone_attrs;

	led_classdev_register(&dev->dev, &global_led);

	return sysfs_create_group(&dev->dev.kobj, &zone_attribute_group);
}

static void alienware_zone_exit(struct platform_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &zone_attribute_group);
	led_classdev_unregister(&global_led);
	if (zone_dev_attrs) {
		int i;
		for (i = 0; i < quirks->num_zones; i++)
			kfree(zone_dev_attrs[i].attr.name);
	}
	kfree(zone_dev_attrs);
	kfree(zone_data);
	kfree(zone_attrs);
}

/*
	The HDMI mux sysfs node indicates the status of the HDMI input mux.
	It can toggle between standard system GPU output and HDMI input.
*/
static acpi_status alienware_hdmi_command(struct hdmi_args *in_args,
					  u32 command, int *out_data)
{
	acpi_status status;
	union acpi_object *obj;
	struct acpi_buffer input;
	struct acpi_buffer output;

	input.length = (acpi_size) sizeof(*in_args);
	input.pointer = in_args;
	if (out_data != NULL) {
		output.length = ACPI_ALLOCATE_BUFFER;
		output.pointer = NULL;
		status = wmi_evaluate_method(WMAX_CONTROL_GUID, 1,
					     command, &input, &output);
	} else
		status = wmi_evaluate_method(WMAX_CONTROL_GUID, 1,
					     command, &input, NULL);

	if (ACPI_SUCCESS(status) && out_data != NULL) {
		obj = (union acpi_object *)output.pointer;
		if (obj && obj->type == ACPI_TYPE_INTEGER)
			*out_data = (u32) obj->integer.value;
	}
	return status;

}

static ssize_t show_hdmi_cable(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	acpi_status status;
	u32 out_data;
	struct hdmi_args in_args = {
		.arg = 0,
	};
	status =
	    alienware_hdmi_command(&in_args, WMAX_METHOD_HDMI_CABLE,
				   (u32 *) &out_data);
	if (ACPI_SUCCESS(status)) {
		if (out_data == 0)
			return scnprintf(buf, PAGE_SIZE,
					 "[unconnected] connected unknown\n");
		else if (out_data == 1)
			return scnprintf(buf, PAGE_SIZE,
					 "unconnected [connected] unknown\n");
	}
	pr_err("alienware-wmi: unknown HDMI cable status: %d\n", status);
	return scnprintf(buf, PAGE_SIZE, "unconnected connected [unknown]\n");
}

static ssize_t show_hdmi_source(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	acpi_status status;
	u32 out_data;
	struct hdmi_args in_args = {
		.arg = 0,
	};
	status =
	    alienware_hdmi_command(&in_args, WMAX_METHOD_HDMI_STATUS,
				   (u32 *) &out_data);

	if (ACPI_SUCCESS(status)) {
		if (out_data == 1)
			return scnprintf(buf, PAGE_SIZE,
					 "[input] gpu unknown\n");
		else if (out_data == 2)
			return scnprintf(buf, PAGE_SIZE,
					 "input [gpu] unknown\n");
	}
	pr_err("alienware-wmi: unknown HDMI source status: %d\n", out_data);
	return scnprintf(buf, PAGE_SIZE, "input gpu [unknown]\n");
}

static ssize_t toggle_hdmi_source(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	acpi_status status;
	struct hdmi_args args;
	if (strcmp(buf, "gpu\n") == 0)
		args.arg = 1;
	else if (strcmp(buf, "input\n") == 0)
		args.arg = 2;
	else
		args.arg = 3;
	pr_debug("alienware-wmi: setting hdmi to %d : %s", args.arg, buf);

	status = alienware_hdmi_command(&args, WMAX_METHOD_HDMI_SOURCE, NULL);

	if (ACPI_FAILURE(status))
		pr_err("alienware-wmi: HDMI toggle failed: results: %u\n",
		       status);
	return count;
}

static DEVICE_ATTR(cable, S_IRUGO, show_hdmi_cable, NULL);
static DEVICE_ATTR(source, S_IRUGO | S_IWUSR, show_hdmi_source,
		   toggle_hdmi_source);

static struct attribute *hdmi_attrs[] = {
	&dev_attr_cable.attr,
	&dev_attr_source.attr,
	NULL,
};

static struct attribute_group hdmi_attribute_group = {
	.name = "hdmi",
	.attrs = hdmi_attrs,
};

static void remove_hdmi(struct platform_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &hdmi_attribute_group);
}

static int create_hdmi(struct platform_device *dev)
{
	int ret;

	ret = sysfs_create_group(&dev->dev.kobj, &hdmi_attribute_group);
	if (ret)
		goto error_create_hdmi;
	return 0;

error_create_hdmi:
	remove_hdmi(dev);
	return ret;
}

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

	ret = platform_driver_register(&platform_driver);
	if (ret)
		goto fail_platform_driver;
	platform_device = platform_device_alloc("alienware-wmi", -1);
	if (!platform_device) {
		ret = -ENOMEM;
		goto fail_platform_device1;
	}
	ret = platform_device_add(platform_device);
	if (ret)
		goto fail_platform_device2;

	if (interface == WMAX) {
		ret = create_hdmi(platform_device);
		if (ret)
			goto fail_prep_hdmi;
	}

	ret = alienware_zone_init(platform_device);
	if (ret)
		goto fail_prep_zones;

	return 0;

fail_prep_zones:
	alienware_zone_exit(platform_device);
fail_prep_hdmi:
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
	if (platform_device) {
		alienware_zone_exit(platform_device);
		remove_hdmi(platform_device);
		platform_device_unregister(platform_device);
		platform_driver_unregister(&platform_driver);
	}
}

module_exit(alienware_wmi_exit);

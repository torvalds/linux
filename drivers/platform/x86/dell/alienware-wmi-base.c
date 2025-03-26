// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Alienware special feature control
 *
 * Copyright (C) 2014 Dell Inc <Dell.Client.Kernel@dell.com>
 * Copyright (C) 2025 Kurt Borja <kuurtb@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <linux/leds.h>
#include "alienware-wmi.h"

MODULE_AUTHOR("Mario Limonciello <mario.limonciello@outlook.com>");
MODULE_AUTHOR("Kurt Borja <kuurtb@gmail.com>");
MODULE_DESCRIPTION("Alienware special feature control");
MODULE_LICENSE("GPL");

struct alienfx_quirks *alienfx;

static struct alienfx_quirks quirk_inspiron5675 = {
	.num_zones = 2,
	.hdmi_mux = false,
	.amplifier = false,
	.deepslp = false,
};

static struct alienfx_quirks quirk_unknown = {
	.num_zones = 2,
	.hdmi_mux = false,
	.amplifier = false,
	.deepslp = false,
};

static struct alienfx_quirks quirk_x51_r1_r2 = {
	.num_zones = 3,
	.hdmi_mux = false,
	.amplifier = false,
	.deepslp = false,
};

static struct alienfx_quirks quirk_x51_r3 = {
	.num_zones = 4,
	.hdmi_mux = false,
	.amplifier = true,
	.deepslp = false,
};

static struct alienfx_quirks quirk_asm100 = {
	.num_zones = 2,
	.hdmi_mux = true,
	.amplifier = false,
	.deepslp = false,
};

static struct alienfx_quirks quirk_asm200 = {
	.num_zones = 2,
	.hdmi_mux = true,
	.amplifier = false,
	.deepslp = true,
};

static struct alienfx_quirks quirk_asm201 = {
	.num_zones = 2,
	.hdmi_mux = true,
	.amplifier = true,
	.deepslp = true,
};

static int __init dmi_matched(const struct dmi_system_id *dmi)
{
	alienfx = dmi->driver_data;
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
		.ident = "Dell Inc. Inspiron 5675",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 5675"),
		},
		.driver_data = &quirk_inspiron5675,
	},
	{}
};

u8 alienware_interface;

int alienware_wmi_command(struct wmi_device *wdev, u32 method_id,
			  void *in_args, size_t in_size, u32 *out_data)
{
	struct acpi_buffer out = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_buffer in = {in_size, in_args};
	acpi_status ret;

	ret = wmidev_evaluate_method(wdev, 0, method_id, &in, out_data ? &out : NULL);
	if (ACPI_FAILURE(ret))
		return -EIO;

	union acpi_object *obj __free(kfree) = out.pointer;

	if (out_data) {
		if (obj && obj->type == ACPI_TYPE_INTEGER)
			*out_data = (u32)obj->integer.value;
		else
			return -ENOMSG;
	}

	return 0;
}

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
static ssize_t zone_show(struct device *dev, struct device_attribute *attr,
			 char *buf, u8 location)
{
	struct alienfx_priv *priv = dev_get_drvdata(dev);
	struct color_platform *colors = &priv->colors[location];

	return sprintf(buf, "red: %d, green: %d, blue: %d\n",
		       colors->red, colors->green, colors->blue);

}

static ssize_t zone_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count, u8 location)
{
	struct alienfx_priv *priv = dev_get_drvdata(dev);
	struct color_platform *colors = &priv->colors[location];
	struct alienfx_platdata *pdata = dev_get_platdata(dev);
	int ret;

	ret = parse_rgb(buf, colors);
	if (ret)
		return ret;

	ret = pdata->ops.upd_led(priv, pdata->wdev, location);

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
	struct alienfx_priv *priv = dev_get_drvdata(dev);

	if (priv->lighting_control_state == LEGACY_BOOTING)
		return sysfs_emit(buf, "[booting] running suspend\n");
	else if (priv->lighting_control_state == LEGACY_SUSPEND)
		return sysfs_emit(buf, "booting running [suspend]\n");

	return sysfs_emit(buf, "booting [running] suspend\n");
}

static ssize_t lighting_control_state_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct alienfx_priv *priv = dev_get_drvdata(dev);
	u8 val;

	if (strcmp(buf, "booting\n") == 0)
		val = LEGACY_BOOTING;
	else if (strcmp(buf, "suspend\n") == 0)
		val = LEGACY_SUSPEND;
	else if (alienware_interface == LEGACY)
		val = LEGACY_RUNNING;
	else
		val = WMAX_RUNNING;

	priv->lighting_control_state = val;
	pr_debug("alienware-wmi: updated control state to %d\n",
		 priv->lighting_control_state);

	return count;
}

static DEVICE_ATTR_RW(lighting_control_state);

static umode_t zone_attr_visible(struct kobject *kobj,
				 struct attribute *attr, int n)
{
	if (n < alienfx->num_zones + 1)
		return attr->mode;

	return 0;
}

static bool zone_group_visible(struct kobject *kobj)
{
	return alienfx->num_zones > 0;
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
static void global_led_set(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	struct alienfx_priv *priv = container_of(led_cdev, struct alienfx_priv,
						 global_led);
	struct alienfx_platdata *pdata = dev_get_platdata(&priv->pdev->dev);
	int ret;

	priv->global_brightness = brightness;

	ret = pdata->ops.upd_brightness(priv, pdata->wdev, brightness);
	if (ret)
		pr_err("LED brightness update failed\n");
}

static enum led_brightness global_led_get(struct led_classdev *led_cdev)
{
	struct alienfx_priv *priv = container_of(led_cdev, struct alienfx_priv,
						 global_led);

	return priv->global_brightness;
}

/*
 * Platform Driver
 */
static int alienfx_probe(struct platform_device *pdev)
{
	struct alienfx_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (alienware_interface == WMAX)
		priv->lighting_control_state = WMAX_RUNNING;
	else
		priv->lighting_control_state = LEGACY_RUNNING;

	priv->pdev = pdev;
	priv->global_led.name = "alienware::global_brightness";
	priv->global_led.brightness_set = global_led_set;
	priv->global_led.brightness_get = global_led_get;
	priv->global_led.max_brightness = 0x0F;
	priv->global_brightness = priv->global_led.max_brightness;
	platform_set_drvdata(pdev, priv);

	return devm_led_classdev_register(&pdev->dev, &priv->global_led);
}

static const struct attribute_group *alienfx_groups[] = {
	&zone_attribute_group,
	WMAX_DEV_GROUPS
	NULL
};

static struct platform_driver platform_driver = {
	.driver = {
		.name = "alienware-wmi",
		.dev_groups = alienfx_groups,
	},
	.probe = alienfx_probe,
};

static void alienware_alienfx_remove(void *data)
{
	struct platform_device *pdev = data;

	platform_device_unregister(pdev);
}

int alienware_alienfx_setup(struct alienfx_platdata *pdata)
{
	struct device *dev = &pdata->wdev->dev;
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_register_data(NULL, "alienware-wmi",
					     PLATFORM_DEVID_NONE, pdata,
					     sizeof(*pdata));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	dev_set_drvdata(dev, pdev);
	ret = devm_add_action_or_reset(dev, alienware_alienfx_remove, pdev);
	if (ret)
		return ret;

	return 0;
}

static int __init alienware_wmi_init(void)
{
	int ret;

	dmi_check_system(alienware_quirks);
	if (!alienfx)
		alienfx = &quirk_unknown;

	ret = platform_driver_register(&platform_driver);
	if (ret < 0)
		return ret;

	if (wmi_has_guid(WMAX_CONTROL_GUID)) {
		alienware_interface = WMAX;
		ret = alienware_wmax_wmi_init();
	} else {
		alienware_interface = LEGACY;
		ret = alienware_legacy_wmi_init();
	}

	if (ret < 0)
		platform_driver_unregister(&platform_driver);

	return ret;
}

module_init(alienware_wmi_init);

static void __exit alienware_wmi_exit(void)
{
	if (alienware_interface == WMAX)
		alienware_wmax_wmi_exit();
	else
		alienware_legacy_wmi_exit();

	platform_driver_unregister(&platform_driver);
}

module_exit(alienware_wmi_exit);

/*
 *  Generic Backlight Driver
 *
 *  Copyright (c) 2004-2008 Richard Purdie
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include <linux/backlight.h>

static int genericbl_intensity;
static struct backlight_device *generic_backlight_device;
static struct generic_bl_info *bl_machinfo;

/* Flag to signal when the battery is low */
#define GENERICBL_BATTLOW       BL_CORE_DRIVER1

static int genericbl_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_FBBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_SUSPENDED)
		intensity = 0;
	if (bd->props.state & GENERICBL_BATTLOW)
		intensity &= bl_machinfo->limit_mask;

	bl_machinfo->set_bl_intensity(intensity);

	genericbl_intensity = intensity;

	if (bl_machinfo->kick_battery)
		bl_machinfo->kick_battery();

	return 0;
}

static int genericbl_get_intensity(struct backlight_device *bd)
{
	return genericbl_intensity;
}

/*
 * Called when the battery is low to limit the backlight intensity.
 * If limit==0 clear any limit, otherwise limit the intensity
 */
void genericbl_limit_intensity(int limit)
{
	struct backlight_device *bd = generic_backlight_device;

	mutex_lock(&bd->ops_lock);
	if (limit)
		bd->props.state |= GENERICBL_BATTLOW;
	else
		bd->props.state &= ~GENERICBL_BATTLOW;
	backlight_update_status(generic_backlight_device);
	mutex_unlock(&bd->ops_lock);
}
EXPORT_SYMBOL(genericbl_limit_intensity);

static const struct backlight_ops genericbl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = genericbl_get_intensity,
	.update_status  = genericbl_send_intensity,
};

static int genericbl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct generic_bl_info *machinfo = pdev->dev.platform_data;
	const char *name = "generic-bl";
	struct backlight_device *bd;

	bl_machinfo = machinfo;
	if (!machinfo->limit_mask)
		machinfo->limit_mask = -1;

	if (machinfo->name)
		name = machinfo->name;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = machinfo->max_intensity;
	bd = backlight_device_register(name, &pdev->dev, NULL, &genericbl_ops,
				       &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);

	bd->props.power = FB_BLANK_UNBLANK;
	bd->props.brightness = machinfo->default_intensity;
	backlight_update_status(bd);

	generic_backlight_device = bd;

	pr_info("Generic Backlight Driver Initialized.\n");
	return 0;
}

static int genericbl_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	bd->props.power = 0;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	backlight_device_unregister(bd);

	pr_info("Generic Backlight Driver Unloaded\n");
	return 0;
}

static struct platform_driver genericbl_driver = {
	.probe		= genericbl_probe,
	.remove		= genericbl_remove,
	.driver		= {
		.name	= "generic-bl",
	},
};

module_platform_driver(genericbl_driver);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("Generic Backlight Driver");
MODULE_LICENSE("GPL");

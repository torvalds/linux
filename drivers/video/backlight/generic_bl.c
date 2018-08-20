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

static int genericbl_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_FBBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_SUSPENDED)
		intensity = 0;

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

static const struct backlight_ops genericbl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = genericbl_get_intensity,
	.update_status  = genericbl_send_intensity,
};

static int genericbl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct generic_bl_info *machinfo = dev_get_platdata(&pdev->dev);
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
	bd = devm_backlight_device_register(&pdev->dev, name, &pdev->dev,
					NULL, &genericbl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);

	bd->props.power = FB_BLANK_UNBLANK;
	bd->props.brightness = machinfo->default_intensity;
	backlight_update_status(bd);

	generic_backlight_device = bd;

	dev_info(&pdev->dev, "Generic Backlight Driver Initialized.\n");
	return 0;
}

static int genericbl_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	bd->props.power = 0;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	dev_info(&pdev->dev, "Generic Backlight Driver Unloaded\n");
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

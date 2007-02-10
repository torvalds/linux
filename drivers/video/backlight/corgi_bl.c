/*
 *  Backlight Driver for Sharp Zaurus Handhelds (various models)
 *
 *  Copyright (c) 2004-2006 Richard Purdie
 *
 *  Based on Sharp's 2.4 Backlight Driver
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
#include <asm/arch/sharpsl.h>
#include <asm/hardware/sharpsl_pm.h>

static int corgibl_intensity;
static struct backlight_properties corgibl_data;
static struct backlight_device *corgi_backlight_device;
static struct corgibl_machinfo *bl_machinfo;

static unsigned long corgibl_flags;
#define CORGIBL_SUSPENDED     0x01
#define CORGIBL_BATTLOW       0x02

static int corgibl_send_intensity(struct backlight_device *bd)
{
	void (*corgi_kick_batt)(void);
	int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		intensity = 0;
	if (corgibl_flags & CORGIBL_SUSPENDED)
		intensity = 0;
	if (corgibl_flags & CORGIBL_BATTLOW)
		intensity &= bl_machinfo->limit_mask;

	bl_machinfo->set_bl_intensity(intensity);

	corgibl_intensity = intensity;

 	corgi_kick_batt = symbol_get(sharpsl_battery_kick);
 	if (corgi_kick_batt) {
 		corgi_kick_batt();
 		symbol_put(sharpsl_battery_kick);
 	}

	return 0;
}

#ifdef CONFIG_PM
static int corgibl_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	corgibl_flags |= CORGIBL_SUSPENDED;
	backlight_update_status(bd);
	return 0;
}

static int corgibl_resume(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	corgibl_flags &= ~CORGIBL_SUSPENDED;
	backlight_update_status(bd);
	return 0;
}
#else
#define corgibl_suspend	NULL
#define corgibl_resume	NULL
#endif

static int corgibl_get_intensity(struct backlight_device *bd)
{
	return corgibl_intensity;
}

/*
 * Called when the battery is low to limit the backlight intensity.
 * If limit==0 clear any limit, otherwise limit the intensity
 */
void corgibl_limit_intensity(int limit)
{
	if (limit)
		corgibl_flags |= CORGIBL_BATTLOW;
	else
		corgibl_flags &= ~CORGIBL_BATTLOW;
	backlight_update_status(corgi_backlight_device);
}
EXPORT_SYMBOL(corgibl_limit_intensity);


static struct backlight_ops corgibl_ops = {
	.get_brightness = corgibl_get_intensity,
	.update_status  = corgibl_send_intensity,
};

static int corgibl_probe(struct platform_device *pdev)
{
	struct corgibl_machinfo *machinfo = pdev->dev.platform_data;

	bl_machinfo = machinfo;
	if (!machinfo->limit_mask)
		machinfo->limit_mask = -1;

	corgi_backlight_device = backlight_device_register ("corgi-bl",
		&pdev->dev, NULL, &corgibl_ops);
	if (IS_ERR (corgi_backlight_device))
		return PTR_ERR (corgi_backlight_device);

	platform_set_drvdata(pdev, corgi_backlight_device);

	corgi_backlight_device->props.max_brightness = machinfo->max_intensity;
	corgi_backlight_device->props.power = FB_BLANK_UNBLANK;
	corgi_backlight_device->props.brightness = machinfo->default_intensity;
	backlight_update_status(corgi_backlight_device);

	printk("Corgi Backlight Driver Initialized.\n");
	return 0;
}

static int corgibl_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	corgibl_data.power = 0;
	corgibl_data.brightness = 0;
	backlight_update_status(bd);

	backlight_device_unregister(bd);

	printk("Corgi Backlight Driver Unloaded\n");
	return 0;
}

static struct platform_driver corgibl_driver = {
	.probe		= corgibl_probe,
	.remove		= corgibl_remove,
	.suspend	= corgibl_suspend,
	.resume		= corgibl_resume,
	.driver		= {
		.name	= "corgi-bl",
	},
};

static int __init corgibl_init(void)
{
	return platform_driver_register(&corgibl_driver);
}

static void __exit corgibl_exit(void)
{
	platform_driver_unregister(&corgibl_driver);
}

module_init(corgibl_init);
module_exit(corgibl_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("Corgi Backlight Driver");
MODULE_LICENSE("GPL");

/*
 *  Backlight Driver for Sharp Corgi
 *
 *  Copyright (c) 2004-2005 Richard Purdie
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
#include <linux/spinlock.h>
#include <linux/fb.h>
#include <linux/backlight.h>

#include <asm/arch/sharpsl.h>
#include <asm/hardware/sharpsl_pm.h>

#define CORGI_DEFAULT_INTENSITY		0x1f
#define CORGI_LIMIT_MASK		0x0b

static int corgibl_powermode = FB_BLANK_UNBLANK;
static int current_intensity = 0;
static int corgibl_limit = 0;
static void (*corgibl_mach_set_intensity)(int intensity);
static spinlock_t bl_lock = SPIN_LOCK_UNLOCKED;
static struct backlight_properties corgibl_data;

static void corgibl_send_intensity(int intensity)
{
	unsigned long flags;
	void (*corgi_kick_batt)(void);

	if (corgibl_powermode != FB_BLANK_UNBLANK) {
		intensity = 0;
	} else {
		if (corgibl_limit)
			intensity &= CORGI_LIMIT_MASK;
	}

	spin_lock_irqsave(&bl_lock, flags);

	corgibl_mach_set_intensity(intensity);

	spin_unlock_irqrestore(&bl_lock, flags);

 	corgi_kick_batt = symbol_get(sharpsl_battery_kick);
 	if (corgi_kick_batt) {
 		corgi_kick_batt();
 		symbol_put(sharpsl_battery_kick);
 	}
}

static void corgibl_blank(int blank)
{
	switch(blank) {

	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		if (corgibl_powermode == FB_BLANK_UNBLANK) {
			corgibl_send_intensity(0);
			corgibl_powermode = blank;
		}
		break;
	case FB_BLANK_UNBLANK:
		if (corgibl_powermode != FB_BLANK_UNBLANK) {
			corgibl_powermode = blank;
			corgibl_send_intensity(current_intensity);
		}
		break;
	}
}

#ifdef CONFIG_PM
static int corgibl_suspend(struct platform_device *dev, pm_message_t state)
{
	corgibl_blank(FB_BLANK_POWERDOWN);
	return 0;
}

static int corgibl_resume(struct platform_device *dev)
{
	corgibl_blank(FB_BLANK_UNBLANK);
	return 0;
}
#else
#define corgibl_suspend	NULL
#define corgibl_resume	NULL
#endif


static int corgibl_set_power(struct backlight_device *bd, int state)
{
	corgibl_blank(state);
	return 0;
}

static int corgibl_get_power(struct backlight_device *bd)
{
	return corgibl_powermode;
}

static int corgibl_set_intensity(struct backlight_device *bd, int intensity)
{
	if (intensity > corgibl_data.max_brightness)
		intensity = corgibl_data.max_brightness;
	corgibl_send_intensity(intensity);
	current_intensity=intensity;
	return 0;
}

static int corgibl_get_intensity(struct backlight_device *bd)
{
	return current_intensity;
}

/*
 * Called when the battery is low to limit the backlight intensity.
 * If limit==0 clear any limit, otherwise limit the intensity
 */
void corgibl_limit_intensity(int limit)
{
	corgibl_limit = (limit ? 1 : 0);
	corgibl_send_intensity(current_intensity);
}
EXPORT_SYMBOL(corgibl_limit_intensity);


static struct backlight_properties corgibl_data = {
	.owner		= THIS_MODULE,
	.get_power      = corgibl_get_power,
	.set_power      = corgibl_set_power,
	.get_brightness = corgibl_get_intensity,
	.set_brightness = corgibl_set_intensity,
};

static struct backlight_device *corgi_backlight_device;

static int __init corgibl_probe(struct platform_device *pdev)
{
	struct corgibl_machinfo *machinfo = pdev->dev.platform_data;

	corgibl_data.max_brightness = machinfo->max_intensity;
	corgibl_mach_set_intensity = machinfo->set_bl_intensity;

	corgi_backlight_device = backlight_device_register ("corgi-bl",
		NULL, &corgibl_data);
	if (IS_ERR (corgi_backlight_device))
		return PTR_ERR (corgi_backlight_device);

	corgibl_set_intensity(NULL, CORGI_DEFAULT_INTENSITY);
	corgibl_limit_intensity(0);

	printk("Corgi Backlight Driver Initialized.\n");
	return 0;
}

static int corgibl_remove(struct platform_device *dev)
{
	backlight_device_unregister(corgi_backlight_device);

	corgibl_set_intensity(NULL, 0);

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
MODULE_LICENSE("GPLv2");

/* linux/arch/arm/plat-samsung/dev-backlight.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * Common infrastructure for PWM Backlight for Samsung boards
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/pwm_backlight.h>

#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <plat/backlight.h>

struct samsung_bl_drvdata {
	struct platform_pwm_backlight_data plat_data;
	struct samsung_bl_gpio_info *gpio_info;
};

static int samsung_bl_init(struct device *dev)
{
	int ret = 0;
	struct platform_pwm_backlight_data *pdata = dev->platform_data;
	struct samsung_bl_drvdata *drvdata = container_of(pdata,
					struct samsung_bl_drvdata, plat_data);
	struct samsung_bl_gpio_info *bl_gpio_info = drvdata->gpio_info;

	ret = gpio_request(bl_gpio_info->no, "Backlight");
	if (ret) {
		printk(KERN_ERR "failed to request GPIO for LCD Backlight\n");
		return ret;
	}

	/* Configure GPIO pin with specific GPIO function for PWM timer */
	s3c_gpio_cfgpin(bl_gpio_info->no, bl_gpio_info->func);

	return 0;
}

static void samsung_bl_exit(struct device *dev)
{
	struct platform_pwm_backlight_data *pdata = dev->platform_data;
	struct samsung_bl_drvdata *drvdata = container_of(pdata,
					struct samsung_bl_drvdata, plat_data);
	struct samsung_bl_gpio_info *bl_gpio_info = drvdata->gpio_info;

	s3c_gpio_cfgpin(bl_gpio_info->no, S3C_GPIO_OUTPUT);
	gpio_free(bl_gpio_info->no);
}

/* Initialize few important fields of platform_pwm_backlight_data
 * structure with default values. These fields can be overridden by
 * board-specific values sent from machine file.
 * For ease of operation, these fields are initialized with values
 * used by most samsung boards.
 * Users has the option of sending info about other parameters
 * for their specific boards
 */

static struct samsung_bl_drvdata samsung_dfl_bl_data __initdata = {
	.plat_data = {
		.max_brightness = 255,
		.dft_brightness = 255,
		.pwm_period_ns  = 78770,
		.enable_gpio    = -1,
		.init           = samsung_bl_init,
		.exit           = samsung_bl_exit,
	},
};

static struct platform_device samsung_dfl_bl_device __initdata = {
	.name		= "pwm-backlight",
};

/* samsung_bl_set - Set board specific data (if any) provided by user for
 * PWM Backlight control and register specific PWM and backlight device.
 * @gpio_info:	structure containing GPIO info for PWM timer
 * @bl_data:	structure containing Backlight control data
 */
void __init samsung_bl_set(struct samsung_bl_gpio_info *gpio_info,
	struct platform_pwm_backlight_data *bl_data)
{
	int ret = 0;
	struct platform_device *samsung_bl_device;
	struct samsung_bl_drvdata *samsung_bl_drvdata;
	struct platform_pwm_backlight_data *samsung_bl_data;

	samsung_bl_device = kmemdup(&samsung_dfl_bl_device,
			sizeof(struct platform_device), GFP_KERNEL);
	if (!samsung_bl_device) {
		printk(KERN_ERR "%s: no memory for platform dev\n", __func__);
		return;
	}

	samsung_bl_drvdata = kmemdup(&samsung_dfl_bl_data,
				sizeof(samsung_dfl_bl_data), GFP_KERNEL);
	if (!samsung_bl_drvdata) {
		printk(KERN_ERR "%s: no memory for platform dev\n", __func__);
		goto err_data;
	}
	samsung_bl_device->dev.platform_data = &samsung_bl_drvdata->plat_data;
	samsung_bl_drvdata->gpio_info = gpio_info;
	samsung_bl_data = &samsung_bl_drvdata->plat_data;

	/* Copy board specific data provided by user */
	samsung_bl_data->pwm_id = bl_data->pwm_id;
	samsung_bl_device->dev.parent = &samsung_device_pwm.dev;

	if (bl_data->max_brightness)
		samsung_bl_data->max_brightness = bl_data->max_brightness;
	if (bl_data->dft_brightness)
		samsung_bl_data->dft_brightness = bl_data->dft_brightness;
	if (bl_data->lth_brightness)
		samsung_bl_data->lth_brightness = bl_data->lth_brightness;
	if (bl_data->pwm_period_ns)
		samsung_bl_data->pwm_period_ns = bl_data->pwm_period_ns;
	if (bl_data->enable_gpio >= 0)
		samsung_bl_data->enable_gpio = bl_data->enable_gpio;
	if (bl_data->enable_gpio_flags)
		samsung_bl_data->enable_gpio_flags = bl_data->enable_gpio_flags;
	if (bl_data->init)
		samsung_bl_data->init = bl_data->init;
	if (bl_data->notify)
		samsung_bl_data->notify = bl_data->notify;
	if (bl_data->notify_after)
		samsung_bl_data->notify_after = bl_data->notify_after;
	if (bl_data->exit)
		samsung_bl_data->exit = bl_data->exit;
	if (bl_data->check_fb)
		samsung_bl_data->check_fb = bl_data->check_fb;

	/* Register the Backlight dev */
	ret = platform_device_register(samsung_bl_device);
	if (ret) {
		printk(KERN_ERR "failed to register backlight device: %d\n", ret);
		goto err_plat_reg2;
	}

	return;

err_plat_reg2:
	kfree(samsung_bl_data);
err_data:
	kfree(samsung_bl_device);
	return;
}

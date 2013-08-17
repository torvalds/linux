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

static int samsung_bl_init(struct device *dev)
{
	int ret = 0;
	struct platform_device *timer_dev =
			container_of(dev->parent, struct platform_device, dev);
	struct samsung_bl_gpio_info *bl_gpio_info =
			timer_dev->dev.platform_data;

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
	struct platform_device *timer_dev =
			container_of(dev->parent, struct platform_device, dev);
	struct samsung_bl_gpio_info *bl_gpio_info =
			timer_dev->dev.platform_data;

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

static struct platform_pwm_backlight_data samsung_dfl_bl_data __initdata = {
	.max_brightness = 255,
	.dft_brightness = 255,
	.pwm_period_ns  = 78770,
	.init           = samsung_bl_init,
	.exit           = samsung_bl_exit,
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
	struct platform_pwm_backlight_data *samsung_bl_data;

	samsung_bl_device = kmemdup(&samsung_dfl_bl_device,
			sizeof(struct platform_device), GFP_KERNEL);
	if (!samsung_bl_device) {
		printk(KERN_ERR "%s: no memory for platform dev\n", __func__);
		return;
	}

	samsung_bl_data = s3c_set_platdata(&samsung_dfl_bl_data,
		sizeof(struct platform_pwm_backlight_data), samsung_bl_device);
	if (!samsung_bl_data) {
		printk(KERN_ERR "%s: no memory for platform dev\n", __func__);
		goto err_data;
	}

	/* Copy board specific data provided by user */
	samsung_bl_data->pwm_id = bl_data->pwm_id;
	samsung_bl_device->dev.parent =
			&s3c_device_timer[samsung_bl_data->pwm_id].dev;

	if (bl_data->max_brightness)
		samsung_bl_data->max_brightness = bl_data->max_brightness;
	if (bl_data->dft_brightness)
		samsung_bl_data->dft_brightness = bl_data->dft_brightness;
	if (bl_data->lth_brightness)
		samsung_bl_data->lth_brightness = bl_data->lth_brightness;
	if (bl_data->pwm_period_ns)
		samsung_bl_data->pwm_period_ns = bl_data->pwm_period_ns;
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

	/* Keep the GPIO info for future use */
	s3c_device_timer[samsung_bl_data->pwm_id].dev.platform_data = gpio_info;

	/* Register the specific PWM timer dev for Backlight control */
	ret = platform_device_register(
			&s3c_device_timer[samsung_bl_data->pwm_id]);
	if (ret) {
		printk(KERN_ERR "failed to register pwm timer for backlight: %d\n", ret);
		goto err_plat_reg1;
	}

	/* Register the Backlight dev */
	ret = platform_device_register(samsung_bl_device);
	if (ret) {
		printk(KERN_ERR "failed to register backlight device: %d\n", ret);
		goto err_plat_reg2;
	}

	return;

err_plat_reg2:
	platform_device_unregister(&s3c_device_timer[samsung_bl_data->pwm_id]);
err_plat_reg1:
	kfree(samsung_bl_data);
err_data:
	kfree(samsung_bl_device);
	return;
}

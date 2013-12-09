/*
 * Copyright (C) 2008 Atmel Corporation
 *
 * Backlight driver using Atmel PWM peripheral.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/backlight.h>
#include <linux/atmel_pwm.h>
#include <linux/atmel-pwm-bl.h>
#include <linux/slab.h>

struct atmel_pwm_bl {
	const struct atmel_pwm_bl_platform_data	*pdata;
	struct backlight_device			*bldev;
	struct platform_device			*pdev;
	struct pwm_channel			pwmc;
	int					gpio_on;
};

static int atmel_pwm_bl_set_intensity(struct backlight_device *bd)
{
	struct atmel_pwm_bl *pwmbl = bl_get_data(bd);
	int intensity = bd->props.brightness;
	int pwm_duty;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		intensity = 0;

	if (pwmbl->pdata->pwm_active_low)
		pwm_duty = pwmbl->pdata->pwm_duty_min + intensity;
	else
		pwm_duty = pwmbl->pdata->pwm_duty_max - intensity;

	if (pwm_duty > pwmbl->pdata->pwm_duty_max)
		pwm_duty = pwmbl->pdata->pwm_duty_max;
	if (pwm_duty < pwmbl->pdata->pwm_duty_min)
		pwm_duty = pwmbl->pdata->pwm_duty_min;

	if (!intensity) {
		if (pwmbl->gpio_on != -1) {
			gpio_set_value(pwmbl->gpio_on,
					0 ^ pwmbl->pdata->on_active_low);
		}
		pwm_channel_writel(&pwmbl->pwmc, PWM_CUPD, pwm_duty);
		pwm_channel_disable(&pwmbl->pwmc);
	} else {
		pwm_channel_enable(&pwmbl->pwmc);
		pwm_channel_writel(&pwmbl->pwmc, PWM_CUPD, pwm_duty);
		if (pwmbl->gpio_on != -1) {
			gpio_set_value(pwmbl->gpio_on,
					1 ^ pwmbl->pdata->on_active_low);
		}
	}

	return 0;
}

static int atmel_pwm_bl_get_intensity(struct backlight_device *bd)
{
	struct atmel_pwm_bl *pwmbl = bl_get_data(bd);
	u32 intensity;

	if (pwmbl->pdata->pwm_active_low) {
		intensity = pwm_channel_readl(&pwmbl->pwmc, PWM_CDTY) -
			pwmbl->pdata->pwm_duty_min;
	} else {
		intensity = pwmbl->pdata->pwm_duty_max -
			pwm_channel_readl(&pwmbl->pwmc, PWM_CDTY);
	}

	return intensity & 0xffff;
}

static int atmel_pwm_bl_init_pwm(struct atmel_pwm_bl *pwmbl)
{
	unsigned long pwm_rate = pwmbl->pwmc.mck;
	unsigned long prescale = DIV_ROUND_UP(pwm_rate,
			(pwmbl->pdata->pwm_frequency *
			 pwmbl->pdata->pwm_compare_max)) - 1;

	/*
	 * Prescale must be power of two and maximum 0xf in size because of
	 * hardware limit. PWM speed will be:
	 *	PWM module clock speed / (2 ^ prescale).
	 */
	prescale = fls(prescale);
	if (prescale > 0xf)
		prescale = 0xf;

	pwm_channel_writel(&pwmbl->pwmc, PWM_CMR, prescale);
	pwm_channel_writel(&pwmbl->pwmc, PWM_CDTY,
			pwmbl->pdata->pwm_duty_min +
			pwmbl->bldev->props.brightness);
	pwm_channel_writel(&pwmbl->pwmc, PWM_CPRD,
			pwmbl->pdata->pwm_compare_max);

	dev_info(&pwmbl->pdev->dev, "Atmel PWM backlight driver "
			"(%lu Hz)\n", pwmbl->pwmc.mck /
			pwmbl->pdata->pwm_compare_max /
			(1 << prescale));

	return pwm_channel_enable(&pwmbl->pwmc);
}

static const struct backlight_ops atmel_pwm_bl_ops = {
	.get_brightness = atmel_pwm_bl_get_intensity,
	.update_status  = atmel_pwm_bl_set_intensity,
};

static int atmel_pwm_bl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	const struct atmel_pwm_bl_platform_data *pdata;
	struct backlight_device *bldev;
	struct atmel_pwm_bl *pwmbl;
	int retval;

	pwmbl = kzalloc(sizeof(struct atmel_pwm_bl), GFP_KERNEL);
	if (!pwmbl)
		return -ENOMEM;

	pwmbl->pdev = pdev;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		retval = -ENODEV;
		goto err_free_mem;
	}

	if (pdata->pwm_compare_max < pdata->pwm_duty_max ||
			pdata->pwm_duty_min > pdata->pwm_duty_max ||
			pdata->pwm_frequency == 0) {
		retval = -EINVAL;
		goto err_free_mem;
	}

	pwmbl->pdata = pdata;
	pwmbl->gpio_on = pdata->gpio_on;

	retval = pwm_channel_alloc(pdata->pwm_channel, &pwmbl->pwmc);
	if (retval)
		goto err_free_mem;

	if (pwmbl->gpio_on != -1) {
		retval = gpio_request(pwmbl->gpio_on, "gpio_atmel_pwm_bl");
		if (retval) {
			pwmbl->gpio_on = -1;
			goto err_free_pwm;
		}

		/* Turn display off by default. */
		retval = gpio_direction_output(pwmbl->gpio_on,
				0 ^ pdata->on_active_low);
		if (retval)
			goto err_free_gpio;
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = pdata->pwm_duty_max - pdata->pwm_duty_min;
	bldev = backlight_device_register("atmel-pwm-bl", &pdev->dev, pwmbl,
					  &atmel_pwm_bl_ops, &props);
	if (IS_ERR(bldev)) {
		retval = PTR_ERR(bldev);
		goto err_free_gpio;
	}

	pwmbl->bldev = bldev;

	platform_set_drvdata(pdev, pwmbl);

	/* Power up the backlight by default at middle intesity. */
	bldev->props.power = FB_BLANK_UNBLANK;
	bldev->props.brightness = bldev->props.max_brightness / 2;

	retval = atmel_pwm_bl_init_pwm(pwmbl);
	if (retval)
		goto err_free_bl_dev;

	atmel_pwm_bl_set_intensity(bldev);

	return 0;

err_free_bl_dev:
	platform_set_drvdata(pdev, NULL);
	backlight_device_unregister(bldev);
err_free_gpio:
	if (pwmbl->gpio_on != -1)
		gpio_free(pwmbl->gpio_on);
err_free_pwm:
	pwm_channel_free(&pwmbl->pwmc);
err_free_mem:
	kfree(pwmbl);
	return retval;
}

static int __exit atmel_pwm_bl_remove(struct platform_device *pdev)
{
	struct atmel_pwm_bl *pwmbl = platform_get_drvdata(pdev);

	if (pwmbl->gpio_on != -1) {
		gpio_set_value(pwmbl->gpio_on, 0);
		gpio_free(pwmbl->gpio_on);
	}
	pwm_channel_disable(&pwmbl->pwmc);
	pwm_channel_free(&pwmbl->pwmc);
	backlight_device_unregister(pwmbl->bldev);
	platform_set_drvdata(pdev, NULL);
	kfree(pwmbl);

	return 0;
}

static struct platform_driver atmel_pwm_bl_driver = {
	.driver = {
		.name = "atmel-pwm-bl",
	},
	/* REVISIT add suspend() and resume() */
	.remove = __exit_p(atmel_pwm_bl_remove),
};

static int __init atmel_pwm_bl_init(void)
{
	return platform_driver_probe(&atmel_pwm_bl_driver, atmel_pwm_bl_probe);
}
module_init(atmel_pwm_bl_init);

static void __exit atmel_pwm_bl_exit(void)
{
	platform_driver_unregister(&atmel_pwm_bl_driver);
}
module_exit(atmel_pwm_bl_exit);

MODULE_AUTHOR("Hans-Christian egtvedt <hans-christian.egtvedt@atmel.com>");
MODULE_DESCRIPTION("Atmel PWM backlight driver");
MODULE_LICENSE("GPL");

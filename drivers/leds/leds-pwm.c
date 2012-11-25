/*
 * linux/drivers/leds-pwm.c
 *
 * simple PWM based LED control
 *
 * Copyright 2009 Luotao Fu @ Pengutronix (l.fu@pengutronix.de)
 *
 * based on leds-gpio.c by Raphael Assenat <raph@8d.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/leds_pwm.h>
#include <linux/slab.h>

struct led_pwm_data {
	struct led_classdev	cdev;
	struct pwm_device	*pwm;
	unsigned int		active_low;
	unsigned int		period;
};

static void led_pwm_set(struct led_classdev *led_cdev,
	enum led_brightness brightness)
{
	struct led_pwm_data *led_dat =
		container_of(led_cdev, struct led_pwm_data, cdev);
	unsigned int max = led_dat->cdev.max_brightness;
	unsigned int period =  led_dat->period;

	if (brightness == 0) {
		pwm_config(led_dat->pwm, 0, period);
		pwm_disable(led_dat->pwm);
	} else {
		pwm_config(led_dat->pwm, brightness * period / max, period);
		pwm_enable(led_dat->pwm);
	}
}

static int led_pwm_probe(struct platform_device *pdev)
{
	struct led_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct led_pwm *cur_led;
	struct led_pwm_data *leds_data, *led_dat;
	int i, ret = 0;

	if (!pdata)
		return -EBUSY;

	leds_data = devm_kzalloc(&pdev->dev,
			sizeof(struct led_pwm_data) * pdata->num_leds,
				GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;

	for (i = 0; i < pdata->num_leds; i++) {
		cur_led = &pdata->leds[i];
		led_dat = &leds_data[i];

		led_dat->pwm = pwm_request(cur_led->pwm_id,
				cur_led->name);
		if (IS_ERR(led_dat->pwm)) {
			ret = PTR_ERR(led_dat->pwm);
			dev_err(&pdev->dev, "unable to request PWM %d\n",
					cur_led->pwm_id);
			goto err;
		}

		led_dat->cdev.name = cur_led->name;
		led_dat->cdev.default_trigger = cur_led->default_trigger;
		led_dat->active_low = cur_led->active_low;
		led_dat->period = cur_led->pwm_period_ns;
		led_dat->cdev.brightness_set = led_pwm_set;
		led_dat->cdev.brightness = LED_OFF;
		led_dat->cdev.max_brightness = cur_led->max_brightness;
		led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;

		ret = led_classdev_register(&pdev->dev, &led_dat->cdev);
		if (ret < 0) {
			pwm_free(led_dat->pwm);
			goto err;
		}
	}

	platform_set_drvdata(pdev, leds_data);

	return 0;

err:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			led_classdev_unregister(&leds_data[i].cdev);
			pwm_free(leds_data[i].pwm);
		}
	}

	return ret;
}

static int __devexit led_pwm_remove(struct platform_device *pdev)
{
	int i;
	struct led_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct led_pwm_data *leds_data;

	leds_data = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&leds_data[i].cdev);
		pwm_free(leds_data[i].pwm);
	}

	return 0;
}

static struct platform_driver led_pwm_driver = {
	.probe		= led_pwm_probe,
	.remove		= __devexit_p(led_pwm_remove),
	.driver		= {
		.name	= "leds_pwm",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(led_pwm_driver);

MODULE_AUTHOR("Luotao Fu <l.fu@pengutronix.de>");
MODULE_DESCRIPTION("PWM LED driver for PXA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-pwm");

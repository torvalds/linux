/*
 * LEDs driver for GPIOs
 *
 * Copyright (C) 2007 8D Technologies inc.
 * Raphael Assenat <raph@8d.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/workqueue.h>

#include <asm/gpio.h>

struct gpio_led_data {
	struct led_classdev cdev;
	unsigned gpio;
	struct work_struct work;
	u8 new_level;
	u8 can_sleep;
	u8 active_low;
};

static void gpio_led_work(struct work_struct *work)
{
	struct gpio_led_data	*led_dat =
		container_of(work, struct gpio_led_data, work);

	gpio_set_value_cansleep(led_dat->gpio, led_dat->new_level);
}

static void gpio_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct gpio_led_data *led_dat =
		container_of(led_cdev, struct gpio_led_data, cdev);
	int level;

	if (value == LED_OFF)
		level = 0;
	else
		level = 1;

	if (led_dat->active_low)
		level = !level;

	/* Setting GPIOs with I2C/etc requires a task context, and we don't
	 * seem to have a reliable way to know if we're already in one; so
	 * let's just assume the worst.
	 */
	if (led_dat->can_sleep) {
		led_dat->new_level = level;
		schedule_work(&led_dat->work);
	} else
		gpio_set_value(led_dat->gpio, level);
}

static int gpio_led_probe(struct platform_device *pdev)
{
	struct gpio_led_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_led *cur_led;
	struct gpio_led_data *leds_data, *led_dat;
	int i, ret = 0;

	if (!pdata)
		return -EBUSY;

	leds_data = kzalloc(sizeof(struct gpio_led_data) * pdata->num_leds,
				GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;

	for (i = 0; i < pdata->num_leds; i++) {
		cur_led = &pdata->leds[i];
		led_dat = &leds_data[i];

		ret = gpio_request(cur_led->gpio, cur_led->name);
		if (ret < 0)
			goto err;

		led_dat->cdev.name = cur_led->name;
		led_dat->cdev.default_trigger = cur_led->default_trigger;
		led_dat->gpio = cur_led->gpio;
		led_dat->can_sleep = gpio_cansleep(cur_led->gpio);
		led_dat->active_low = cur_led->active_low;
		led_dat->cdev.brightness_set = gpio_led_set;
		led_dat->cdev.brightness = LED_OFF;

		gpio_direction_output(led_dat->gpio, led_dat->active_low);

		INIT_WORK(&led_dat->work, gpio_led_work);

		ret = led_classdev_register(&pdev->dev, &led_dat->cdev);
		if (ret < 0) {
			gpio_free(led_dat->gpio);
			goto err;
		}
	}

	platform_set_drvdata(pdev, leds_data);

	return 0;

err:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			led_classdev_unregister(&leds_data[i].cdev);
			cancel_work_sync(&leds_data[i].work);
			gpio_free(leds_data[i].gpio);
		}
	}

	kfree(leds_data);

	return ret;
}

static int __devexit gpio_led_remove(struct platform_device *pdev)
{
	int i;
	struct gpio_led_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_led_data *leds_data;

	leds_data = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&leds_data[i].cdev);
		cancel_work_sync(&leds_data[i].work);
		gpio_free(leds_data[i].gpio);
	}

	kfree(leds_data);

	return 0;
}

#ifdef CONFIG_PM
static int gpio_led_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct gpio_led_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_led_data *leds_data;
	int i;

	leds_data = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++)
		led_classdev_suspend(&leds_data[i].cdev);

	return 0;
}

static int gpio_led_resume(struct platform_device *pdev)
{
	struct gpio_led_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_led_data *leds_data;
	int i;

	leds_data = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++)
		led_classdev_resume(&leds_data[i].cdev);

	return 0;
}
#else
#define gpio_led_suspend NULL
#define gpio_led_resume NULL
#endif

static struct platform_driver gpio_led_driver = {
	.probe		= gpio_led_probe,
	.remove		= __devexit_p(gpio_led_remove),
	.suspend	= gpio_led_suspend,
	.resume		= gpio_led_resume,
	.driver		= {
		.name	= "leds-gpio",
		.owner	= THIS_MODULE,
	},
};

static int __init gpio_led_init(void)
{
	return platform_driver_register(&gpio_led_driver);
}

static void __exit gpio_led_exit(void)
{
	platform_driver_unregister(&gpio_led_driver);
}

module_init(gpio_led_init);
module_exit(gpio_led_exit);

MODULE_AUTHOR("Raphael Assenat <raph@8d.com>");
MODULE_DESCRIPTION("GPIO LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-gpio");

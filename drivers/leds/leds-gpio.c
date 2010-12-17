/*
 * LEDs driver for GPIOs
 *
 * Copyright (C) 2007 8D Technologies inc.
 * Raphael Assenat <raph@8d.com>
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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
	int (*platform_gpio_blink_set)(unsigned gpio,
			unsigned long *delay_on, unsigned long *delay_off);
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

static int gpio_blink_set(struct led_classdev *led_cdev,
	unsigned long *delay_on, unsigned long *delay_off)
{
	struct gpio_led_data *led_dat =
		container_of(led_cdev, struct gpio_led_data, cdev);

	return led_dat->platform_gpio_blink_set(led_dat->gpio, delay_on, delay_off);
}

static int __devinit create_gpio_led(const struct gpio_led *template,
	struct gpio_led_data *led_dat, struct device *parent,
	int (*blink_set)(unsigned, unsigned long *, unsigned long *))
{
	int ret, state;

	led_dat->gpio = -1;

	/* skip leds that aren't available */
	if (!gpio_is_valid(template->gpio)) {
		printk(KERN_INFO "Skipping unavailable LED gpio %d (%s)\n",
				template->gpio, template->name);
		return 0;
	}

	ret = gpio_request(template->gpio, template->name);
	if (ret < 0)
		return ret;

	led_dat->cdev.name = template->name;
	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->gpio = template->gpio;
	led_dat->can_sleep = gpio_cansleep(template->gpio);
	led_dat->active_low = template->active_low;
	if (blink_set) {
		led_dat->platform_gpio_blink_set = blink_set;
		led_dat->cdev.blink_set = gpio_blink_set;
	}
	led_dat->cdev.brightness_set = gpio_led_set;
	if (template->default_state == LEDS_GPIO_DEFSTATE_KEEP)
		state = !!gpio_get_value(led_dat->gpio) ^ led_dat->active_low;
	else
		state = (template->default_state == LEDS_GPIO_DEFSTATE_ON);
	led_dat->cdev.brightness = state ? LED_FULL : LED_OFF;
	if (!template->retain_state_suspended)
		led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;

	ret = gpio_direction_output(led_dat->gpio, led_dat->active_low ^ state);
	if (ret < 0)
		goto err;

	INIT_WORK(&led_dat->work, gpio_led_work);

	ret = led_classdev_register(parent, &led_dat->cdev);
	if (ret < 0)
		goto err;

	return 0;
err:
	gpio_free(led_dat->gpio);
	return ret;
}

static void delete_gpio_led(struct gpio_led_data *led)
{
	if (!gpio_is_valid(led->gpio))
		return;
	led_classdev_unregister(&led->cdev);
	cancel_work_sync(&led->work);
	gpio_free(led->gpio);
}

#ifdef CONFIG_LEDS_GPIO_PLATFORM
static int __devinit gpio_led_probe(struct platform_device *pdev)
{
	struct gpio_led_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_led_data *leds_data;
	int i, ret = 0;

	if (!pdata)
		return -EBUSY;

	leds_data = kzalloc(sizeof(struct gpio_led_data) * pdata->num_leds,
				GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;

	for (i = 0; i < pdata->num_leds; i++) {
		ret = create_gpio_led(&pdata->leds[i], &leds_data[i],
				      &pdev->dev, pdata->gpio_blink_set);
		if (ret < 0)
			goto err;
	}

	platform_set_drvdata(pdev, leds_data);

	return 0;

err:
	for (i = i - 1; i >= 0; i--)
		delete_gpio_led(&leds_data[i]);

	kfree(leds_data);

	return ret;
}

static int __devexit gpio_led_remove(struct platform_device *pdev)
{
	int i;
	struct gpio_led_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_led_data *leds_data;

	leds_data = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++)
		delete_gpio_led(&leds_data[i]);

	kfree(leds_data);

	return 0;
}

static struct platform_driver gpio_led_driver = {
	.probe		= gpio_led_probe,
	.remove		= __devexit_p(gpio_led_remove),
	.driver		= {
		.name	= "leds-gpio",
		.owner	= THIS_MODULE,
	},
};

MODULE_ALIAS("platform:leds-gpio");
#endif /* CONFIG_LEDS_GPIO_PLATFORM */

/* Code to create from OpenFirmware platform devices */
#ifdef CONFIG_LEDS_GPIO_OF
#include <linux/of_platform.h>
#include <linux/of_gpio.h>

struct gpio_led_of_platform_data {
	int num_leds;
	struct gpio_led_data led_data[];
};

static int __devinit of_gpio_leds_probe(struct of_device *ofdev,
					const struct of_device_id *match)
{
	struct device_node *np = ofdev->node, *child;
	struct gpio_led_of_platform_data *pdata;
	int count = 0, ret;

	/* count LEDs defined by this device, so we know how much to allocate */
	for_each_child_of_node(np, child)
		count++;
	if (!count)
		return 0; /* or ENODEV? */

	pdata = kzalloc(sizeof(*pdata) + sizeof(struct gpio_led_data) * count,
			GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		struct gpio_led led = {};
		enum of_gpio_flags flags;
		const char *state;

		led.gpio = of_get_gpio_flags(child, 0, &flags);
		led.active_low = flags & OF_GPIO_ACTIVE_LOW;
		led.name = of_get_property(child, "label", NULL) ? : child->name;
		led.default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);
		state = of_get_property(child, "default-state", NULL);
		if (state) {
			if (!strcmp(state, "keep"))
				led.default_state = LEDS_GPIO_DEFSTATE_KEEP;
			else if(!strcmp(state, "on"))
				led.default_state = LEDS_GPIO_DEFSTATE_ON;
			else
				led.default_state = LEDS_GPIO_DEFSTATE_OFF;
		}

		ret = create_gpio_led(&led, &pdata->led_data[pdata->num_leds++],
				      &ofdev->dev, NULL);
		if (ret < 0) {
			of_node_put(child);
			goto err;
		}
	}

	dev_set_drvdata(&ofdev->dev, pdata);

	return 0;

err:
	for (count = pdata->num_leds - 2; count >= 0; count--)
		delete_gpio_led(&pdata->led_data[count]);

	kfree(pdata);

	return ret;
}

static int __devexit of_gpio_leds_remove(struct of_device *ofdev)
{
	struct gpio_led_of_platform_data *pdata = dev_get_drvdata(&ofdev->dev);
	int i;

	for (i = 0; i < pdata->num_leds; i++)
		delete_gpio_led(&pdata->led_data[i]);

	kfree(pdata);

	dev_set_drvdata(&ofdev->dev, NULL);

	return 0;
}

static const struct of_device_id of_gpio_leds_match[] = {
	{ .compatible = "gpio-leds", },
	{},
};

static struct of_platform_driver of_gpio_leds_driver = {
	.driver = {
		.name = "of_gpio_leds",
		.owner = THIS_MODULE,
	},
	.match_table = of_gpio_leds_match,
	.probe = of_gpio_leds_probe,
	.remove = __devexit_p(of_gpio_leds_remove),
};
#endif

static int __init gpio_led_init(void)
{
	int ret;

#ifdef CONFIG_LEDS_GPIO_PLATFORM	
	ret = platform_driver_register(&gpio_led_driver);
	if (ret)
		return ret;
#endif
#ifdef CONFIG_LEDS_GPIO_OF
	ret = of_register_platform_driver(&of_gpio_leds_driver);
#endif
#ifdef CONFIG_LEDS_GPIO_PLATFORM	
	if (ret)
		platform_driver_unregister(&gpio_led_driver);
#endif

	return ret;
}

static void __exit gpio_led_exit(void)
{
#ifdef CONFIG_LEDS_GPIO_PLATFORM
	platform_driver_unregister(&gpio_led_driver);
#endif
#ifdef CONFIG_LEDS_GPIO_OF
	of_unregister_platform_driver(&of_gpio_leds_driver);
#endif
}

module_init(gpio_led_init);
module_exit(gpio_led_exit);

MODULE_AUTHOR("Raphael Assenat <raph@8d.com>, Trent Piepho <tpiepho@freescale.com>");
MODULE_DESCRIPTION("GPIO LED driver");
MODULE_LICENSE("GPL");

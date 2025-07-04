// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic GPIO beeper driver
 *
 * Copyright (C) 2013-2014 Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>

#define BEEPER_MODNAME		"gpio-beeper"

struct gpio_beeper {
	struct work_struct	work;
	struct gpio_desc	*desc;
	bool			beeping;
};

static void gpio_beeper_toggle(struct gpio_beeper *beep, bool on)
{
	gpiod_set_value_cansleep(beep->desc, on);
}

static void gpio_beeper_work(struct work_struct *work)
{
	struct gpio_beeper *beep = container_of(work, struct gpio_beeper, work);

	gpio_beeper_toggle(beep, beep->beeping);
}

static int gpio_beeper_event(struct input_dev *dev, unsigned int type,
			     unsigned int code, int value)
{
	struct gpio_beeper *beep = input_get_drvdata(dev);

	if (type != EV_SND || code != SND_BELL)
		return -ENOTSUPP;

	if (value < 0)
		return -EINVAL;

	beep->beeping = value;
	/* Schedule work to actually turn the beeper on or off */
	schedule_work(&beep->work);

	return 0;
}

static void gpio_beeper_close(struct input_dev *input)
{
	struct gpio_beeper *beep = input_get_drvdata(input);

	cancel_work_sync(&beep->work);
	gpio_beeper_toggle(beep, false);
}

static int gpio_beeper_probe(struct platform_device *pdev)
{
	struct gpio_beeper *beep;
	struct input_dev *input;

	beep = devm_kzalloc(&pdev->dev, sizeof(*beep), GFP_KERNEL);
	if (!beep)
		return -ENOMEM;

	beep->desc = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(beep->desc))
		return PTR_ERR(beep->desc);

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	INIT_WORK(&beep->work, gpio_beeper_work);

	input->name		= pdev->name;
	input->id.bustype	= BUS_HOST;
	input->id.vendor	= 0x0001;
	input->id.product	= 0x0001;
	input->id.version	= 0x0100;
	input->close		= gpio_beeper_close;
	input->event		= gpio_beeper_event;

	input_set_capability(input, EV_SND, SND_BELL);

	input_set_drvdata(input, beep);

	return input_register_device(input);
}

#ifdef CONFIG_OF
static const struct of_device_id gpio_beeper_of_match[] = {
	{ .compatible = "gpio-beeper", },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_beeper_of_match);
#endif

static struct platform_driver gpio_beeper_platform_driver = {
	.driver	= {
		.name		= BEEPER_MODNAME,
		.of_match_table	= of_match_ptr(gpio_beeper_of_match),
	},
	.probe	= gpio_beeper_probe,
};
module_platform_driver(gpio_beeper_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("Generic GPIO beeper driver");

/*
 *  Driver for buttons on GPIO lines not capable of generating interrupts
 *
 *  Copyright (C) 2007-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2010 Nuno Goncalves <nunojpg@gmail.com>
 *
 *  This file was based on: /drivers/input/misc/cobalt_btns.c
 *	Copyright (C) 2007 Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  also was based on: /drivers/input/keyboard/gpio_keys.c
 *	Copyright 2005 Phil Blundell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>

#define DRV_NAME	"gpio-keys-polled"

struct gpio_keys_button_data {
	int last_state;
	int count;
	int threshold;
	int can_sleep;
};

struct gpio_keys_polled_dev {
	struct input_polled_dev *poll_dev;
	struct device *dev;
	struct gpio_keys_platform_data *pdata;
	struct gpio_keys_button_data data[0];
};

static void gpio_keys_polled_check_state(struct input_dev *input,
					 struct gpio_keys_button *button,
					 struct gpio_keys_button_data *bdata)
{
	int state;

	if (bdata->can_sleep)
		state = !!gpio_get_value_cansleep(button->gpio);
	else
		state = !!gpio_get_value(button->gpio);

	if (state != bdata->last_state) {
		unsigned int type = button->type ?: EV_KEY;

		input_event(input, type, button->code,
			    !!(state ^ button->active_low));
		input_sync(input);
		bdata->count = 0;
		bdata->last_state = state;
	}
}

static void gpio_keys_polled_poll(struct input_polled_dev *dev)
{
	struct gpio_keys_polled_dev *bdev = dev->private;
	struct gpio_keys_platform_data *pdata = bdev->pdata;
	struct input_dev *input = dev->input;
	int i;

	for (i = 0; i < bdev->pdata->nbuttons; i++) {
		struct gpio_keys_button_data *bdata = &bdev->data[i];

		if (bdata->count < bdata->threshold)
			bdata->count++;
		else
			gpio_keys_polled_check_state(input, &pdata->buttons[i],
						     bdata);
	}
}

static void gpio_keys_polled_open(struct input_polled_dev *dev)
{
	struct gpio_keys_polled_dev *bdev = dev->private;
	struct gpio_keys_platform_data *pdata = bdev->pdata;

	if (pdata->enable)
		pdata->enable(bdev->dev);
}

static void gpio_keys_polled_close(struct input_polled_dev *dev)
{
	struct gpio_keys_polled_dev *bdev = dev->private;
	struct gpio_keys_platform_data *pdata = bdev->pdata;

	if (pdata->disable)
		pdata->disable(bdev->dev);
}

static int __devinit gpio_keys_polled_probe(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct gpio_keys_polled_dev *bdev;
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	int error;
	int i;

	if (!pdata || !pdata->poll_interval)
		return -EINVAL;

	bdev = kzalloc(sizeof(struct gpio_keys_polled_dev) +
		       pdata->nbuttons * sizeof(struct gpio_keys_button_data),
		       GFP_KERNEL);
	if (!bdev) {
		dev_err(dev, "no memory for private data\n");
		return -ENOMEM;
	}

	poll_dev = input_allocate_polled_device();
	if (!poll_dev) {
		dev_err(dev, "no memory for polled device\n");
		error = -ENOMEM;
		goto err_free_bdev;
	}

	poll_dev->private = bdev;
	poll_dev->poll = gpio_keys_polled_poll;
	poll_dev->poll_interval = pdata->poll_interval;
	poll_dev->open = gpio_keys_polled_open;
	poll_dev->close = gpio_keys_polled_close;

	input = poll_dev->input;

	input->evbit[0] = BIT(EV_KEY);
	input->name = pdev->name;
	input->phys = DRV_NAME"/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];
		struct gpio_keys_button_data *bdata = &bdev->data[i];
		unsigned int gpio = button->gpio;
		unsigned int type = button->type ?: EV_KEY;

		if (button->wakeup) {
			dev_err(dev, DRV_NAME " does not support wakeup\n");
			error = -EINVAL;
			goto err_free_gpio;
		}

		error = gpio_request(gpio,
				     button->desc ? button->desc : DRV_NAME);
		if (error) {
			dev_err(dev, "unable to claim gpio %u, err=%d\n",
				gpio, error);
			goto err_free_gpio;
		}

		error = gpio_direction_input(gpio);
		if (error) {
			dev_err(dev,
				"unable to set direction on gpio %u, err=%d\n",
				gpio, error);
			goto err_free_gpio;
		}

		bdata->can_sleep = gpio_cansleep(gpio);
		bdata->last_state = -1;
		bdata->threshold = DIV_ROUND_UP(button->debounce_interval,
						pdata->poll_interval);

		input_set_capability(input, type, button->code);
	}

	bdev->poll_dev = poll_dev;
	bdev->dev = dev;
	bdev->pdata = pdata;
	platform_set_drvdata(pdev, bdev);

	error = input_register_polled_device(poll_dev);
	if (error) {
		dev_err(dev, "unable to register polled device, err=%d\n",
			error);
		goto err_free_gpio;
	}

	/* report initial state of the buttons */
	for (i = 0; i < pdata->nbuttons; i++)
		gpio_keys_polled_check_state(input, &pdata->buttons[i],
					 &bdev->data[i]);

	return 0;

err_free_gpio:
	while (--i >= 0)
		gpio_free(pdata->buttons[i].gpio);

	input_free_polled_device(poll_dev);

err_free_bdev:
	kfree(bdev);

	platform_set_drvdata(pdev, NULL);
	return error;
}

static int __devexit gpio_keys_polled_remove(struct platform_device *pdev)
{
	struct gpio_keys_polled_dev *bdev = platform_get_drvdata(pdev);
	struct gpio_keys_platform_data *pdata = bdev->pdata;
	int i;

	input_unregister_polled_device(bdev->poll_dev);

	for (i = 0; i < pdata->nbuttons; i++)
		gpio_free(pdata->buttons[i].gpio);

	input_free_polled_device(bdev->poll_dev);

	kfree(bdev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver gpio_keys_polled_driver = {
	.probe	= gpio_keys_polled_probe,
	.remove	= __devexit_p(gpio_keys_polled_remove),
	.driver	= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(gpio_keys_polled_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_DESCRIPTION("Polled GPIO Buttons driver");
MODULE_ALIAS("platform:" DRV_NAME);

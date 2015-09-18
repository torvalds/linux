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
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio_keys.h>
#include <linux/property.h>

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
	const struct gpio_keys_platform_data *pdata;
	struct gpio_keys_button_data data[0];
};

static void gpio_keys_polled_check_state(struct input_dev *input,
					 struct gpio_keys_button *button,
					 struct gpio_keys_button_data *bdata)
{
	int state;

	if (bdata->can_sleep)
		state = !!gpiod_get_value_cansleep(button->gpiod);
	else
		state = !!gpiod_get_value(button->gpiod);

	if (state != bdata->last_state) {
		unsigned int type = button->type ?: EV_KEY;

		input_event(input, type, button->code, state);
		input_sync(input);
		bdata->count = 0;
		bdata->last_state = state;
	}
}

static void gpio_keys_polled_poll(struct input_polled_dev *dev)
{
	struct gpio_keys_polled_dev *bdev = dev->private;
	const struct gpio_keys_platform_data *pdata = bdev->pdata;
	struct input_dev *input = dev->input;
	int i;

	for (i = 0; i < pdata->nbuttons; i++) {
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
	const struct gpio_keys_platform_data *pdata = bdev->pdata;

	if (pdata->enable)
		pdata->enable(bdev->dev);
}

static void gpio_keys_polled_close(struct input_polled_dev *dev)
{
	struct gpio_keys_polled_dev *bdev = dev->private;
	const struct gpio_keys_platform_data *pdata = bdev->pdata;

	if (pdata->disable)
		pdata->disable(bdev->dev);
}

static struct gpio_keys_platform_data *gpio_keys_polled_get_devtree_pdata(struct device *dev)
{
	struct gpio_keys_platform_data *pdata;
	struct gpio_keys_button *button;
	struct fwnode_handle *child;
	int error;
	int nbuttons;

	nbuttons = device_get_child_node_count(dev);
	if (nbuttons == 0)
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata) + nbuttons * sizeof(*button),
			     GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->buttons = (struct gpio_keys_button *)(pdata + 1);

	pdata->rep = device_property_present(dev, "autorepeat");
	device_property_read_u32(dev, "poll-interval", &pdata->poll_interval);

	device_for_each_child_node(dev, child) {
		struct gpio_desc *desc;

		desc = devm_get_gpiod_from_child(dev, NULL, child);
		if (IS_ERR(desc)) {
			error = PTR_ERR(desc);
			if (error != -EPROBE_DEFER)
				dev_err(dev,
					"Failed to get gpio flags, error: %d\n",
					error);
			fwnode_handle_put(child);
			return ERR_PTR(error);
		}

		button = &pdata->buttons[pdata->nbuttons++];
		button->gpiod = desc;

		if (fwnode_property_read_u32(child, "linux,code", &button->code)) {
			dev_err(dev, "Button without keycode: %d\n",
				pdata->nbuttons - 1);
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}

		fwnode_property_read_string(child, "label", &button->desc);

		if (fwnode_property_read_u32(child, "linux,input-type",
					     &button->type))
			button->type = EV_KEY;

		button->wakeup =
			fwnode_property_read_bool(child, "wakeup-source") ||
			/* legacy name */
			fwnode_property_read_bool(child, "gpio-key,wakeup");

		if (fwnode_property_read_u32(child, "debounce-interval",
					     &button->debounce_interval))
			button->debounce_interval = 5;
	}

	if (pdata->nbuttons == 0)
		return ERR_PTR(-EINVAL);

	return pdata;
}

static const struct of_device_id gpio_keys_polled_of_match[] = {
	{ .compatible = "gpio-keys-polled", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_keys_polled_of_match);

static int gpio_keys_polled_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct gpio_keys_platform_data *pdata = dev_get_platdata(dev);
	struct gpio_keys_polled_dev *bdev;
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	size_t size;
	int error;
	int i;

	if (!pdata) {
		pdata = gpio_keys_polled_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
		if (!pdata) {
			dev_err(dev, "missing platform data\n");
			return -EINVAL;
		}
	}

	if (!pdata->poll_interval) {
		dev_err(dev, "missing poll_interval value\n");
		return -EINVAL;
	}

	size = sizeof(struct gpio_keys_polled_dev) +
			pdata->nbuttons * sizeof(struct gpio_keys_button_data);
	bdev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!bdev) {
		dev_err(dev, "no memory for private data\n");
		return -ENOMEM;
	}

	poll_dev = devm_input_allocate_polled_device(&pdev->dev);
	if (!poll_dev) {
		dev_err(dev, "no memory for polled device\n");
		return -ENOMEM;
	}

	poll_dev->private = bdev;
	poll_dev->poll = gpio_keys_polled_poll;
	poll_dev->poll_interval = pdata->poll_interval;
	poll_dev->open = gpio_keys_polled_open;
	poll_dev->close = gpio_keys_polled_close;

	input = poll_dev->input;

	input->name = pdev->name;
	input->phys = DRV_NAME"/input0";

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	__set_bit(EV_KEY, input->evbit);
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];
		struct gpio_keys_button_data *bdata = &bdev->data[i];
		unsigned int type = button->type ?: EV_KEY;

		if (button->wakeup) {
			dev_err(dev, DRV_NAME " does not support wakeup\n");
			return -EINVAL;
		}

		/*
		 * Legacy GPIO number so request the GPIO here and
		 * convert it to descriptor.
		 */
		if (!button->gpiod && gpio_is_valid(button->gpio)) {
			unsigned flags = GPIOF_IN;

			if (button->active_low)
				flags |= GPIOF_ACTIVE_LOW;

			error = devm_gpio_request_one(&pdev->dev, button->gpio,
					flags, button->desc ? : DRV_NAME);
			if (error) {
				dev_err(dev, "unable to claim gpio %u, err=%d\n",
					button->gpio, error);
				return error;
			}

			button->gpiod = gpio_to_desc(button->gpio);
		}

		if (IS_ERR(button->gpiod))
			return PTR_ERR(button->gpiod);

		bdata->can_sleep = gpiod_cansleep(button->gpiod);
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
		return error;
	}

	/* report initial state of the buttons */
	for (i = 0; i < pdata->nbuttons; i++)
		gpio_keys_polled_check_state(input, &pdata->buttons[i],
					     &bdev->data[i]);

	return 0;
}

static struct platform_driver gpio_keys_polled_driver = {
	.probe	= gpio_keys_polled_probe,
	.driver	= {
		.name	= DRV_NAME,
		.of_match_table = gpio_keys_polled_of_match,
	},
};
module_platform_driver(gpio_keys_polled_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_DESCRIPTION("Polled GPIO Buttons driver");
MODULE_ALIAS("platform:" DRV_NAME);

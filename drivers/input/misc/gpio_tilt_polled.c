/*
 *  Driver for tilt switches connected via GPIO lines
 *  not capable of generating interrupts
 *
 *  Copyright (C) 2011 Heiko Stuebner <heiko@sntech.de>
 *
 *  based on: drivers/input/keyboard/gpio_keys_polled.c
 *
 *  Copyright (C) 2007-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2010 Nuno Goncalves <nunojpg@gmail.com>
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
#include <linux/input/gpio_tilt.h>

#define DRV_NAME	"gpio-tilt-polled"

struct gpio_tilt_polled_dev {
	struct input_polled_dev *poll_dev;
	struct device *dev;
	const struct gpio_tilt_platform_data *pdata;

	int last_state;

	int threshold;
	int count;
};

static void gpio_tilt_polled_poll(struct input_polled_dev *dev)
{
	struct gpio_tilt_polled_dev *tdev = dev->private;
	const struct gpio_tilt_platform_data *pdata = tdev->pdata;
	struct input_dev *input = dev->input;
	struct gpio_tilt_state *tilt_state = NULL;
	int state, i;

	if (tdev->count < tdev->threshold) {
		tdev->count++;
	} else {
		state = 0;
		for (i = 0; i < pdata->nr_gpios; i++)
			state |= (!!gpio_get_value(pdata->gpios[i].gpio) << i);

		if (state != tdev->last_state) {
			for (i = 0; i < pdata->nr_states; i++)
				if (pdata->states[i].gpios == state)
					tilt_state = &pdata->states[i];

			if (tilt_state) {
				for (i = 0; i < pdata->nr_axes; i++)
					input_report_abs(input,
							 pdata->axes[i].axis,
							 tilt_state->axes[i]);

				input_sync(input);
			}

			tdev->count = 0;
			tdev->last_state = state;
		}
	}
}

static void gpio_tilt_polled_open(struct input_polled_dev *dev)
{
	struct gpio_tilt_polled_dev *tdev = dev->private;
	const struct gpio_tilt_platform_data *pdata = tdev->pdata;

	if (pdata->enable)
		pdata->enable(tdev->dev);

	/* report initial state of the axes */
	tdev->last_state = -1;
	tdev->count = tdev->threshold;
	gpio_tilt_polled_poll(tdev->poll_dev);
}

static void gpio_tilt_polled_close(struct input_polled_dev *dev)
{
	struct gpio_tilt_polled_dev *tdev = dev->private;
	const struct gpio_tilt_platform_data *pdata = tdev->pdata;

	if (pdata->disable)
		pdata->disable(tdev->dev);
}

static int __devinit gpio_tilt_polled_probe(struct platform_device *pdev)
{
	const struct gpio_tilt_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct gpio_tilt_polled_dev *tdev;
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	int error, i;

	if (!pdata || !pdata->poll_interval)
		return -EINVAL;

	tdev = kzalloc(sizeof(struct gpio_tilt_polled_dev), GFP_KERNEL);
	if (!tdev) {
		dev_err(dev, "no memory for private data\n");
		return -ENOMEM;
	}

	error = gpio_request_array(pdata->gpios, pdata->nr_gpios);
	if (error) {
		dev_err(dev,
			"Could not request tilt GPIOs: %d\n", error);
		goto err_free_tdev;
	}

	poll_dev = input_allocate_polled_device();
	if (!poll_dev) {
		dev_err(dev, "no memory for polled device\n");
		error = -ENOMEM;
		goto err_free_gpios;
	}

	poll_dev->private = tdev;
	poll_dev->poll = gpio_tilt_polled_poll;
	poll_dev->poll_interval = pdata->poll_interval;
	poll_dev->open = gpio_tilt_polled_open;
	poll_dev->close = gpio_tilt_polled_close;

	input = poll_dev->input;

	input->name = pdev->name;
	input->phys = DRV_NAME"/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	__set_bit(EV_ABS, input->evbit);
	for (i = 0; i < pdata->nr_axes; i++)
		input_set_abs_params(input, pdata->axes[i].axis,
				     pdata->axes[i].min, pdata->axes[i].max,
				     pdata->axes[i].fuzz, pdata->axes[i].flat);

	tdev->threshold = DIV_ROUND_UP(pdata->debounce_interval,
				       pdata->poll_interval);

	tdev->poll_dev = poll_dev;
	tdev->dev = dev;
	tdev->pdata = pdata;

	error = input_register_polled_device(poll_dev);
	if (error) {
		dev_err(dev, "unable to register polled device, err=%d\n",
			error);
		goto err_free_polldev;
	}

	platform_set_drvdata(pdev, tdev);

	return 0;

err_free_polldev:
	input_free_polled_device(poll_dev);
err_free_gpios:
	gpio_free_array(pdata->gpios, pdata->nr_gpios);
err_free_tdev:
	kfree(tdev);

	return error;
}

static int __devexit gpio_tilt_polled_remove(struct platform_device *pdev)
{
	struct gpio_tilt_polled_dev *tdev = platform_get_drvdata(pdev);
	const struct gpio_tilt_platform_data *pdata = tdev->pdata;

	platform_set_drvdata(pdev, NULL);

	input_unregister_polled_device(tdev->poll_dev);
	input_free_polled_device(tdev->poll_dev);

	gpio_free_array(pdata->gpios, pdata->nr_gpios);

	kfree(tdev);

	return 0;
}

static struct platform_driver gpio_tilt_polled_driver = {
	.probe	= gpio_tilt_polled_probe,
	.remove	= gpio_tilt_polled_remove,
	.driver	= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(gpio_tilt_polled_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Polled GPIO tilt driver");
MODULE_ALIAS("platform:" DRV_NAME);

// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio_keys.h>
#include <linux/property.h>

#define DRV_NAME	"gpio-keys-polled"

struct gpio_keys_button_data {
	struct gpio_desc *gpiod;
	int last_state;
	int count;
	int threshold;
};

struct gpio_keys_polled_dev {
	struct input_dev *input;
	struct device *dev;
	const struct gpio_keys_platform_data *pdata;
	unsigned long rel_axis_seen[BITS_TO_LONGS(REL_CNT)];
	unsigned long abs_axis_seen[BITS_TO_LONGS(ABS_CNT)];
	struct gpio_keys_button_data data[];
};

static void gpio_keys_button_event(struct input_dev *input,
				   const struct gpio_keys_button *button,
				   int state)
{
	struct gpio_keys_polled_dev *bdev = input_get_drvdata(input);
	unsigned int type = button->type ?: EV_KEY;

	if (type == EV_REL) {
		if (state) {
			input_event(input, type, button->code, button->value);
			__set_bit(button->code, bdev->rel_axis_seen);
		}
	} else if (type == EV_ABS) {
		if (state) {
			input_event(input, type, button->code, button->value);
			__set_bit(button->code, bdev->abs_axis_seen);
		}
	} else {
		input_event(input, type, button->code, state);
		input_sync(input);
	}
}

static void gpio_keys_polled_check_state(struct input_dev *input,
					 const struct gpio_keys_button *button,
					 struct gpio_keys_button_data *bdata)
{
	int state;

	state = gpiod_get_value_cansleep(bdata->gpiod);
	if (state < 0) {
		dev_err(input->dev.parent,
			"failed to get gpio state: %d\n", state);
	} else {
		gpio_keys_button_event(input, button, state);

		if (state != bdata->last_state) {
			bdata->count = 0;
			bdata->last_state = state;
		}
	}
}

static void gpio_keys_polled_poll(struct input_dev *input)
{
	struct gpio_keys_polled_dev *bdev = input_get_drvdata(input);
	const struct gpio_keys_platform_data *pdata = bdev->pdata;
	int i;

	memset(bdev->rel_axis_seen, 0, sizeof(bdev->rel_axis_seen));
	memset(bdev->abs_axis_seen, 0, sizeof(bdev->abs_axis_seen));

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button_data *bdata = &bdev->data[i];

		if (bdata->count < bdata->threshold) {
			bdata->count++;
			gpio_keys_button_event(input, &pdata->buttons[i],
					       bdata->last_state);
		} else {
			gpio_keys_polled_check_state(input, &pdata->buttons[i],
						     bdata);
		}
	}

	for_each_set_bit(i, input->relbit, REL_CNT) {
		if (!test_bit(i, bdev->rel_axis_seen))
			input_event(input, EV_REL, i, 0);
	}

	for_each_set_bit(i, input->absbit, ABS_CNT) {
		if (!test_bit(i, bdev->abs_axis_seen))
			input_event(input, EV_ABS, i, 0);
	}

	input_sync(input);
}

static int gpio_keys_polled_open(struct input_dev *input)
{
	struct gpio_keys_polled_dev *bdev = input_get_drvdata(input);
	const struct gpio_keys_platform_data *pdata = bdev->pdata;

	if (pdata->enable)
		pdata->enable(bdev->dev);

	return 0;
}

static void gpio_keys_polled_close(struct input_dev *input)
{
	struct gpio_keys_polled_dev *bdev = input_get_drvdata(input);
	const struct gpio_keys_platform_data *pdata = bdev->pdata;

	if (pdata->disable)
		pdata->disable(bdev->dev);
}

static struct gpio_keys_platform_data *
gpio_keys_polled_get_devtree_pdata(struct device *dev)
{
	struct gpio_keys_platform_data *pdata;
	struct gpio_keys_button *button;
	struct fwnode_handle *child;
	int nbuttons;

	nbuttons = device_get_child_node_count(dev);
	if (nbuttons == 0)
		return ERR_PTR(-EINVAL);

	pdata = devm_kzalloc(dev, sizeof(*pdata) + nbuttons * sizeof(*button),
			     GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	button = (struct gpio_keys_button *)(pdata + 1);

	pdata->buttons = button;
	pdata->nbuttons = nbuttons;

	pdata->rep = device_property_present(dev, "autorepeat");
	device_property_read_u32(dev, "poll-interval", &pdata->poll_interval);

	device_property_read_string(dev, "label", &pdata->name);

	device_for_each_child_node(dev, child) {
		if (fwnode_property_read_u32(child, "linux,code",
					     &button->code)) {
			dev_err(dev, "button without keycode\n");
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}

		fwnode_property_read_string(child, "label", &button->desc);

		if (fwnode_property_read_u32(child, "linux,input-type",
					     &button->type))
			button->type = EV_KEY;

		if (fwnode_property_read_u32(child, "linux,input-value",
					     (u32 *)&button->value))
			button->value = 1;

		button->wakeup =
			fwnode_property_read_bool(child, "wakeup-source") ||
			/* legacy name */
			fwnode_property_read_bool(child, "gpio-key,wakeup");

		if (fwnode_property_read_u32(child, "debounce-interval",
					     &button->debounce_interval))
			button->debounce_interval = 5;

		button++;
	}

	return pdata;
}

static void gpio_keys_polled_set_abs_params(struct input_dev *input,
	const struct gpio_keys_platform_data *pdata, unsigned int code)
{
	int i, min = 0, max = 0;

	for (i = 0; i < pdata->nbuttons; i++) {
		const struct gpio_keys_button *button = &pdata->buttons[i];

		if (button->type != EV_ABS || button->code != code)
			continue;

		if (button->value < min)
			min = button->value;
		if (button->value > max)
			max = button->value;
	}

	input_set_abs_params(input, code, min, max, 0, 0);
}

static const struct of_device_id gpio_keys_polled_of_match[] = {
	{ .compatible = "gpio-keys-polled", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_keys_polled_of_match);

static int gpio_keys_polled_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child = NULL;
	const struct gpio_keys_platform_data *pdata = dev_get_platdata(dev);
	struct gpio_keys_polled_dev *bdev;
	struct input_dev *input;
	int error;
	int i;

	if (!pdata) {
		pdata = gpio_keys_polled_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	if (!pdata->poll_interval) {
		dev_err(dev, "missing poll_interval value\n");
		return -EINVAL;
	}

	bdev = devm_kzalloc(dev, struct_size(bdev, data, pdata->nbuttons),
			    GFP_KERNEL);
	if (!bdev) {
		dev_err(dev, "no memory for private data\n");
		return -ENOMEM;
	}

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "no memory for input device\n");
		return -ENOMEM;
	}

	input_set_drvdata(input, bdev);

	input->name = pdata->name ?: pdev->name;
	input->phys = DRV_NAME"/input0";

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	input->open = gpio_keys_polled_open;
	input->close = gpio_keys_polled_close;

	__set_bit(EV_KEY, input->evbit);
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < pdata->nbuttons; i++) {
		const struct gpio_keys_button *button = &pdata->buttons[i];
		struct gpio_keys_button_data *bdata = &bdev->data[i];
		unsigned int type = button->type ?: EV_KEY;

		if (button->wakeup) {
			dev_err(dev, DRV_NAME " does not support wakeup\n");
			fwnode_handle_put(child);
			return -EINVAL;
		}

		if (!dev_get_platdata(dev)) {
			/* No legacy static platform data */
			child = device_get_next_child_node(dev, child);
			if (!child) {
				dev_err(dev, "missing child device node\n");
				return -EINVAL;
			}

			bdata->gpiod = devm_fwnode_gpiod_get(dev, child,
							     NULL, GPIOD_IN,
							     button->desc);
			if (IS_ERR(bdata->gpiod)) {
				error = PTR_ERR(bdata->gpiod);
				if (error != -EPROBE_DEFER)
					dev_err(dev,
						"failed to get gpio: %d\n",
						error);
				fwnode_handle_put(child);
				return error;
			}
		} else if (gpio_is_valid(button->gpio)) {
			/*
			 * Legacy GPIO number so request the GPIO here and
			 * convert it to descriptor.
			 */
			unsigned flags = GPIOF_IN;

			if (button->active_low)
				flags |= GPIOF_ACTIVE_LOW;

			error = devm_gpio_request_one(dev, button->gpio,
					flags, button->desc ? : DRV_NAME);
			if (error)
				return dev_err_probe(dev, error,
						     "unable to claim gpio %u\n",
						     button->gpio);

			bdata->gpiod = gpio_to_desc(button->gpio);
			if (!bdata->gpiod) {
				dev_err(dev,
					"unable to convert gpio %u to descriptor\n",
					button->gpio);
				return -EINVAL;
			}
		}

		bdata->last_state = -1;
		bdata->threshold = DIV_ROUND_UP(button->debounce_interval,
						pdata->poll_interval);

		input_set_capability(input, type, button->code);
		if (type == EV_ABS)
			gpio_keys_polled_set_abs_params(input, pdata,
							button->code);
	}

	fwnode_handle_put(child);

	bdev->input = input;
	bdev->dev = dev;
	bdev->pdata = pdata;

	error = input_setup_polling(input, gpio_keys_polled_poll);
	if (error) {
		dev_err(dev, "unable to set up polling, err=%d\n", error);
		return error;
	}

	input_set_poll_interval(input, pdata->poll_interval);

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "unable to register polled device, err=%d\n",
			error);
		return error;
	}

	/* report initial state of the buttons */
	for (i = 0; i < pdata->nbuttons; i++)
		gpio_keys_polled_check_state(input, &pdata->buttons[i],
					     &bdev->data[i]);

	input_sync(input);

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

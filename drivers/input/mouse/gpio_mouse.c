// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for simulating a mouse on GPIO lines.
 *
 * Copyright (C) 2007 Atmel Corporation
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio/consumer.h>
#include <linux/property.h>
#include <linux/of.h>

/**
 * struct gpio_mouse
 * @scan_ms: the scan interval in milliseconds.
 * @up: GPIO line for up value.
 * @down: GPIO line for down value.
 * @left: GPIO line for left value.
 * @right: GPIO line for right value.
 * @bleft: GPIO line for left button.
 * @bmiddle: GPIO line for middle button.
 * @bright: GPIO line for right button.
 *
 * This struct must be added to the platform_device in the board code.
 * It is used by the gpio_mouse driver to setup GPIO lines and to
 * calculate mouse movement.
 */
struct gpio_mouse {
	u32 scan_ms;
	struct gpio_desc *up;
	struct gpio_desc *down;
	struct gpio_desc *left;
	struct gpio_desc *right;
	struct gpio_desc *bleft;
	struct gpio_desc *bmiddle;
	struct gpio_desc *bright;
};

/*
 * Timer function which is run every scan_ms ms when the device is opened.
 * The dev input variable is set to the input_dev pointer.
 */
static void gpio_mouse_scan(struct input_dev *input)
{
	struct gpio_mouse *gpio = input_get_drvdata(input);
	int x, y;

	if (gpio->bleft)
		input_report_key(input, BTN_LEFT,
				 gpiod_get_value(gpio->bleft));
	if (gpio->bmiddle)
		input_report_key(input, BTN_MIDDLE,
				 gpiod_get_value(gpio->bmiddle));
	if (gpio->bright)
		input_report_key(input, BTN_RIGHT,
				 gpiod_get_value(gpio->bright));

	x = gpiod_get_value(gpio->right) - gpiod_get_value(gpio->left);
	y = gpiod_get_value(gpio->down) - gpiod_get_value(gpio->up);

	input_report_rel(input, REL_X, x);
	input_report_rel(input, REL_Y, y);
	input_sync(input);
}

static int gpio_mouse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_mouse *gmouse;
	struct input_dev *input;
	int error;

	gmouse = devm_kzalloc(dev, sizeof(*gmouse), GFP_KERNEL);
	if (!gmouse)
		return -ENOMEM;

	/* Assign some default scanning time */
	error = device_property_read_u32(dev, "scan-interval-ms",
					 &gmouse->scan_ms);
	if (error || gmouse->scan_ms == 0) {
		dev_warn(dev, "invalid scan time, set to 50 ms\n");
		gmouse->scan_ms = 50;
	}

	gmouse->up = devm_gpiod_get(dev, "up", GPIOD_IN);
	if (IS_ERR(gmouse->up))
		return PTR_ERR(gmouse->up);
	gmouse->down = devm_gpiod_get(dev, "down", GPIOD_IN);
	if (IS_ERR(gmouse->down))
		return PTR_ERR(gmouse->down);
	gmouse->left = devm_gpiod_get(dev, "left", GPIOD_IN);
	if (IS_ERR(gmouse->left))
		return PTR_ERR(gmouse->left);
	gmouse->right = devm_gpiod_get(dev, "right", GPIOD_IN);
	if (IS_ERR(gmouse->right))
		return PTR_ERR(gmouse->right);

	gmouse->bleft = devm_gpiod_get_optional(dev, "button-left", GPIOD_IN);
	if (IS_ERR(gmouse->bleft))
		return PTR_ERR(gmouse->bleft);
	gmouse->bmiddle = devm_gpiod_get_optional(dev, "button-middle",
						  GPIOD_IN);
	if (IS_ERR(gmouse->bmiddle))
		return PTR_ERR(gmouse->bmiddle);
	gmouse->bright = devm_gpiod_get_optional(dev, "button-right",
						 GPIOD_IN);
	if (IS_ERR(gmouse->bright))
		return PTR_ERR(gmouse->bright);

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = pdev->name;
	input->id.bustype = BUS_HOST;

	input_set_drvdata(input, gmouse);

	input_set_capability(input, EV_REL, REL_X);
	input_set_capability(input, EV_REL, REL_Y);
	if (gmouse->bleft)
		input_set_capability(input, EV_KEY, BTN_LEFT);
	if (gmouse->bmiddle)
		input_set_capability(input, EV_KEY, BTN_MIDDLE);
	if (gmouse->bright)
		input_set_capability(input, EV_KEY, BTN_RIGHT);

	error = input_setup_polling(input, gpio_mouse_scan);
	if (error)
		return error;

	input_set_poll_interval(input, gmouse->scan_ms);

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "could not register input device\n");
		return error;
	}

	dev_dbg(dev, "%d ms scan time, buttons: %s%s%s\n",
		gmouse->scan_ms,
		gmouse->bleft ? "" : "left ",
		gmouse->bmiddle ? "" : "middle ",
		gmouse->bright ? "" : "right");

	return 0;
}

static const struct of_device_id gpio_mouse_of_match[] = {
	{ .compatible = "gpio-mouse", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_mouse_of_match);

static struct platform_driver gpio_mouse_device_driver = {
	.probe		= gpio_mouse_probe,
	.driver		= {
		.name	= "gpio_mouse",
		.of_match_table = gpio_mouse_of_match,
	}
};
module_platform_driver(gpio_mouse_device_driver);

MODULE_AUTHOR("Hans-Christian Egtvedt <egtvedt@samfundet.no>");
MODULE_DESCRIPTION("GPIO mouse driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio_mouse"); /* work with hotplug and coldplug */

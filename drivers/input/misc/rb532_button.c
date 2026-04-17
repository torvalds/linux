// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for the S1 button on Routerboard 532
 *
 * Copyright (C) 2009  Phil Sutter <n0-1@freewrt.org>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>

#include <asm/mach-rc32434/gpio.h>
#include <asm/mach-rc32434/rb.h>

#define DRV_NAME "rb532-button"

#define RB532_BTN_RATE 100 /* msec */
#define RB532_BTN_KSYM BTN_0

/**
 * struct rb532_button - RB532 button information
 * @gpio: GPIO connected to the button
 */
struct rb532_button {
	struct gpio_desc	*gpio;
};

/* The S1 button state is provided by GPIO pin 1. But as this
 * pin is also used for uart input as alternate function, the
 * operational modes must be switched first:
 * 1) disable uart using set_latch_u5()
 * 2) turn off alternate function implicitly through
 *    gpio_direction_input()
 * 3) read the GPIO's current value
 * 4) undo step 2 by enabling alternate function (in this
 *    mode the GPIO direction is fixed, so no change needed)
 * 5) turn on uart again
 * The GPIO value occurs to be inverted, so pin high means
 * button is not pressed.
 */
static bool rb532_button_pressed(struct rb532_button *button)
{
	int val;

	set_latch_u5(0, LO_FOFF);
	gpiod_direction_input(button->gpio);

	val = gpiod_get_value(button->gpio);

	rb532_gpio_set_func(GPIO_BTN_S1);
	set_latch_u5(LO_FOFF, 0);

	return val;
}

static void rb532_button_poll(struct input_dev *input)
{
	struct rb532_button *button = input_get_drvdata(input);

	input_report_key(input, RB532_BTN_KSYM, rb532_button_pressed(button));
	input_sync(input);
}

static int rb532_button_probe(struct platform_device *pdev)
{
	struct rb532_button *button;
	struct input_dev *input;
	int error;

	button = devm_kzalloc(&pdev->dev, sizeof(*button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	button->gpio = devm_gpiod_get(&pdev->dev, "button", GPIOD_IN);
	if (IS_ERR(button->gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(button->gpio),
				     "error getting button GPIO\n");

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;
	input_set_drvdata(input, button);

	input->name = "rb532 button";
	input->phys = "rb532/button0";
	input->id.bustype = BUS_HOST;

	input_set_capability(input, EV_KEY, RB532_BTN_KSYM);

	error = input_setup_polling(input, rb532_button_poll);
	if (error)
		return error;

	input_set_poll_interval(input, RB532_BTN_RATE);

	error = input_register_device(input);
	if (error)
		return error;

	platform_set_drvdata(pdev, button);

	return 0;
}

static struct platform_driver rb532_button_driver = {
	.probe = rb532_button_probe,
	.driver = {
		.name = DRV_NAME,
	},
};
module_platform_driver(rb532_button_driver);

MODULE_AUTHOR("Phil Sutter <n0-1@freewrt.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Support for S1 button on Routerboard 532");
MODULE_ALIAS("platform:" DRV_NAME);

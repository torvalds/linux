// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for the S1 button on Routerboard 532
 *
 * Copyright (C) 2009  Phil Sutter <n0-1@freewrt.org>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/mach-rc32434/gpio.h>
#include <asm/mach-rc32434/rb.h>

#define DRV_NAME "rb532-button"

#define RB532_BTN_RATE 100 /* msec */
#define RB532_BTN_KSYM BTN_0

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
static bool rb532_button_pressed(void)
{
	int val;

	set_latch_u5(0, LO_FOFF);
	gpio_direction_input(GPIO_BTN_S1);

	val = gpio_get_value(GPIO_BTN_S1);

	rb532_gpio_set_func(GPIO_BTN_S1);
	set_latch_u5(LO_FOFF, 0);

	return !val;
}

static void rb532_button_poll(struct input_dev *input)
{
	input_report_key(input, RB532_BTN_KSYM, rb532_button_pressed());
	input_sync(input);
}

static int rb532_button_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	int error;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driven charlieplex keypad driver
 *
 * Copyright (c) 2026 Hugo Villeneuve <hvilleneuve@dimonoff.com>
 *
 * Based on matrix_keyboard.c
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/string_helpers.h>
#include <linux/types.h>

struct charlieplex_keypad {
	struct input_dev *input_dev;
	struct gpio_descs *line_gpios;
	unsigned int nlines;
	unsigned int settling_time_us;
	unsigned int debounce_threshold;
	unsigned int debounce_count;
	int debounce_code;
	int current_code;
};

static void charlieplex_keypad_report_key(struct input_dev *input)
{
	struct charlieplex_keypad *keypad = input_get_drvdata(input);
	const unsigned short *keycodes = input->keycode;

	if (keypad->current_code > 0) {
		input_event(input, EV_MSC, MSC_SCAN, keypad->current_code);
		input_report_key(input, keycodes[keypad->current_code], 0);
		input_sync(input);
	}

	if (keypad->debounce_code) {
		input_event(input, EV_MSC, MSC_SCAN, keypad->debounce_code);
		input_report_key(input, keycodes[keypad->debounce_code], 1);
		input_sync(input);
	}

	keypad->current_code = keypad->debounce_code;
}

static void charlieplex_keypad_check_switch_change(struct input_dev *input,
						   unsigned int code)
{
	struct charlieplex_keypad *keypad = input_get_drvdata(input);

	if (code != keypad->debounce_code) {
		keypad->debounce_count = 0;
		keypad->debounce_code = code;
	}

	if (keypad->debounce_code != keypad->current_code) {
		if (keypad->debounce_count++ >= keypad->debounce_threshold)
			charlieplex_keypad_report_key(input);
	}
}

static int charlieplex_keypad_scan_line(struct charlieplex_keypad *keypad,
					unsigned int oline)
{
	struct gpio_descs *line_gpios = keypad->line_gpios;
	DECLARE_BITMAP(values, MATRIX_MAX_ROWS);
	int err;

	/* Activate only one line as output at a time. */
	gpiod_direction_output(line_gpios->desc[oline], 1);

	if (keypad->settling_time_us)
		fsleep(keypad->settling_time_us);

	/* Read input on all other lines. */
	err = gpiod_get_array_value_cansleep(line_gpios->ndescs, line_gpios->desc,
					     line_gpios->info, values);

	gpiod_direction_input(line_gpios->desc[oline]);

	if (err)
		return err;

	for (unsigned int iline = 0; iline < keypad->nlines; iline++) {
		if (iline == oline)
			continue; /* Do not read active output line. */

		/* Check if GPIO is asserted. */
		if (test_bit(iline, values))
			return MATRIX_SCAN_CODE(oline, iline,
						get_count_order(keypad->nlines));
	}

	return 0;
}

static void charlieplex_keypad_poll(struct input_dev *input)
{
	struct charlieplex_keypad *keypad = input_get_drvdata(input);
	int code = 0;

	for (unsigned int oline = 0; oline < keypad->nlines; oline++) {
		code = charlieplex_keypad_scan_line(keypad, oline);
		if (code != 0)
			break;
	}

	if (code >= 0)
		charlieplex_keypad_check_switch_change(input, code);
}

static int charlieplex_keypad_init_gpio(struct platform_device *pdev,
					struct charlieplex_keypad *keypad)
{
	char **pin_names;
	char label[32];

	snprintf(label, sizeof(label), "%s-pin", pdev->name);

	keypad->line_gpios = devm_gpiod_get_array(&pdev->dev, "line", GPIOD_IN);
	if (IS_ERR(keypad->line_gpios))
		return PTR_ERR(keypad->line_gpios);

	keypad->nlines = keypad->line_gpios->ndescs;

	if (keypad->nlines > MATRIX_MAX_ROWS)
		return -EINVAL;

	pin_names = devm_kasprintf_strarray(&pdev->dev, label, keypad->nlines);
	if (IS_ERR(pin_names))
		return PTR_ERR(pin_names);

	for (unsigned int i = 0; i < keypad->line_gpios->ndescs; i++)
		gpiod_set_consumer_name(keypad->line_gpios->desc[i], pin_names[i]);

	return 0;
}

static int charlieplex_keypad_probe(struct platform_device *pdev)
{
	struct charlieplex_keypad *keypad;
	struct input_dev *input_dev;
	unsigned int debounce_interval_ms = 5;
	unsigned int poll_interval_ms;
	int err;

	keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad), GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	keypad->input_dev = input_dev;

	err = device_property_read_u32(&pdev->dev, "poll-interval", &poll_interval_ms);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "failed to parse 'poll-interval' property\n");

	if (poll_interval_ms == 0)
		return dev_err_probe(&pdev->dev, -EINVAL, "invalid 'poll-interval' value\n");

	device_property_read_u32(&pdev->dev, "debounce-delay-ms", &debounce_interval_ms);
	device_property_read_u32(&pdev->dev, "settling-time-us", &keypad->settling_time_us);

	keypad->current_code = -1;
	keypad->debounce_code = -1;
	keypad->debounce_threshold = DIV_ROUND_UP(debounce_interval_ms, poll_interval_ms);

	err = charlieplex_keypad_init_gpio(pdev, keypad);
	if (err)
		return err;

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;

	err = matrix_keypad_build_keymap(NULL, NULL, keypad->nlines,
					 keypad->nlines, NULL, input_dev);
	if (err)
		return dev_err_probe(&pdev->dev, err, "failed to build keymap\n");

	if (device_property_read_bool(&pdev->dev, "autorepeat"))
		__set_bit(EV_REP, input_dev->evbit);

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	err = input_setup_polling(input_dev, charlieplex_keypad_poll);
	if (err)
		return dev_err_probe(&pdev->dev, err, "unable to set up polling\n");

	input_set_poll_interval(input_dev, poll_interval_ms);

	input_set_drvdata(input_dev, keypad);

	err = input_register_device(keypad->input_dev);
	if (err)
		return err;

	return 0;
}

static const struct of_device_id charlieplex_keypad_dt_match[] = {
	{ .compatible = "gpio-charlieplex-keypad" },
	{ }
};
MODULE_DEVICE_TABLE(of, charlieplex_keypad_dt_match);

static struct platform_driver charlieplex_keypad_driver = {
	.probe		= charlieplex_keypad_probe,
	.driver		= {
		.name	= "charlieplex-keypad",
		.of_match_table = charlieplex_keypad_dt_match,
	},
};
module_platform_driver(charlieplex_keypad_driver);

MODULE_AUTHOR("Hugo Villeneuve <hvilleneuve@dimonoff.com>");
MODULE_DESCRIPTION("GPIO driven charlieplex keypad driver");
MODULE_LICENSE("GPL");

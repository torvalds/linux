/*
 *  Keyboard backlight LED driver for Chrome OS.
 *
 *  Copyright (C) 2012 Google, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Keyboard LED ACPI Device must be defined in firmware */
#define ACPI_KEYBOARD_BACKLIGHT_DEVICE	"\\_SB.KBLT"
#define ACPI_KEYBOARD_BACKLIGHT_READ	ACPI_KEYBOARD_BACKLIGHT_DEVICE ".KBQC"
#define ACPI_KEYBOARD_BACKLIGHT_WRITE	ACPI_KEYBOARD_BACKLIGHT_DEVICE ".KBCM"

#define ACPI_KEYBOARD_BACKLIGHT_MAX		100

static void keyboard_led_set_brightness(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	union acpi_object param;
	struct acpi_object_list input;
	acpi_status status;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = brightness;
	input.count = 1;
	input.pointer = &param;

	status = acpi_evaluate_object(NULL, ACPI_KEYBOARD_BACKLIGHT_WRITE,
				      &input, NULL);
	if (ACPI_FAILURE(status))
		dev_err(cdev->dev, "Error setting keyboard LED value: %d\n",
			status);
}

static enum led_brightness
keyboard_led_get_brightness(struct led_classdev *cdev)
{
	unsigned long long brightness;
	acpi_status status;

	status = acpi_evaluate_integer(NULL, ACPI_KEYBOARD_BACKLIGHT_READ,
				       NULL, &brightness);
	if (ACPI_FAILURE(status)) {
		dev_err(cdev->dev, "Error getting keyboard LED value: %d\n",
			status);
		return -EIO;
	}

	return brightness;
}

static int keyboard_led_probe(struct platform_device *pdev)
{
	struct led_classdev *cdev;
	acpi_handle handle;
	acpi_status status;
	int error;

	/* Look for the keyboard LED ACPI Device */
	status = acpi_get_handle(ACPI_ROOT_OBJECT,
				 ACPI_KEYBOARD_BACKLIGHT_DEVICE,
				 &handle);
	if (ACPI_FAILURE(status)) {
		dev_err(&pdev->dev, "Unable to find ACPI device %s: %d\n",
			ACPI_KEYBOARD_BACKLIGHT_DEVICE, status);
		return -ENXIO;
	}

	cdev = devm_kzalloc(&pdev->dev, sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->name = "chromeos::kbd_backlight";
	cdev->max_brightness = ACPI_KEYBOARD_BACKLIGHT_MAX;
	cdev->flags |= LED_CORE_SUSPENDRESUME;
	cdev->brightness_set = keyboard_led_set_brightness;
	cdev->brightness_get = keyboard_led_get_brightness;

	error = devm_led_classdev_register(&pdev->dev, cdev);
	if (error)
		return error;

	return 0;
}

static const struct acpi_device_id keyboard_led_id[] = {
	{ "GOOG0002", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, keyboard_led_id);

static struct platform_driver keyboard_led_driver = {
	.driver		= {
		.name	= "chromeos-keyboard-leds",
		.acpi_match_table = ACPI_PTR(keyboard_led_id),
	},
	.probe		= keyboard_led_probe,
};
module_platform_driver(keyboard_led_driver);

MODULE_AUTHOR("Simon Que <sque@chromium.org>");
MODULE_DESCRIPTION("ChromeOS Keyboard backlight LED Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:chromeos-keyboard-leds");

// SPDX-License-Identifier: GPL-2.0+
/*
 * System76 ACPI Driver
 *
 * Copyright (C) 2019 System76
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/types.h>

struct system76_data {
	struct acpi_device *acpi_dev;
	struct led_classdev ap_led;
	struct led_classdev kb_led;
	enum led_brightness kb_brightness;
	enum led_brightness kb_toggle_brightness;
	int kb_color;
};

static const struct acpi_device_id device_ids[] = {
	{"17761776", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, device_ids);

// Array of keyboard LED brightness levels
static const enum led_brightness kb_levels[] = {
	48,
	72,
	96,
	144,
	192,
	255
};

// Array of keyboard LED colors in 24-bit RGB format
static const int kb_colors[] = {
	0xFFFFFF,
	0x0000FF,
	0xFF0000,
	0xFF00FF,
	0x00FF00,
	0x00FFFF,
	0xFFFF00
};

// Get a System76 ACPI device value by name
static int system76_get(struct system76_data *data, char *method)
{
	acpi_handle handle;
	acpi_status status;
	unsigned long long ret = 0;

	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_integer(handle, method, NULL, &ret);
	if (ACPI_SUCCESS(status))
		return (int)ret;
	else
		return -1;
}

// Set a System76 ACPI device value by name
static int system76_set(struct system76_data *data, char *method, int value)
{
	union acpi_object obj;
	struct acpi_object_list obj_list;
	acpi_handle handle;
	acpi_status status;

	obj.type = ACPI_TYPE_INTEGER;
	obj.integer.value = value;
	obj_list.count = 1;
	obj_list.pointer = &obj;
	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_object(handle, method, &obj_list, NULL);
	if (ACPI_SUCCESS(status))
		return 0;
	else
		return -1;
}

// Get the airplane mode LED brightness
static enum led_brightness ap_led_get(struct led_classdev *led)
{
	struct system76_data *data;
	int value;

	data = container_of(led, struct system76_data, ap_led);
	value = system76_get(data, "GAPL");
	if (value > 0)
		return (enum led_brightness)value;
	else
		return LED_OFF;
}

// Set the airplane mode LED brightness
static void ap_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct system76_data *data;

	data = container_of(led, struct system76_data, ap_led);
	system76_set(data, "SAPL", value == LED_OFF ? 0 : 1);
}

// Get the last set keyboard LED brightness
static enum led_brightness kb_led_get(struct led_classdev *led)
{
	struct system76_data *data;

	data = container_of(led, struct system76_data, kb_led);
	return data->kb_brightness;
}

// Set the keyboard LED brightness
static void kb_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct system76_data *data;

	data = container_of(led, struct system76_data, kb_led);
	data->kb_brightness = value;
	system76_set(data, "SKBL", (int)data->kb_brightness);
}

// Get the last set keyboard LED color
static ssize_t kb_led_color_show(
	struct device *dev,
	struct device_attribute *dev_attr,
	char *buf)
{
	struct led_classdev *led;
	struct system76_data *data;

	led = (struct led_classdev *)dev->driver_data;
	data = container_of(led, struct system76_data, kb_led);
	return sprintf(buf, "%06X\n", data->kb_color);
}

// Set the keyboard LED color
static ssize_t kb_led_color_store(
	struct device *dev,
	struct device_attribute *dev_attr,
	const char *buf,
	size_t size)
{
	struct led_classdev *led;
	struct system76_data *data;
	unsigned int val;
	int ret;

	led = (struct led_classdev *)dev->driver_data;
	data = container_of(led, struct system76_data, kb_led);
	ret = kstrtouint(buf, 16, &val);
	if (ret)
		return ret;
	if (val > 0xFFFFFF)
		return -EINVAL;
	data->kb_color = (int)val;
	system76_set(data, "SKBC", data->kb_color);

	return size;
}

static const struct device_attribute kb_led_color_dev_attr = {
	.attr = {
		.name = "color",
		.mode = 0644,
	},
	.show = kb_led_color_show,
	.store = kb_led_color_store,
};

// Notify that the keyboard LED was changed by hardware
static void kb_led_notify(struct system76_data *data)
{
	led_classdev_notify_brightness_hw_changed(
		&data->kb_led,
		data->kb_brightness
	);
}

// Read keyboard LED brightness as set by hardware
static void kb_led_hotkey_hardware(struct system76_data *data)
{
	int value;

	value = system76_get(data, "GKBL");
	if (value < 0)
		return;
	data->kb_brightness = value;
	kb_led_notify(data);
}

// Toggle the keyboard LED
static void kb_led_hotkey_toggle(struct system76_data *data)
{
	if (data->kb_brightness > 0) {
		data->kb_toggle_brightness = data->kb_brightness;
		kb_led_set(&data->kb_led, 0);
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

// Decrease the keyboard LED brightness
static void kb_led_hotkey_down(struct system76_data *data)
{
	int i;

	if (data->kb_brightness > 0) {
		for (i = ARRAY_SIZE(kb_levels); i > 0; i--) {
			if (kb_levels[i - 1] < data->kb_brightness) {
				kb_led_set(&data->kb_led, kb_levels[i - 1]);
				break;
			}
		}
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

// Increase the keyboard LED brightness
static void kb_led_hotkey_up(struct system76_data *data)
{
	int i;

	if (data->kb_brightness > 0) {
		for (i = 0; i < ARRAY_SIZE(kb_levels); i++) {
			if (kb_levels[i] > data->kb_brightness) {
				kb_led_set(&data->kb_led, kb_levels[i]);
				break;
			}
		}
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

// Cycle the keyboard LED color
static void kb_led_hotkey_color(struct system76_data *data)
{
	int i;

	if (data->kb_color < 0)
		return;
	if (data->kb_brightness > 0) {
		for (i = 0; i < ARRAY_SIZE(kb_colors); i++) {
			if (kb_colors[i] == data->kb_color)
				break;
		}
		i += 1;
		if (i >= ARRAY_SIZE(kb_colors))
			i = 0;
		data->kb_color = kb_colors[i];
		system76_set(data, "SKBC", data->kb_color);
	} else {
		kb_led_set(&data->kb_led, data->kb_toggle_brightness);
	}
	kb_led_notify(data);
}

// Handle ACPI notification
static void system76_notify(struct acpi_device *acpi_dev, u32 event)
{
	struct system76_data *data;

	data = acpi_driver_data(acpi_dev);
	switch (event) {
	case 0x80:
		kb_led_hotkey_hardware(data);
		break;
	case 0x81:
		kb_led_hotkey_toggle(data);
		break;
	case 0x82:
		kb_led_hotkey_down(data);
		break;
	case 0x83:
		kb_led_hotkey_up(data);
		break;
	case 0x84:
		kb_led_hotkey_color(data);
		break;
	}
}

// Add a System76 ACPI device
static int system76_add(struct acpi_device *acpi_dev)
{
	struct system76_data *data;
	int err;

	data = devm_kzalloc(&acpi_dev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	acpi_dev->driver_data = data;
	data->acpi_dev = acpi_dev;

	err = system76_get(data, "INIT");
	if (err)
		return err;
	data->ap_led.name = "system76_acpi::airplane";
	data->ap_led.flags = LED_CORE_SUSPENDRESUME;
	data->ap_led.brightness_get = ap_led_get;
	data->ap_led.brightness_set = ap_led_set;
	data->ap_led.max_brightness = 1;
	data->ap_led.default_trigger = "rfkill-none";
	err = devm_led_classdev_register(&acpi_dev->dev, &data->ap_led);
	if (err)
		return err;

	data->kb_led.name = "system76_acpi::kbd_backlight";
	data->kb_led.flags = LED_BRIGHT_HW_CHANGED | LED_CORE_SUSPENDRESUME;
	data->kb_led.brightness_get = kb_led_get;
	data->kb_led.brightness_set = kb_led_set;
	if (acpi_has_method(acpi_device_handle(data->acpi_dev), "SKBC")) {
		data->kb_led.max_brightness = 255;
		data->kb_toggle_brightness = 72;
		data->kb_color = 0xffffff;
		system76_set(data, "SKBC", data->kb_color);
	} else {
		data->kb_led.max_brightness = 5;
		data->kb_color = -1;
	}
	err = devm_led_classdev_register(&acpi_dev->dev, &data->kb_led);
	if (err)
		return err;

	if (data->kb_color >= 0) {
		err = device_create_file(
			data->kb_led.dev,
			&kb_led_color_dev_attr
		);
		if (err)
			return err;
	}

	return 0;
}

// Remove a System76 ACPI device
static int system76_remove(struct acpi_device *acpi_dev)
{
	struct system76_data *data;

	data = acpi_driver_data(acpi_dev);
	if (data->kb_color >= 0)
		device_remove_file(data->kb_led.dev, &kb_led_color_dev_attr);

	devm_led_classdev_unregister(&acpi_dev->dev, &data->ap_led);

	devm_led_classdev_unregister(&acpi_dev->dev, &data->kb_led);

	system76_get(data, "FINI");

	return 0;
}

static struct acpi_driver system76_driver = {
	.name = "System76 ACPI Driver",
	.class = "hotkey",
	.ids = device_ids,
	.ops = {
		.add = system76_add,
		.remove = system76_remove,
		.notify = system76_notify,
	},
};
module_acpi_driver(system76_driver);

MODULE_DESCRIPTION("System76 ACPI Driver");
MODULE_AUTHOR("Jeremy Soller <jeremy@system76.com>");
MODULE_LICENSE("GPL");

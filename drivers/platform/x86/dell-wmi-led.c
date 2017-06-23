/*
 * Copyright (C) 2010 Dell Inc.
 * Louis Davis <louis_davis@dell.com>
 * Jim Dailey <jim_dailey@dell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 */

#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/module.h>

MODULE_AUTHOR("Louis Davis/Jim Dailey");
MODULE_DESCRIPTION("Dell LED Control Driver");
MODULE_LICENSE("GPL");

#define DELL_LED_BIOS_GUID "F6E4FE6E-909D-47cb-8BAB-C9F6F2F8D396"
MODULE_ALIAS("wmi:" DELL_LED_BIOS_GUID);

/* Error Result Codes: */
#define INVALID_DEVICE_ID	250
#define INVALID_PARAMETER	251
#define INVALID_BUFFER		252
#define INTERFACE_ERROR		253
#define UNSUPPORTED_COMMAND	254
#define UNSPECIFIED_ERROR	255

/* Device ID */
#define DEVICE_ID_PANEL_BACK	1

/* LED Commands */
#define CMD_LED_ON	16
#define CMD_LED_OFF	17
#define CMD_LED_BLINK	18

struct bios_args {
	unsigned char length;
	unsigned char result_code;
	unsigned char device_id;
	unsigned char command;
	unsigned char on_time;
	unsigned char off_time;
};

static int dell_led_perform_fn(u8 length, u8 result_code, u8 device_id,
			       u8 command, u8 on_time, u8 off_time)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct bios_args *bios_return;
	struct acpi_buffer input;
	union acpi_object *obj;
	acpi_status status;
	u8 return_code;

	struct bios_args args = {
		.length = length,
		.result_code = result_code,
		.device_id = device_id,
		.command = command,
		.on_time = on_time,
		.off_time = off_time
	};

	input.length = sizeof(struct bios_args);
	input.pointer = &args;

	status = wmi_evaluate_method(DELL_LED_BIOS_GUID, 0, 1, &input, &output);
	if (ACPI_FAILURE(status))
		return status;

	obj = output.pointer;

	if (!obj)
		return -EINVAL;
	if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return -EINVAL;
	}

	bios_return = ((struct bios_args *)obj->buffer.pointer);
	return_code = bios_return->result_code;

	kfree(obj);

	return return_code;
}

static int led_on(void)
{
	return dell_led_perform_fn(3,	/* Length of command */
		INTERFACE_ERROR,	/* Init to  INTERFACE_ERROR */
		DEVICE_ID_PANEL_BACK,	/* Device ID */
		CMD_LED_ON,		/* Command */
		0,			/* not used */
		0);			/* not used */
}

static int led_off(void)
{
	return dell_led_perform_fn(3,	/* Length of command */
		INTERFACE_ERROR,	/* Init to  INTERFACE_ERROR */
		DEVICE_ID_PANEL_BACK,	/* Device ID */
		CMD_LED_OFF,		/* Command */
		0,			/* not used */
		0);			/* not used */
}

static int led_blink(unsigned char on_eighths, unsigned char off_eighths)
{
	return dell_led_perform_fn(5,	/* Length of command */
		INTERFACE_ERROR,	/* Init to  INTERFACE_ERROR */
		DEVICE_ID_PANEL_BACK,	/* Device ID */
		CMD_LED_BLINK,		/* Command */
		on_eighths,		/* blink on in eigths of a second */
		off_eighths);		/* blink off in eights of a second */
}

static void dell_led_set(struct led_classdev *led_cdev,
			 enum led_brightness value)
{
	if (value == LED_OFF)
		led_off();
	else
		led_on();
}

static int dell_led_blink(struct led_classdev *led_cdev,
			  unsigned long *delay_on, unsigned long *delay_off)
{
	unsigned long on_eighths;
	unsigned long off_eighths;

	/*
	 * The Dell LED delay is based on 125ms intervals.
	 * Need to round up to next interval.
	 */

	on_eighths = DIV_ROUND_UP(*delay_on, 125);
	on_eighths = clamp_t(unsigned long, on_eighths, 1, 255);
	*delay_on = on_eighths * 125;

	off_eighths = DIV_ROUND_UP(*delay_off, 125);
	off_eighths = clamp_t(unsigned long, off_eighths, 1, 255);
	*delay_off = off_eighths * 125;

	led_blink(on_eighths, off_eighths);

	return 0;
}

static struct led_classdev dell_led = {
	.name		= "dell::lid",
	.brightness	= LED_OFF,
	.max_brightness = 1,
	.brightness_set = dell_led_set,
	.blink_set	= dell_led_blink,
	.flags		= LED_CORE_SUSPENDRESUME,
};

static int __init dell_led_init(void)
{
	int error = 0;

	if (!wmi_has_guid(DELL_LED_BIOS_GUID))
		return -ENODEV;

	error = led_off();
	if (error != 0)
		return -ENODEV;

	return led_classdev_register(NULL, &dell_led);
}

static void __exit dell_led_exit(void)
{
	led_classdev_unregister(&dell_led);

	led_off();
}

module_init(dell_led_init);
module_exit(dell_led_exit);

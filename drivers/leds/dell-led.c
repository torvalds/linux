/*
 * dell_led.c - Dell LED Driver
 *
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
#include <linux/dmi.h>
#include <linux/dell-led.h>
#include "../platform/x86/dell-smbios.h"

MODULE_AUTHOR("Louis Davis/Jim Dailey");
MODULE_DESCRIPTION("Dell LED Control Driver");
MODULE_LICENSE("GPL");

#define DELL_LED_BIOS_GUID "F6E4FE6E-909D-47cb-8BAB-C9F6F2F8D396"
#define DELL_APP_GUID "A80593CE-A997-11DA-B012-B622A1EF5492"
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

#define GLOBAL_MIC_MUTE_ENABLE	0x364
#define GLOBAL_MIC_MUTE_DISABLE	0x365

static int dell_micmute_led_set(int state)
{
	struct calling_interface_buffer *buffer;
	struct calling_interface_token *token;

	if (!wmi_has_guid(DELL_APP_GUID))
		return -ENODEV;

	if (state == 0)
		token = dell_smbios_find_token(GLOBAL_MIC_MUTE_DISABLE);
	else if (state == 1)
		token = dell_smbios_find_token(GLOBAL_MIC_MUTE_ENABLE);
	else
		return -EINVAL;

	if (!token)
		return -ENODEV;

	buffer = dell_smbios_get_buffer();
	buffer->input[0] = token->location;
	buffer->input[1] = token->value;
	dell_smbios_send_request(1, 0);
	dell_smbios_release_buffer();

	return state;
}

int dell_app_wmi_led_set(int whichled, int on)
{
	int state = 0;

	switch (whichled) {
	case DELL_LED_MICMUTE:
		state = dell_micmute_led_set(on);
		break;
	default:
		pr_warn("led type %x is not supported\n", whichled);
		break;
	}

	return state;
}
EXPORT_SYMBOL_GPL(dell_app_wmi_led_set);

struct bios_args {
	unsigned char length;
	unsigned char result_code;
	unsigned char device_id;
	unsigned char command;
	unsigned char on_time;
	unsigned char off_time;
};

static int dell_led_perform_fn(u8 length,
		u8 result_code,
		u8 device_id,
		u8 command,
		u8 on_time,
		u8 off_time)
{
	struct bios_args *bios_return;
	u8 return_code;
	union acpi_object *obj;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer input;
	acpi_status status;

	struct bios_args args;
	args.length = length;
	args.result_code = result_code;
	args.device_id = device_id;
	args.command = command;
	args.on_time = on_time;
	args.off_time = off_time;

	input.length = sizeof(struct bios_args);
	input.pointer = &args;

	status = wmi_evaluate_method(DELL_LED_BIOS_GUID,
		1,
		1,
		&input,
		&output);

	if (ACPI_FAILURE(status))
		return status;

	obj = output.pointer;

	if (!obj)
		return -EINVAL;
	else if (obj->type != ACPI_TYPE_BUFFER) {
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

static int led_blink(unsigned char on_eighths,
		unsigned char off_eighths)
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
		unsigned long *delay_on,
		unsigned long *delay_off)
{
	unsigned long on_eighths;
	unsigned long off_eighths;

	/* The Dell LED delay is based on 125ms intervals.
	   Need to round up to next interval. */

	on_eighths = (*delay_on + 124) / 125;
	if (0 == on_eighths)
		on_eighths = 1;
	if (on_eighths > 255)
		on_eighths = 255;
	*delay_on = on_eighths * 125;

	off_eighths = (*delay_off + 124) / 125;
	if (0 == off_eighths)
		off_eighths = 1;
	if (off_eighths > 255)
		off_eighths = 255;
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

	if (!wmi_has_guid(DELL_LED_BIOS_GUID) && !wmi_has_guid(DELL_APP_GUID))
		return -ENODEV;

	if (wmi_has_guid(DELL_LED_BIOS_GUID)) {
		error = led_off();
		if (error != 0)
			return -ENODEV;

		error = led_classdev_register(NULL, &dell_led);
	}

	return error;
}

static void __exit dell_led_exit(void)
{
	int error = 0;

	if (wmi_has_guid(DELL_LED_BIOS_GUID)) {
		error = led_off();
		if (error == 0)
			led_classdev_unregister(&dell_led);
	}
}

module_init(dell_led_init);
module_exit(dell_led_exit);

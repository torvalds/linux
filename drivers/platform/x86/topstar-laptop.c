/*
 * ACPI driver for Topstar notebooks (hotkeys support only)
 *
 * Copyright (c) 2009 Herton Ronaldo Krzesinski <herton@mandriva.com.br>
 *
 * Implementation inspired by existing x86 platform drivers, in special
 * asus/eepc/fujitsu-laptop, thanks to their authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/input.h>

#define ACPI_TOPSTAR_CLASS "topstar"

struct topstar_hkey {
	struct input_dev *inputdev;
};

struct tps_key_entry {
	u8 code;
	u16 keycode;
};

static struct tps_key_entry topstar_keymap[] = {
	{ 0x80, KEY_BRIGHTNESSUP },
	{ 0x81, KEY_BRIGHTNESSDOWN },
	{ 0x83, KEY_VOLUMEUP },
	{ 0x84, KEY_VOLUMEDOWN },
	{ 0x85, KEY_MUTE },
	{ 0x86, KEY_SWITCHVIDEOMODE },
	{ 0x87, KEY_F13 }, /* touchpad enable/disable key */
	{ 0x88, KEY_WLAN },
	{ 0x8a, KEY_WWW },
	{ 0x8b, KEY_MAIL },
	{ 0x8c, KEY_MEDIA },
	{ 0x96, KEY_F14 }, /* G key? */
	{ }
};

static struct tps_key_entry *tps_get_key_by_scancode(unsigned int code)
{
	struct tps_key_entry *key;

	for (key = topstar_keymap; key->code; key++)
		if (code == key->code)
			return key;

	return NULL;
}

static struct tps_key_entry *tps_get_key_by_keycode(unsigned int code)
{
	struct tps_key_entry *key;

	for (key = topstar_keymap; key->code; key++)
		if (code == key->keycode)
			return key;

	return NULL;
}

static void acpi_topstar_notify(struct acpi_device *device, u32 event)
{
	struct tps_key_entry *key;
	static bool dup_evnt[2];
	bool *dup;
	struct topstar_hkey *hkey = acpi_driver_data(device);

	/* 0x83 and 0x84 key events comes duplicated... */
	if (event == 0x83 || event == 0x84) {
		dup = &dup_evnt[event - 0x83];
		if (*dup) {
			*dup = false;
			return;
		}
		*dup = true;
	}

	/*
	 * 'G key' generate two event codes, convert to only
	 * one event/key code for now (3G switch?)
	 */
	if (event == 0x97)
		event = 0x96;

	key = tps_get_key_by_scancode(event);
	if (key) {
		input_report_key(hkey->inputdev, key->keycode, 1);
		input_sync(hkey->inputdev);
		input_report_key(hkey->inputdev, key->keycode, 0);
		input_sync(hkey->inputdev);
		return;
	}

	/* Known non hotkey events don't handled or that we don't care yet */
	if (event == 0x8e || event == 0x8f || event == 0x90)
		return;

	pr_info("unknown event = 0x%02x\n", event);
}

static int acpi_topstar_fncx_switch(struct acpi_device *device, bool state)
{
	acpi_status status;
	union acpi_object fncx_params[1] = {
		{ .type = ACPI_TYPE_INTEGER }
	};
	struct acpi_object_list fncx_arg_list = { 1, &fncx_params[0] };

	fncx_params[0].integer.value = state ? 0x86 : 0x87;
	status = acpi_evaluate_object(device->handle, "FNCX", &fncx_arg_list, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to switch FNCX notifications\n");
		return -ENODEV;
	}

	return 0;
}

static int topstar_getkeycode(struct input_dev *dev,
				unsigned int scancode, unsigned int *keycode)
{
	struct tps_key_entry *key = tps_get_key_by_scancode(scancode);

	if (!key)
		return -EINVAL;

	*keycode = key->keycode;
	return 0;
}

static int topstar_setkeycode(struct input_dev *dev,
				unsigned int scancode, unsigned int keycode)
{
	struct tps_key_entry *key;
	int old_keycode;

	key = tps_get_key_by_scancode(scancode);

	if (!key)
		return -EINVAL;

	old_keycode = key->keycode;
	key->keycode = keycode;
	set_bit(keycode, dev->keybit);
	if (!tps_get_key_by_keycode(old_keycode))
		clear_bit(old_keycode, dev->keybit);
	return 0;
}

static int acpi_topstar_init_hkey(struct topstar_hkey *hkey)
{
	struct tps_key_entry *key;

	hkey->inputdev = input_allocate_device();
	if (!hkey->inputdev) {
		pr_err("Unable to allocate input device\n");
		return -ENODEV;
	}
	hkey->inputdev->name = "Topstar Laptop extra buttons";
	hkey->inputdev->phys = "topstar/input0";
	hkey->inputdev->id.bustype = BUS_HOST;
	hkey->inputdev->getkeycode = topstar_getkeycode;
	hkey->inputdev->setkeycode = topstar_setkeycode;
	for (key = topstar_keymap; key->code; key++) {
		set_bit(EV_KEY, hkey->inputdev->evbit);
		set_bit(key->keycode, hkey->inputdev->keybit);
	}
	if (input_register_device(hkey->inputdev)) {
		pr_err("Unable to register input device\n");
		input_free_device(hkey->inputdev);
		return -ENODEV;
	}

	return 0;
}

static int acpi_topstar_add(struct acpi_device *device)
{
	struct topstar_hkey *tps_hkey;

	tps_hkey = kzalloc(sizeof(struct topstar_hkey), GFP_KERNEL);
	if (!tps_hkey)
		return -ENOMEM;

	strcpy(acpi_device_name(device), "Topstar TPSACPI");
	strcpy(acpi_device_class(device), ACPI_TOPSTAR_CLASS);

	if (acpi_topstar_fncx_switch(device, true))
		goto add_err;

	if (acpi_topstar_init_hkey(tps_hkey))
		goto add_err;

	device->driver_data = tps_hkey;
	return 0;

add_err:
	kfree(tps_hkey);
	return -ENODEV;
}

static int acpi_topstar_remove(struct acpi_device *device, int type)
{
	struct topstar_hkey *tps_hkey = acpi_driver_data(device);

	acpi_topstar_fncx_switch(device, false);

	input_unregister_device(tps_hkey->inputdev);
	kfree(tps_hkey);

	return 0;
}

static const struct acpi_device_id topstar_device_ids[] = {
	{ "TPSACPI01", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, topstar_device_ids);

static struct acpi_driver acpi_topstar_driver = {
	.name = "Topstar laptop ACPI driver",
	.class = ACPI_TOPSTAR_CLASS,
	.ids = topstar_device_ids,
	.ops = {
		.add = acpi_topstar_add,
		.remove = acpi_topstar_remove,
		.notify = acpi_topstar_notify,
	},
};

static int __init topstar_laptop_init(void)
{
	int ret;

	ret = acpi_bus_register_driver(&acpi_topstar_driver);
	if (ret < 0)
		return ret;

	printk(KERN_INFO "Topstar Laptop ACPI extras driver loaded\n");

	return 0;
}

static void __exit topstar_laptop_exit(void)
{
	acpi_bus_unregister_driver(&acpi_topstar_driver);
}

module_init(topstar_laptop_init);
module_exit(topstar_laptop_exit);

MODULE_AUTHOR("Herton Ronaldo Krzesinski");
MODULE_DESCRIPTION("Topstar Laptop ACPI Extras driver");
MODULE_LICENSE("GPL");

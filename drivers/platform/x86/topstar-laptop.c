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
#include <linux/input/sparse-keymap.h>

#define ACPI_TOPSTAR_CLASS "topstar"

struct topstar_hkey {
	struct input_dev *inputdev;
};

static const struct key_entry topstar_keymap[] = {
	{ KE_KEY, 0x80, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x81, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0x83, { KEY_VOLUMEUP } },
	{ KE_KEY, 0x84, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x85, { KEY_MUTE } },
	{ KE_KEY, 0x86, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x87, { KEY_F13 } }, /* touchpad enable/disable key */
	{ KE_KEY, 0x88, { KEY_WLAN } },
	{ KE_KEY, 0x8a, { KEY_WWW } },
	{ KE_KEY, 0x8b, { KEY_MAIL } },
	{ KE_KEY, 0x8c, { KEY_MEDIA } },

	/* Known non hotkey events don't handled or that we don't care yet */
	{ KE_IGNORE, 0x82, }, /* backlight event */
	{ KE_IGNORE, 0x8e, },
	{ KE_IGNORE, 0x8f, },
	{ KE_IGNORE, 0x90, },

	/*
	 * 'G key' generate two event codes, convert to only
	 * one event/key code for now, consider replacing by
	 * a switch (3G switch - SW_3G?)
	 */
	{ KE_KEY, 0x96, { KEY_F14 } },
	{ KE_KEY, 0x97, { KEY_F14 } },

	{ KE_END, 0 }
};

static void acpi_topstar_notify(struct acpi_device *device, u32 event)
{
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

	if (!sparse_keymap_report_event(hkey->inputdev, event, 1, true))
		pr_info("unknown event = 0x%02x\n", event);
}

static int acpi_topstar_fncx_switch(struct acpi_device *device, bool state)
{
	acpi_status status;

	status = acpi_execute_simple_method(device->handle, "FNCX",
						state ? 0x86 : 0x87);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to switch FNCX notifications\n");
		return -ENODEV;
	}

	return 0;
}

static int acpi_topstar_init_hkey(struct topstar_hkey *hkey)
{
	struct input_dev *input;
	int error;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->name = "Topstar Laptop extra buttons";
	input->phys = "topstar/input0";
	input->id.bustype = BUS_HOST;

	error = sparse_keymap_setup(input, topstar_keymap, NULL);
	if (error) {
		pr_err("Unable to setup input device keymap\n");
		goto err_free_dev;
	}

	error = input_register_device(input);
	if (error) {
		pr_err("Unable to register input device\n");
		goto err_free_dev;
	}

	hkey->inputdev = input;
	return 0;

 err_free_dev:
	input_free_device(input);
	return error;
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

static int acpi_topstar_remove(struct acpi_device *device)
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
module_acpi_driver(acpi_topstar_driver);

MODULE_AUTHOR("Herton Ronaldo Krzesinski");
MODULE_DESCRIPTION("Topstar Laptop ACPI Extras driver");
MODULE_LICENSE("GPL");

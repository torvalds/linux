/*
 * Topstar Laptop ACPI Extras driver
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
#include <linux/platform_device.h>

#define TOPSTAR_LAPTOP_CLASS "topstar"

struct topstar_laptop {
	struct acpi_device *device;
	struct platform_device *platform;
	struct input_dev *input;
};

/*
 * Input
 */

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

static void topstar_input_notify(struct topstar_laptop *topstar, int event)
{
	if (!sparse_keymap_report_event(topstar->input, event, 1, true))
		pr_info("unknown event = 0x%02x\n", event);
}

static int topstar_input_init(struct topstar_laptop *topstar)
{
	struct input_dev *input;
	int err;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->name = "Topstar Laptop extra buttons";
	input->phys = TOPSTAR_LAPTOP_CLASS "/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &topstar->platform->dev;

	err = sparse_keymap_setup(input, topstar_keymap, NULL);
	if (err) {
		pr_err("Unable to setup input device keymap\n");
		goto err_free_dev;
	}

	err = input_register_device(input);
	if (err) {
		pr_err("Unable to register input device\n");
		goto err_free_dev;
	}

	topstar->input = input;
	return 0;

err_free_dev:
	input_free_device(input);
	return err;
}

static void topstar_input_exit(struct topstar_laptop *topstar)
{
	input_unregister_device(topstar->input);
}

/*
 * Platform
 */

static struct platform_driver topstar_platform_driver = {
	.driver = {
		.name = TOPSTAR_LAPTOP_CLASS,
	},
};

static int topstar_platform_init(struct topstar_laptop *topstar)
{
	int err;

	topstar->platform = platform_device_alloc(TOPSTAR_LAPTOP_CLASS, -1);
	if (!topstar->platform)
		return -ENOMEM;

	platform_set_drvdata(topstar->platform, topstar);

	err = platform_device_add(topstar->platform);
	if (err)
		goto err_device_put;

	return 0;

err_device_put:
	platform_device_put(topstar->platform);
	return err;
}

static void topstar_platform_exit(struct topstar_laptop *topstar)
{
	platform_device_unregister(topstar->platform);
}

/*
 * ACPI
 */

static int topstar_acpi_fncx_switch(struct acpi_device *device, bool state)
{
	acpi_status status;
	u64 arg = state ? 0x86 : 0x87;

	status = acpi_execute_simple_method(device->handle, "FNCX", arg);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to switch FNCX notifications\n");
		return -ENODEV;
	}

	return 0;
}

static void topstar_acpi_notify(struct acpi_device *device, u32 event)
{
	struct topstar_laptop *topstar = acpi_driver_data(device);
	static bool dup_evnt[2];
	bool *dup;

	/* 0x83 and 0x84 key events comes duplicated... */
	if (event == 0x83 || event == 0x84) {
		dup = &dup_evnt[event - 0x83];
		if (*dup) {
			*dup = false;
			return;
		}
		*dup = true;
	}

	topstar_input_notify(topstar, event);
}

static int topstar_acpi_init(struct topstar_laptop *topstar)
{
	return topstar_acpi_fncx_switch(topstar->device, true);
}

static void topstar_acpi_exit(struct topstar_laptop *topstar)
{
	topstar_acpi_fncx_switch(topstar->device, false);
}

static int topstar_acpi_add(struct acpi_device *device)
{
	struct topstar_laptop *topstar;
	int err;

	topstar = kzalloc(sizeof(struct topstar_laptop), GFP_KERNEL);
	if (!topstar)
		return -ENOMEM;

	strcpy(acpi_device_name(device), "Topstar TPSACPI");
	strcpy(acpi_device_class(device), TOPSTAR_LAPTOP_CLASS);
	device->driver_data = topstar;
	topstar->device = device;

	err = topstar_acpi_init(topstar);
	if (err)
		goto err_free;

	err = topstar_platform_init(topstar);
	if (err)
		goto err_acpi_exit;

	err = topstar_input_init(topstar);
	if (err)
		goto err_platform_exit;

	return 0;

err_platform_exit:
	topstar_platform_exit(topstar);
err_acpi_exit:
	topstar_acpi_exit(topstar);
err_free:
	kfree(topstar);
	return err;
}

static int topstar_acpi_remove(struct acpi_device *device)
{
	struct topstar_laptop *topstar = acpi_driver_data(device);

	topstar_input_exit(topstar);
	topstar_platform_exit(topstar);
	topstar_acpi_exit(topstar);

	kfree(topstar);
	return 0;
}

static const struct acpi_device_id topstar_device_ids[] = {
	{ "TPS0001", 0 },
	{ "TPSACPI01", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, topstar_device_ids);

static struct acpi_driver topstar_acpi_driver = {
	.name = "Topstar laptop ACPI driver",
	.class = TOPSTAR_LAPTOP_CLASS,
	.ids = topstar_device_ids,
	.ops = {
		.add = topstar_acpi_add,
		.remove = topstar_acpi_remove,
		.notify = topstar_acpi_notify,
	},
};

static int __init topstar_laptop_init(void)
{
	int ret;

	ret = platform_driver_register(&topstar_platform_driver);
	if (ret < 0)
		return ret;

	ret = acpi_bus_register_driver(&topstar_acpi_driver);
	if (ret < 0)
		goto err_driver_unreg;

	pr_info("ACPI extras driver loaded\n");
	return 0;

err_driver_unreg:
	platform_driver_unregister(&topstar_platform_driver);
	return ret;
}

static void __exit topstar_laptop_exit(void)
{
	acpi_bus_unregister_driver(&topstar_acpi_driver);
	platform_driver_unregister(&topstar_platform_driver);
}

module_init(topstar_laptop_init);
module_exit(topstar_laptop_exit);

MODULE_AUTHOR("Herton Ronaldo Krzesinski");
MODULE_DESCRIPTION("Topstar Laptop ACPI Extras driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Airplane mode button for HP & Xiaomi laptops
 *
 *  Copyright (C) 2014-2017 Alex Hung <alex.hung@canonical.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Hung");
MODULE_ALIAS("acpi*:HPQ6001:*");
MODULE_ALIAS("acpi*:WSTADEF:*");
MODULE_ALIAS("acpi*:AMDI0051:*");

static struct input_dev *hpwl_input_dev;

static const struct acpi_device_id hpwl_ids[] = {
	{"HPQ6001", 0},
	{"WSTADEF", 0},
	{"AMDI0051", 0},
	{"", 0},
};

static int hp_wireless_input_setup(void)
{
	int err;

	hpwl_input_dev = input_allocate_device();
	if (!hpwl_input_dev)
		return -ENOMEM;

	hpwl_input_dev->name = "HP Wireless hotkeys";
	hpwl_input_dev->phys = "hpq6001/input0";
	hpwl_input_dev->id.bustype = BUS_HOST;
	hpwl_input_dev->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_RFKILL, hpwl_input_dev->keybit);

	err = input_register_device(hpwl_input_dev);
	if (err)
		goto err_free_dev;

	return 0;

err_free_dev:
	input_free_device(hpwl_input_dev);
	return err;
}

static void hp_wireless_input_destroy(void)
{
	input_unregister_device(hpwl_input_dev);
}

static void hpwl_notify(struct acpi_device *acpi_dev, u32 event)
{
	if (event != 0x80) {
		pr_info("Received unknown event (0x%x)\n", event);
		return;
	}

	input_report_key(hpwl_input_dev, KEY_RFKILL, 1);
	input_sync(hpwl_input_dev);
	input_report_key(hpwl_input_dev, KEY_RFKILL, 0);
	input_sync(hpwl_input_dev);
}

static int hpwl_add(struct acpi_device *device)
{
	int err;

	err = hp_wireless_input_setup();
	if (err)
		pr_err("Failed to setup hp wireless hotkeys\n");

	return err;
}

static int hpwl_remove(struct acpi_device *device)
{
	hp_wireless_input_destroy();
	return 0;
}

static struct acpi_driver hpwl_driver = {
	.name	= "hp-wireless",
	.owner	= THIS_MODULE,
	.ids	= hpwl_ids,
	.ops	= {
		.add	= hpwl_add,
		.remove	= hpwl_remove,
		.notify	= hpwl_notify,
	},
};

module_acpi_driver(hpwl_driver);

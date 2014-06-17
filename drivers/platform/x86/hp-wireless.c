/*
 *  hp-wireless button for Windows 8
 *
 *  Copyright (C) 2014 Alex Hung <alex.hung@canonical.com>
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
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

static struct input_dev *hpwl_input_dev;

static const struct acpi_device_id hpwl_ids[] = {
	{"HPQ6001", 0},
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

static int __init hpwl_init(void)
{
	int err;

	pr_info("Initializing HPQ6001 module\n");
	err = acpi_bus_register_driver(&hpwl_driver);
	if (err) {
		pr_err("Unable to register HP wireless control driver.\n");
		goto error_acpi_register;
	}

	return 0;

error_acpi_register:
	return err;
}

static void __exit hpwl_exit(void)
{
	pr_info("Exiting HPQ6001 module\n");
	acpi_bus_unregister_driver(&hpwl_driver);
}

module_init(hpwl_init);
module_exit(hpwl_exit);

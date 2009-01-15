/*
 *  leds-hp-disk.c - driver for HP "hard disk protection" LED
 *
 *  Copyright (C) 2008 Pavel Machek
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/leds.h>
#include <acpi/acpi_drivers.h>

#define DRIVER_NAME     "leds-hp-disk"
#define ACPI_MDPS_CLASS "led"

/* For automatic insertion of the module */
static struct acpi_device_id hpled_device_ids[] = {
	{"HPQ0004", 0}, /* HP Mobile Data Protection System PNP */
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, hpled_device_ids);

struct acpi_hpled {
	struct acpi_device	*device;   /* The ACPI device */
};

static struct acpi_hpled adev;

static acpi_status hpled_acpi_write(acpi_handle handle, int reg)
{
	unsigned long long ret; /* Not used when writing */
	union acpi_object in_obj[1];
	struct acpi_object_list args = { 1, in_obj };

	in_obj[0].type          = ACPI_TYPE_INTEGER;
	in_obj[0].integer.value = reg;

	return acpi_evaluate_integer(handle, "ALED", &args, &ret);
}

static void hpled_set(struct led_classdev *led_cdev,
			       enum led_brightness value)
{
	hpled_acpi_write(adev.device->handle, !!value);
}

static struct led_classdev hpled_led = {
	.name			= "hp:red:hddprotection",
	.default_trigger	= "heartbeat",
	.brightness_set		= hpled_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static int hpled_add(struct acpi_device *device)
{
	int ret;

	if (!device)
		return -EINVAL;

	adev.device = device;
	strcpy(acpi_device_name(device), DRIVER_NAME);
	strcpy(acpi_device_class(device), ACPI_MDPS_CLASS);
	device->driver_data = &adev;

	ret = led_classdev_register(NULL, &hpled_led);
	return ret;
}

static int hpled_remove(struct acpi_device *device, int type)
{
	if (!device)
		return -EINVAL;

	led_classdev_unregister(&hpled_led);
	return 0;
}



static struct acpi_driver leds_hp_driver = {
	.name  = DRIVER_NAME,
	.class = ACPI_MDPS_CLASS,
	.ids   = hpled_device_ids,
	.ops = {
		.add     = hpled_add,
		.remove  = hpled_remove,
	}
};

static int __init hpled_init_module(void)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	ret = acpi_bus_register_driver(&leds_hp_driver);
	if (ret < 0)
		return ret;

	printk(KERN_INFO DRIVER_NAME " driver loaded.\n");

	return 0;
}

static void __exit hpled_exit_module(void)
{
	acpi_bus_unregister_driver(&leds_hp_driver);
}

MODULE_DESCRIPTION("Driver for HP disk protection LED");
MODULE_AUTHOR("Pavel Machek <pavel@suse.cz>");
MODULE_LICENSE("GPL");

module_init(hpled_init_module);
module_exit(hpled_exit_module);

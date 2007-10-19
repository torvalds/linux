/*
 *  atlas_btns.c - Atlas Wallmount Touchscreen ACPI Extras
 *
 *  Copyright (C) 2006 Jaya Kumar
 *  Based on Toshiba ACPI by John Belmonte and ASUS ACPI
 *  This work was sponsored by CIS(M) Sdn Bhd.
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
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <acpi/acpi_drivers.h>

#define ACPI_ATLAS_NAME			"Atlas ACPI"
#define ACPI_ATLAS_CLASS		"Atlas"

static struct input_dev *input_dev;

/* button handling code */
static acpi_status acpi_atlas_button_setup(acpi_handle region_handle,
		    u32 function, void *handler_context, void **return_context)
{
	*return_context =
		(function != ACPI_REGION_DEACTIVATE) ? handler_context : NULL;

	return AE_OK;
}

static acpi_status acpi_atlas_button_handler(u32 function,
		      acpi_physical_address address,
		      u32 bit_width, acpi_integer *value,
		      void *handler_context, void *region_context)
{
	acpi_status status;
	int keycode;

	if (function == ACPI_WRITE) {
		keycode = KEY_F1 + (address & 0x0F);
		input_report_key(input_dev, keycode, !(address & 0x10));
		input_sync(input_dev);
		status = 0;
	} else {
		printk(KERN_WARNING "atlas: shrugged on unexpected function"
			":function=%x,address=%lx,value=%x\n",
			function, (unsigned long)address, (u32)*value);
		status = -EINVAL;
	}

	return status;
}

static int atlas_acpi_button_add(struct acpi_device *device)
{
	acpi_status status;
	int err;

	input_dev = input_allocate_device();
	if (!input_dev) {
		printk(KERN_ERR "atlas: unable to allocate input device\n");
		return -ENOMEM;
	}

	input_dev->name = "Atlas ACPI button driver";
	input_dev->phys = "ASIM0000/atlas/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->evbit[BIT_WORD(EV_KEY)] = BIT_MASK(EV_KEY);

	set_bit(KEY_F1, input_dev->keybit);
	set_bit(KEY_F2, input_dev->keybit);
	set_bit(KEY_F3, input_dev->keybit);
	set_bit(KEY_F4, input_dev->keybit);
	set_bit(KEY_F5, input_dev->keybit);
	set_bit(KEY_F6, input_dev->keybit);
	set_bit(KEY_F7, input_dev->keybit);
	set_bit(KEY_F8, input_dev->keybit);
	set_bit(KEY_F9, input_dev->keybit);

	err = input_register_device(input_dev);
	if (err) {
		printk(KERN_ERR "atlas: couldn't register input device\n");
		input_free_device(input_dev);
		return err;
	}

	/* hookup button handler */
	status = acpi_install_address_space_handler(device->handle,
				0x81, &acpi_atlas_button_handler,
				&acpi_atlas_button_setup, device);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR "Atlas: Error installing addr spc handler\n");
		input_unregister_device(input_dev);
		status = -EINVAL;
	}

	return status;
}

static int atlas_acpi_button_remove(struct acpi_device *device, int type)
{
	acpi_status status;

	status = acpi_remove_address_space_handler(device->handle,
				0x81, &acpi_atlas_button_handler);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR "Atlas: Error removing addr spc handler\n");
		status = -EINVAL;
	}

	input_unregister_device(input_dev);

	return status;
}

static const struct acpi_device_id atlas_device_ids[] = {
	{"ASIM0000", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, atlas_device_ids);

static struct acpi_driver atlas_acpi_driver = {
	.name	= ACPI_ATLAS_NAME,
	.class	= ACPI_ATLAS_CLASS,
	.ids	= atlas_device_ids,
	.ops	= {
		.add	= atlas_acpi_button_add,
		.remove	= atlas_acpi_button_remove,
	},
};

static int __init atlas_acpi_init(void)
{
	int result;

	if (acpi_disabled)
		return -ENODEV;

	result = acpi_bus_register_driver(&atlas_acpi_driver);
	if (result < 0) {
		printk(KERN_ERR "Atlas ACPI: Unable to register driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit atlas_acpi_exit(void)
{
	acpi_bus_unregister_driver(&atlas_acpi_driver);
}

module_init(atlas_acpi_init);
module_exit(atlas_acpi_exit);

MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atlas button driver");


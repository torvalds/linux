/*
 *  acpi_fan.c - ACPI Fan Driver ($Revision: 29 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_FAN_COMPONENT		0x00200000
#define ACPI_FAN_CLASS			"fan"
#define ACPI_FAN_DRIVER_NAME		"ACPI Fan Driver"
#define ACPI_FAN_FILE_STATE		"state"

#define _COMPONENT		ACPI_FAN_COMPONENT
ACPI_MODULE_NAME("acpi_fan")

    MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_FAN_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int acpi_fan_add(struct acpi_device *device);
static int acpi_fan_remove(struct acpi_device *device, int type);

static struct acpi_driver acpi_fan_driver = {
	.name = ACPI_FAN_DRIVER_NAME,
	.class = ACPI_FAN_CLASS,
	.ids = "PNP0C0B",
	.ops = {
		.add = acpi_fan_add,
		.remove = acpi_fan_remove,
		},
};

struct acpi_fan {
	acpi_handle handle;
};

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_fan_dir;

static int acpi_fan_read_state(struct seq_file *seq, void *offset)
{
	struct acpi_fan *fan = seq->private;
	int state = 0;

	ACPI_FUNCTION_TRACE("acpi_fan_read_state");

	if (fan) {
		if (acpi_bus_get_power(fan->handle, &state))
			seq_printf(seq, "status:                  ERROR\n");
		else
			seq_printf(seq, "status:                  %s\n",
				   !state ? "on" : "off");
	}
	return_VALUE(0);
}

static int acpi_fan_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_fan_read_state, PDE(inode)->data);
}

static ssize_t
acpi_fan_write_state(struct file *file, const char __user * buffer,
		     size_t count, loff_t * ppos)
{
	int result = 0;
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_fan *fan = (struct acpi_fan *)m->private;
	char state_string[12] = { '\0' };

	ACPI_FUNCTION_TRACE("acpi_fan_write_state");

	if (!fan || (count > sizeof(state_string) - 1))
		return_VALUE(-EINVAL);

	if (copy_from_user(state_string, buffer, count))
		return_VALUE(-EFAULT);

	state_string[count] = '\0';

	result = acpi_bus_set_power(fan->handle,
				    simple_strtoul(state_string, NULL, 0));
	if (result)
		return_VALUE(result);

	return_VALUE(count);
}

static struct file_operations acpi_fan_state_ops = {
	.open = acpi_fan_state_open_fs,
	.read = seq_read,
	.write = acpi_fan_write_state,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int acpi_fan_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_fan_add_fs");

	if (!device)
		return_VALUE(-EINVAL);

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_fan_dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
		acpi_device_dir(device)->owner = THIS_MODULE;
	}

	/* 'status' [R/W] */
	entry = create_proc_entry(ACPI_FAN_FILE_STATE,
				  S_IFREG | S_IRUGO | S_IWUSR,
				  acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create '%s' fs entry\n",
				  ACPI_FAN_FILE_STATE));
	else {
		entry->proc_fops = &acpi_fan_state_ops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	return_VALUE(0);
}

static int acpi_fan_remove_fs(struct acpi_device *device)
{
	ACPI_FUNCTION_TRACE("acpi_fan_remove_fs");

	if (acpi_device_dir(device)) {
		remove_proc_entry(ACPI_FAN_FILE_STATE, acpi_device_dir(device));
		remove_proc_entry(acpi_device_bid(device), acpi_fan_dir);
		acpi_device_dir(device) = NULL;
	}

	return_VALUE(0);
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static int acpi_fan_add(struct acpi_device *device)
{
	int result = 0;
	struct acpi_fan *fan = NULL;
	int state = 0;

	ACPI_FUNCTION_TRACE("acpi_fan_add");

	if (!device)
		return_VALUE(-EINVAL);

	fan = kmalloc(sizeof(struct acpi_fan), GFP_KERNEL);
	if (!fan)
		return_VALUE(-ENOMEM);
	memset(fan, 0, sizeof(struct acpi_fan));

	fan->handle = device->handle;
	strcpy(acpi_device_name(device), "Fan");
	strcpy(acpi_device_class(device), ACPI_FAN_CLASS);
	acpi_driver_data(device) = fan;

	result = acpi_bus_get_power(fan->handle, &state);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error reading power state\n"));
		goto end;
	}

	result = acpi_fan_add_fs(device);
	if (result)
		goto end;

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       !device->power.state ? "on" : "off");

      end:
	if (result)
		kfree(fan);

	return_VALUE(result);
}

static int acpi_fan_remove(struct acpi_device *device, int type)
{
	struct acpi_fan *fan = NULL;

	ACPI_FUNCTION_TRACE("acpi_fan_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	fan = (struct acpi_fan *)acpi_driver_data(device);

	acpi_fan_remove_fs(device);

	kfree(fan);

	return_VALUE(0);
}

static int __init acpi_fan_init(void)
{
	int result = 0;

	ACPI_FUNCTION_TRACE("acpi_fan_init");

	acpi_fan_dir = proc_mkdir(ACPI_FAN_CLASS, acpi_root_dir);
	if (!acpi_fan_dir)
		return_VALUE(-ENODEV);
	acpi_fan_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&acpi_fan_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_FAN_CLASS, acpi_root_dir);
		return_VALUE(-ENODEV);
	}

	return_VALUE(0);
}

static void __exit acpi_fan_exit(void)
{
	ACPI_FUNCTION_TRACE("acpi_fan_exit");

	acpi_bus_unregister_driver(&acpi_fan_driver);

	remove_proc_entry(ACPI_FAN_CLASS, acpi_root_dir);

	return_VOID;
}

module_init(acpi_fan_init);
module_exit(acpi_fan_exit);

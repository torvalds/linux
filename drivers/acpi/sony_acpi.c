/*
 * ACPI Sony Notebook Control Driver (SNC)
 *
 * Copyright (C) 2004-2005 Stelian Pop <stelian@popies.net>
 *
 * Parts of this driver inspired from asus_acpi.c and ibm_acpi.c
 * which are copyrighted by their respective authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <asm/uaccess.h>

#define ACPI_SNC_CLASS		"sony"
#define ACPI_SNC_HID		"SNY5001"
#define ACPI_SNC_DRIVER_NAME	"ACPI Sony Notebook Control Driver v0.3"

/* the device uses 1-based values, while the backlight subsystem uses
   0-based values */
#define SONY_MAX_BRIGHTNESS	8

#define LOG_PFX			KERN_WARNING "sony_acpi: "

MODULE_AUTHOR("Stelian Pop");
MODULE_DESCRIPTION(ACPI_SNC_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "set this to 1 (and RTFM) if you want to help "
			"the development of this driver");

static acpi_handle sony_acpi_handle;
static struct proc_dir_entry *sony_acpi_dir;

static int sony_backlight_update_status(struct backlight_device *bd);
static int sony_backlight_get_brightness(struct backlight_device *bd);
static struct backlight_device *sony_backlight_device;
static struct backlight_properties sony_backlight_properties = {
	.owner		= THIS_MODULE,
	.update_status	= sony_backlight_update_status,
	.get_brightness	= sony_backlight_get_brightness,
	.max_brightness	= SONY_MAX_BRIGHTNESS - 1,
};

static struct sony_acpi_value {
	char			*name;	 /* name of the entry */
	struct proc_dir_entry 	*proc;	 /* /proc entry */
	char			*acpiget;/* name of the ACPI get function */
	char			*acpiset;/* name of the ACPI get function */
	int 			min;	 /* minimum allowed value or -1 */
	int			max;	 /* maximum allowed value or -1 */
	int			value;	 /* current setting */
	int			valid;	 /* Has ever been set */
	int			debug;	 /* active only in debug mode ? */
} sony_acpi_values[] = {
	{
		.name		= "brightness_default",
		.acpiget	= "GPBR",
		.acpiset	= "SPBR",
		.min		= 1,
		.max		= SONY_MAX_BRIGHTNESS,
		.debug		= 0,
	},
	{
		.name           = "fnkey",
		.acpiget        = "GHKE",
		.debug          = 0,
	},
	{
		.name		= "cdpower",
		.acpiget	= "GCDP",
		.acpiset	= "SCDP",
		.min		= -1,
		.max		= -1,
		.debug		= 0,
	},
	{
		.name		= "PID",
		.acpiget	= "GPID",
		.debug		= 1,
	},
	{
		.name		= "CTR",
		.acpiget	= "GCTR",
		.acpiset	= "SCTR",
		.min		= -1,
		.max		= -1,
		.debug		= 1,
	},
	{
		.name		= "PCR",
		.acpiget	= "GPCR",
		.acpiset	= "SPCR",
		.min		= -1,
		.max		= -1,
		.debug		= 1,
	},
	{
		.name		= "CMI",
		.acpiget	= "GCMI",
		.acpiset	= "SCMI",
		.min		= -1,
		.max		= -1,
		.debug		= 1,
	},
	{
		.name		= NULL,
	}
};

static int acpi_callgetfunc(acpi_handle handle, char *name, int *result)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, NULL, &output);
	if ((status == AE_OK) && (out_obj.type == ACPI_TYPE_INTEGER)) {
		*result = out_obj.integer.value;
		return 0;
	}

	printk(LOG_PFX "acpi_callreadfunc failed\n");

	return -1;
}

static int acpi_callsetfunc(acpi_handle handle, char *name, int value,
			    int *result)
{
	struct acpi_object_list params;
	union acpi_object in_obj;
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = value;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, &params, &output);
	if (status == AE_OK) {
		if (result != NULL) {
			if (out_obj.type != ACPI_TYPE_INTEGER) {
				printk(LOG_PFX "acpi_evaluate_object bad "
				       "return type\n");
				return -1;
			}
			*result = out_obj.integer.value;
		}
		return 0;
	}

	printk(LOG_PFX "acpi_evaluate_object failed\n");

	return -1;
}

static int parse_buffer(const char __user *buffer, unsigned long count,
			int *val) {
	char s[32];
	int ret;

	if (count > 31)
		return -EINVAL;
	if (copy_from_user(s, buffer, count))
		return -EFAULT;
	s[count] = '\0';
	ret = simple_strtoul(s, NULL, 10);
	*val = ret;
	return 0;
}

static int sony_acpi_read(char* page, char** start, off_t off, int count,
			  int* eof, void *data)
{
	struct sony_acpi_value *item = data;
	int value;

	if (!item->acpiget)
		return -EIO;

	if (acpi_callgetfunc(sony_acpi_handle, item->acpiget, &value) < 0)
		return -EIO;

	return sprintf(page, "%d\n", value);
}

static int sony_acpi_write(struct file *file, const char __user *buffer,
			   unsigned long count, void *data)
{
	struct sony_acpi_value *item = data;
	int result;
	int value;

	if (!item->acpiset)
		return -EIO;

	if ((result = parse_buffer(buffer, count, &value)) < 0)
		return result;

	if (item->min != -1 && value < item->min)
		return -EINVAL;
	if (item->max != -1 && value > item->max)
		return -EINVAL;

	if (acpi_callsetfunc(sony_acpi_handle, item->acpiset, value, NULL) < 0)
		return -EIO;
	item->value = value;
	item->valid = 1;
	return count;
}

static int sony_acpi_resume(struct acpi_device *device)
{
	struct sony_acpi_value *item;

	for (item = sony_acpi_values; item->name; item++) {
		int ret;

		if (!item->valid)
			continue;
		ret = acpi_callsetfunc(sony_acpi_handle, item->acpiset,
					item->value, NULL);
		if (ret < 0) {
			printk("%s: %d\n", __FUNCTION__, ret);
			break;
		}
	}
	return 0;
}

static void sony_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	printk(LOG_PFX "sony_acpi_notify\n");
}

static acpi_status sony_walk_callback(acpi_handle handle, u32 level,
				      void *context, void **return_value)
{
	struct acpi_namespace_node *node;
	union acpi_operand_object *operand;

	node = (struct acpi_namespace_node *) handle;
	operand = (union acpi_operand_object *) node->object;

	printk(LOG_PFX "method: name: %4.4s, args %X\n", node->name.ascii,
	       (u32) operand->method.param_count);

	return AE_OK;
}

static int sony_acpi_add(struct acpi_device *device)
{
	acpi_status status;
	int result;
	acpi_handle handle;
	struct sony_acpi_value *item;

	sony_acpi_handle = device->handle;

	acpi_driver_data(device) = NULL;
	acpi_device_dir(device) = sony_acpi_dir;

	if (debug) {
		status = acpi_walk_namespace(ACPI_TYPE_METHOD, sony_acpi_handle,
					     1, sony_walk_callback, NULL, NULL);
		if (ACPI_FAILURE(status)) {
			printk(LOG_PFX "unable to walk acpi resources\n");
			result = -ENODEV;
			goto outwalk;
		}

		status = acpi_install_notify_handler(sony_acpi_handle,
						     ACPI_DEVICE_NOTIFY,
						     sony_acpi_notify,
						     NULL);
		if (ACPI_FAILURE(status)) {
			printk(LOG_PFX "unable to install notify handler\n");
			result = -ENODEV;
			goto outnotify;
		}
	}

	if (ACPI_SUCCESS(acpi_get_handle(sony_acpi_handle, "GBRT", &handle))) {
		sony_backlight_device = backlight_device_register("sony", NULL,
					&sony_backlight_properties);
	        if (IS_ERR(sony_backlight_device)) {
        	        printk(LOG_PFX "unable to register backlight device\n");
		}
	}

	for (item = sony_acpi_values; item->name; ++item) {
		if (!debug && item->debug)
			continue;

		if (item->acpiget &&
		    ACPI_FAILURE(acpi_get_handle(sony_acpi_handle,
		    		 item->acpiget, &handle)))
		    	continue;

		if (item->acpiset &&
		    ACPI_FAILURE(acpi_get_handle(sony_acpi_handle,
		    		 item->acpiset, &handle)))
		    	continue;

		item->proc = create_proc_entry(item->name, 0600,
					       acpi_device_dir(device));
		if (!item->proc) {
			printk(LOG_PFX "unable to create proc entry\n");
			result = -EIO;
			goto outproc;
		}

		item->proc->read_proc = sony_acpi_read;
		item->proc->write_proc = sony_acpi_write;
		item->proc->data = item;
		item->proc->owner = THIS_MODULE;
	}

	printk(KERN_INFO ACPI_SNC_DRIVER_NAME " successfully installed\n");

	return 0;

outproc:
	if (debug) {
		status = acpi_remove_notify_handler(sony_acpi_handle,
						    ACPI_DEVICE_NOTIFY,
						    sony_acpi_notify);
		if (ACPI_FAILURE(status))
			printk(LOG_PFX "unable to remove notify handler\n");
	}
outnotify:
	for (item = sony_acpi_values; item->name; ++item)
		if (item->proc)
			remove_proc_entry(item->name, acpi_device_dir(device));
outwalk:
	return result;
}

static int sony_acpi_remove(struct acpi_device *device, int type)
{
	acpi_status status;
	struct sony_acpi_value *item;

	if (sony_backlight_device)
		backlight_device_unregister(sony_backlight_device);

	if (debug) {
		status = acpi_remove_notify_handler(sony_acpi_handle,
						    ACPI_DEVICE_NOTIFY,
						    sony_acpi_notify);
		if (ACPI_FAILURE(status))
			printk(LOG_PFX "unable to remove notify handler\n");
	}

	for (item = sony_acpi_values; item->name; ++item)
		if (item->proc)
			remove_proc_entry(item->name, acpi_device_dir(device));

	printk(KERN_INFO ACPI_SNC_DRIVER_NAME " successfully removed\n");

	return 0;
}

static int sony_backlight_update_status(struct backlight_device *bd)
{
	return acpi_callsetfunc(sony_acpi_handle, "SBRT",
				bd->props->brightness + 1,
				NULL);
}

static int sony_backlight_get_brightness(struct backlight_device *bd)
{
	int value;

	if (acpi_callgetfunc(sony_acpi_handle, "GBRT", &value))
		return 0;
	/* brightness levels are 1-based, while backlight ones are 0-based */
	return value - 1;
}

static struct acpi_driver sony_acpi_driver = {
	.name	= ACPI_SNC_DRIVER_NAME,
	.class	= ACPI_SNC_CLASS,
	.ids	= ACPI_SNC_HID,
	.ops	= {
			.add	= sony_acpi_add,
			.remove	= sony_acpi_remove,
			.resume = sony_acpi_resume,
		  },
};

static int __init sony_acpi_init(void)
{
	int result;

	sony_acpi_dir = proc_mkdir("sony", acpi_root_dir);
	if (!sony_acpi_dir) {
		printk(LOG_PFX "unable to create /proc entry\n");
		return -ENODEV;
	}
	sony_acpi_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&sony_acpi_driver);
	if (result < 0) {
		remove_proc_entry("sony", acpi_root_dir);
		return -ENODEV;
	}
	return 0;
}


static void __exit sony_acpi_exit(void)
{
	acpi_bus_unregister_driver(&sony_acpi_driver);
	remove_proc_entry("sony", acpi_root_dir);
}

module_init(sony_acpi_init);
module_exit(sony_acpi_exit);

/*
 *  acpi_battery.c - ACPI Battery Driver ($Revision: 37 $)
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

#define ACPI_BATTERY_VALUE_UNKNOWN 0xFFFFFFFF

#define ACPI_BATTERY_FORMAT_BIF	"NNNNNNNNNSSSS"
#define ACPI_BATTERY_FORMAT_BST	"NNNN"

#define ACPI_BATTERY_COMPONENT		0x00040000
#define ACPI_BATTERY_CLASS		"battery"
#define ACPI_BATTERY_HID		"PNP0C0A"
#define ACPI_BATTERY_DRIVER_NAME	"ACPI Battery Driver"
#define ACPI_BATTERY_DEVICE_NAME	"Battery"
#define ACPI_BATTERY_FILE_INFO		"info"
#define ACPI_BATTERY_FILE_STATUS	"state"
#define ACPI_BATTERY_FILE_ALARM		"alarm"
#define ACPI_BATTERY_NOTIFY_STATUS	0x80
#define ACPI_BATTERY_NOTIFY_INFO	0x81
#define ACPI_BATTERY_UNITS_WATTS	"mW"
#define ACPI_BATTERY_UNITS_AMPS		"mA"

#define _COMPONENT		ACPI_BATTERY_COMPONENT
ACPI_MODULE_NAME("acpi_battery")

    MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_BATTERY_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int acpi_battery_add(struct acpi_device *device);
static int acpi_battery_remove(struct acpi_device *device, int type);

static struct acpi_driver acpi_battery_driver = {
	.name = ACPI_BATTERY_DRIVER_NAME,
	.class = ACPI_BATTERY_CLASS,
	.ids = ACPI_BATTERY_HID,
	.ops = {
		.add = acpi_battery_add,
		.remove = acpi_battery_remove,
		},
};

struct acpi_battery_status {
	acpi_integer state;
	acpi_integer present_rate;
	acpi_integer remaining_capacity;
	acpi_integer present_voltage;
};

struct acpi_battery_info {
	acpi_integer power_unit;
	acpi_integer design_capacity;
	acpi_integer last_full_capacity;
	acpi_integer battery_technology;
	acpi_integer design_voltage;
	acpi_integer design_capacity_warning;
	acpi_integer design_capacity_low;
	acpi_integer battery_capacity_granularity_1;
	acpi_integer battery_capacity_granularity_2;
	acpi_string model_number;
	acpi_string serial_number;
	acpi_string battery_type;
	acpi_string oem_info;
};

struct acpi_battery_flags {
	u8 present:1;		/* Bay occupied? */
	u8 power_unit:1;	/* 0=watts, 1=apms */
	u8 alarm:1;		/* _BTP present? */
	u8 reserved:5;
};

struct acpi_battery_trips {
	unsigned long warning;
	unsigned long low;
};

struct acpi_battery {
	acpi_handle handle;
	struct acpi_battery_flags flags;
	struct acpi_battery_trips trips;
	unsigned long alarm;
	struct acpi_battery_info *info;
};

/* --------------------------------------------------------------------------
                               Battery Management
   -------------------------------------------------------------------------- */

static int
acpi_battery_get_info(struct acpi_battery *battery,
		      struct acpi_battery_info **bif)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer format = { sizeof(ACPI_BATTERY_FORMAT_BIF),
		ACPI_BATTERY_FORMAT_BIF
	};
	struct acpi_buffer data = { 0, NULL };
	union acpi_object *package = NULL;

	ACPI_FUNCTION_TRACE("acpi_battery_get_info");

	if (!battery || !bif)
		return_VALUE(-EINVAL);

	/* Evalute _BIF */

	status = acpi_evaluate_object(battery->handle, "_BIF", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _BIF\n"));
		return_VALUE(-ENODEV);
	}

	package = (union acpi_object *)buffer.pointer;

	/* Extract Package Data */

	status = acpi_extract_package(package, &format, &data);
	if (status != AE_BUFFER_OVERFLOW) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error extracting _BIF\n"));
		result = -ENODEV;
		goto end;
	}

	data.pointer = kmalloc(data.length, GFP_KERNEL);
	if (!data.pointer) {
		result = -ENOMEM;
		goto end;
	}
	memset(data.pointer, 0, data.length);

	status = acpi_extract_package(package, &format, &data);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error extracting _BIF\n"));
		kfree(data.pointer);
		result = -ENODEV;
		goto end;
	}

      end:
	acpi_os_free(buffer.pointer);

	if (!result)
		(*bif) = (struct acpi_battery_info *)data.pointer;

	return_VALUE(result);
}

static int
acpi_battery_get_status(struct acpi_battery *battery,
			struct acpi_battery_status **bst)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer format = { sizeof(ACPI_BATTERY_FORMAT_BST),
		ACPI_BATTERY_FORMAT_BST
	};
	struct acpi_buffer data = { 0, NULL };
	union acpi_object *package = NULL;

	ACPI_FUNCTION_TRACE("acpi_battery_get_status");

	if (!battery || !bst)
		return_VALUE(-EINVAL);

	/* Evalute _BST */

	status = acpi_evaluate_object(battery->handle, "_BST", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _BST\n"));
		return_VALUE(-ENODEV);
	}

	package = (union acpi_object *)buffer.pointer;

	/* Extract Package Data */

	status = acpi_extract_package(package, &format, &data);
	if (status != AE_BUFFER_OVERFLOW) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error extracting _BST\n"));
		result = -ENODEV;
		goto end;
	}

	data.pointer = kmalloc(data.length, GFP_KERNEL);
	if (!data.pointer) {
		result = -ENOMEM;
		goto end;
	}
	memset(data.pointer, 0, data.length);

	status = acpi_extract_package(package, &format, &data);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error extracting _BST\n"));
		kfree(data.pointer);
		result = -ENODEV;
		goto end;
	}

      end:
	acpi_os_free(buffer.pointer);

	if (!result)
		(*bst) = (struct acpi_battery_status *)data.pointer;

	return_VALUE(result);
}

static int
acpi_battery_set_alarm(struct acpi_battery *battery, unsigned long alarm)
{
	acpi_status status = 0;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };

	ACPI_FUNCTION_TRACE("acpi_battery_set_alarm");

	if (!battery)
		return_VALUE(-EINVAL);

	if (!battery->flags.alarm)
		return_VALUE(-ENODEV);

	arg0.integer.value = alarm;

	status = acpi_evaluate_object(battery->handle, "_BTP", &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Alarm set to %d\n", (u32) alarm));

	battery->alarm = alarm;

	return_VALUE(0);
}

static int acpi_battery_check(struct acpi_battery *battery)
{
	int result = 0;
	acpi_status status = AE_OK;
	acpi_handle handle = NULL;
	struct acpi_device *device = NULL;
	struct acpi_battery_info *bif = NULL;

	ACPI_FUNCTION_TRACE("acpi_battery_check");

	if (!battery)
		return_VALUE(-EINVAL);

	result = acpi_bus_get_device(battery->handle, &device);
	if (result)
		return_VALUE(result);

	result = acpi_bus_get_status(device);
	if (result)
		return_VALUE(result);

	/* Insertion? */

	if (!battery->flags.present && device->status.battery_present) {

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Battery inserted\n"));

		/* Evalute _BIF to get certain static information */

		result = acpi_battery_get_info(battery, &bif);
		if (result)
			return_VALUE(result);

		battery->flags.power_unit = bif->power_unit;
		battery->trips.warning = bif->design_capacity_warning;
		battery->trips.low = bif->design_capacity_low;
		kfree(bif);

		/* See if alarms are supported, and if so, set default */

		status = acpi_get_handle(battery->handle, "_BTP", &handle);
		if (ACPI_SUCCESS(status)) {
			battery->flags.alarm = 1;
			acpi_battery_set_alarm(battery, battery->trips.warning);
		}
	}

	/* Removal? */

	else if (battery->flags.present && !device->status.battery_present) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Battery removed\n"));
	}

	battery->flags.present = device->status.battery_present;

	return_VALUE(result);
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_battery_dir;
static int acpi_battery_read_info(struct seq_file *seq, void *offset)
{
	int result = 0;
	struct acpi_battery *battery = (struct acpi_battery *)seq->private;
	struct acpi_battery_info *bif = NULL;
	char *units = "?";

	ACPI_FUNCTION_TRACE("acpi_battery_read_info");

	if (!battery)
		goto end;

	if (battery->flags.present)
		seq_printf(seq, "present:                 yes\n");
	else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	/* Battery Info (_BIF) */

	result = acpi_battery_get_info(battery, &bif);
	if (result || !bif) {
		seq_printf(seq, "ERROR: Unable to read battery information\n");
		goto end;
	}

	units =
	    bif->
	    power_unit ? ACPI_BATTERY_UNITS_AMPS : ACPI_BATTERY_UNITS_WATTS;

	if (bif->design_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "design capacity:         unknown\n");
	else
		seq_printf(seq, "design capacity:         %d %sh\n",
			   (u32) bif->design_capacity, units);

	if (bif->last_full_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "last full capacity:      unknown\n");
	else
		seq_printf(seq, "last full capacity:      %d %sh\n",
			   (u32) bif->last_full_capacity, units);

	switch ((u32) bif->battery_technology) {
	case 0:
		seq_printf(seq, "battery technology:      non-rechargeable\n");
		break;
	case 1:
		seq_printf(seq, "battery technology:      rechargeable\n");
		break;
	default:
		seq_printf(seq, "battery technology:      unknown\n");
		break;
	}

	if (bif->design_voltage == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "design voltage:          unknown\n");
	else
		seq_printf(seq, "design voltage:          %d mV\n",
			   (u32) bif->design_voltage);

	seq_printf(seq, "design capacity warning: %d %sh\n",
		   (u32) bif->design_capacity_warning, units);
	seq_printf(seq, "design capacity low:     %d %sh\n",
		   (u32) bif->design_capacity_low, units);
	seq_printf(seq, "capacity granularity 1:  %d %sh\n",
		   (u32) bif->battery_capacity_granularity_1, units);
	seq_printf(seq, "capacity granularity 2:  %d %sh\n",
		   (u32) bif->battery_capacity_granularity_2, units);
	seq_printf(seq, "model number:            %s\n", bif->model_number);
	seq_printf(seq, "serial number:           %s\n", bif->serial_number);
	seq_printf(seq, "battery type:            %s\n", bif->battery_type);
	seq_printf(seq, "OEM info:                %s\n", bif->oem_info);

      end:
	kfree(bif);

	return_VALUE(0);
}

static int acpi_battery_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_info, PDE(inode)->data);
}

static int acpi_battery_read_state(struct seq_file *seq, void *offset)
{
	int result = 0;
	struct acpi_battery *battery = (struct acpi_battery *)seq->private;
	struct acpi_battery_status *bst = NULL;
	char *units = "?";

	ACPI_FUNCTION_TRACE("acpi_battery_read_state");

	if (!battery)
		goto end;

	if (battery->flags.present)
		seq_printf(seq, "present:                 yes\n");
	else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	/* Battery Units */

	units =
	    battery->flags.
	    power_unit ? ACPI_BATTERY_UNITS_AMPS : ACPI_BATTERY_UNITS_WATTS;

	/* Battery Status (_BST) */

	result = acpi_battery_get_status(battery, &bst);
	if (result || !bst) {
		seq_printf(seq, "ERROR: Unable to read battery status\n");
		goto end;
	}

	if (!(bst->state & 0x04))
		seq_printf(seq, "capacity state:          ok\n");
	else
		seq_printf(seq, "capacity state:          critical\n");

	if ((bst->state & 0x01) && (bst->state & 0x02)) {
		seq_printf(seq,
			   "charging state:          charging/discharging\n");
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Battery Charging and Discharging?\n"));
	} else if (bst->state & 0x01)
		seq_printf(seq, "charging state:          discharging\n");
	else if (bst->state & 0x02)
		seq_printf(seq, "charging state:          charging\n");
	else {
		seq_printf(seq, "charging state:          charged\n");
	}

	if (bst->present_rate == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present rate:            unknown\n");
	else
		seq_printf(seq, "present rate:            %d %s\n",
			   (u32) bst->present_rate, units);

	if (bst->remaining_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "remaining capacity:      unknown\n");
	else
		seq_printf(seq, "remaining capacity:      %d %sh\n",
			   (u32) bst->remaining_capacity, units);

	if (bst->present_voltage == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present voltage:         unknown\n");
	else
		seq_printf(seq, "present voltage:         %d mV\n",
			   (u32) bst->present_voltage);

      end:
	kfree(bst);

	return_VALUE(0);
}

static int acpi_battery_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_state, PDE(inode)->data);
}

static int acpi_battery_read_alarm(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = (struct acpi_battery *)seq->private;
	char *units = "?";

	ACPI_FUNCTION_TRACE("acpi_battery_read_alarm");

	if (!battery)
		goto end;

	if (!battery->flags.present) {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	/* Battery Units */

	units =
	    battery->flags.
	    power_unit ? ACPI_BATTERY_UNITS_AMPS : ACPI_BATTERY_UNITS_WATTS;

	/* Battery Alarm */

	seq_printf(seq, "alarm:                   ");
	if (!battery->alarm)
		seq_printf(seq, "unsupported\n");
	else
		seq_printf(seq, "%d %sh\n", (u32) battery->alarm, units);

      end:
	return_VALUE(0);
}

static ssize_t
acpi_battery_write_alarm(struct file *file,
			 const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	int result = 0;
	char alarm_string[12] = { '\0' };
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_battery *battery = (struct acpi_battery *)m->private;

	ACPI_FUNCTION_TRACE("acpi_battery_write_alarm");

	if (!battery || (count > sizeof(alarm_string) - 1))
		return_VALUE(-EINVAL);

	if (!battery->flags.present)
		return_VALUE(-ENODEV);

	if (copy_from_user(alarm_string, buffer, count))
		return_VALUE(-EFAULT);

	alarm_string[count] = '\0';

	result = acpi_battery_set_alarm(battery,
					simple_strtoul(alarm_string, NULL, 0));
	if (result)
		return_VALUE(result);

	return_VALUE(count);
}

static int acpi_battery_alarm_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_alarm, PDE(inode)->data);
}

static struct file_operations acpi_battery_info_ops = {
	.open = acpi_battery_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct file_operations acpi_battery_state_ops = {
	.open = acpi_battery_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct file_operations acpi_battery_alarm_ops = {
	.open = acpi_battery_alarm_open_fs,
	.read = seq_read,
	.write = acpi_battery_write_alarm,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int acpi_battery_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_battery_add_fs");

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_battery_dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
		acpi_device_dir(device)->owner = THIS_MODULE;
	}

	/* 'info' [R] */
	entry = create_proc_entry(ACPI_BATTERY_FILE_INFO,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create '%s' fs entry\n",
				  ACPI_BATTERY_FILE_INFO));
	else {
		entry->proc_fops = &acpi_battery_info_ops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'status' [R] */
	entry = create_proc_entry(ACPI_BATTERY_FILE_STATUS,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create '%s' fs entry\n",
				  ACPI_BATTERY_FILE_STATUS));
	else {
		entry->proc_fops = &acpi_battery_state_ops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'alarm' [R/W] */
	entry = create_proc_entry(ACPI_BATTERY_FILE_ALARM,
				  S_IFREG | S_IRUGO | S_IWUSR,
				  acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create '%s' fs entry\n",
				  ACPI_BATTERY_FILE_ALARM));
	else {
		entry->proc_fops = &acpi_battery_alarm_ops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	return_VALUE(0);
}

static int acpi_battery_remove_fs(struct acpi_device *device)
{
	ACPI_FUNCTION_TRACE("acpi_battery_remove_fs");

	if (acpi_device_dir(device)) {
		remove_proc_entry(ACPI_BATTERY_FILE_ALARM,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_BATTERY_FILE_STATUS,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_BATTERY_FILE_INFO,
				  acpi_device_dir(device));

		remove_proc_entry(acpi_device_bid(device), acpi_battery_dir);
		acpi_device_dir(device) = NULL;
	}

	return_VALUE(0);
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_battery_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_battery *battery = (struct acpi_battery *)data;
	struct acpi_device *device = NULL;

	ACPI_FUNCTION_TRACE("acpi_battery_notify");

	if (!battery)
		return_VOID;

	if (acpi_bus_get_device(handle, &device))
		return_VOID;

	switch (event) {
	case ACPI_BATTERY_NOTIFY_STATUS:
	case ACPI_BATTERY_NOTIFY_INFO:
		acpi_battery_check(battery);
		acpi_bus_generate_event(device, event, battery->flags.present);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return_VOID;
}

static int acpi_battery_add(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_battery *battery = NULL;

	ACPI_FUNCTION_TRACE("acpi_battery_add");

	if (!device)
		return_VALUE(-EINVAL);

	battery = kmalloc(sizeof(struct acpi_battery), GFP_KERNEL);
	if (!battery)
		return_VALUE(-ENOMEM);
	memset(battery, 0, sizeof(struct acpi_battery));

	battery->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_BATTERY_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_BATTERY_CLASS);
	acpi_driver_data(device) = battery;

	result = acpi_battery_check(battery);
	if (result)
		goto end;

	result = acpi_battery_add_fs(device);
	if (result)
		goto end;

	status = acpi_install_notify_handler(battery->handle,
					     ACPI_DEVICE_NOTIFY,
					     acpi_battery_notify, battery);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error installing notify handler\n"));
		result = -ENODEV;
		goto end;
	}

	printk(KERN_INFO PREFIX "%s Slot [%s] (battery %s)\n",
	       ACPI_BATTERY_DEVICE_NAME, acpi_device_bid(device),
	       device->status.battery_present ? "present" : "absent");

      end:
	if (result) {
		acpi_battery_remove_fs(device);
		kfree(battery);
	}

	return_VALUE(result);
}

static int acpi_battery_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;
	struct acpi_battery *battery = NULL;

	ACPI_FUNCTION_TRACE("acpi_battery_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	battery = (struct acpi_battery *)acpi_driver_data(device);

	status = acpi_remove_notify_handler(battery->handle,
					    ACPI_DEVICE_NOTIFY,
					    acpi_battery_notify);
	if (ACPI_FAILURE(status))
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error removing notify handler\n"));

	acpi_battery_remove_fs(device);

	kfree(battery);

	return_VALUE(0);
}

static int __init acpi_battery_init(void)
{
	int result = 0;

	ACPI_FUNCTION_TRACE("acpi_battery_init");

	acpi_battery_dir = proc_mkdir(ACPI_BATTERY_CLASS, acpi_root_dir);
	if (!acpi_battery_dir)
		return_VALUE(-ENODEV);
	acpi_battery_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&acpi_battery_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_BATTERY_CLASS, acpi_root_dir);
		return_VALUE(-ENODEV);
	}

	return_VALUE(0);
}

static void __exit acpi_battery_exit(void)
{
	ACPI_FUNCTION_TRACE("acpi_battery_exit");

	acpi_bus_unregister_driver(&acpi_battery_driver);

	remove_proc_entry(ACPI_BATTERY_CLASS, acpi_root_dir);

	return_VOID;
}

module_init(acpi_battery_init);
module_exit(acpi_battery_exit);

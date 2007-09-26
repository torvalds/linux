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

#define ACPI_BATTERY_COMPONENT		0x00040000
#define ACPI_BATTERY_CLASS		"battery"
#define ACPI_BATTERY_DEVICE_NAME	"Battery"
#define ACPI_BATTERY_NOTIFY_STATUS	0x80
#define ACPI_BATTERY_NOTIFY_INFO	0x81
#define ACPI_BATTERY_UNITS_WATTS	"mW"
#define ACPI_BATTERY_UNITS_AMPS		"mA"

#define _COMPONENT		ACPI_BATTERY_COMPONENT

#define ACPI_BATTERY_UPDATE_TIME	0

#define ACPI_BATTERY_NONE_UPDATE	0
#define ACPI_BATTERY_EASY_UPDATE	1
#define ACPI_BATTERY_INIT_UPDATE	2

ACPI_MODULE_NAME("battery");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Battery Driver");
MODULE_LICENSE("GPL");

static unsigned int update_time = ACPI_BATTERY_UPDATE_TIME;

/* 0 - every time, > 0 - by update_time */
module_param(update_time, uint, 0644);

extern struct proc_dir_entry *acpi_lock_battery_dir(void);
extern void *acpi_unlock_battery_dir(struct proc_dir_entry *acpi_battery_dir);

static int acpi_battery_add(struct acpi_device *device);
static int acpi_battery_remove(struct acpi_device *device, int type);
static int acpi_battery_resume(struct acpi_device *device);

static const struct acpi_device_id battery_device_ids[] = {
	{"PNP0C0A", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, battery_device_ids);

static struct acpi_driver acpi_battery_driver = {
	.name = "battery",
	.class = ACPI_BATTERY_CLASS,
	.ids = battery_device_ids,
	.ops = {
		.add = acpi_battery_add,
		.resume = acpi_battery_resume,
		.remove = acpi_battery_remove,
		},
};

enum acpi_battery_files {
	ACPI_BATTERY_INFO = 0,
	ACPI_BATTERY_STATE,
	ACPI_BATTERY_ALARM,
	ACPI_BATTERY_NUMFILES,
};

struct acpi_battery {
	struct acpi_device *device;
	struct mutex lock;
	unsigned long alarm;
	unsigned long update_time[ACPI_BATTERY_NUMFILES];
	int state;
	int present_rate;
	int remaining_capacity;
	int present_voltage;
	int power_unit;
	int design_capacity;
	int last_full_capacity;
	int technology;
	int design_voltage;
	int design_capacity_warning;
	int design_capacity_low;
	int capacity_granularity_1;
	int capacity_granularity_2;
	char model_number[32];
	char serial_number[32];
	char type[32];
	char oem_info[32];
	u8 present_prev;
	u8 alarm_present;
	u8 init_update;
	u8 update[ACPI_BATTERY_NUMFILES];
};

inline int acpi_battery_present(struct acpi_battery *battery)
{
	return battery->device->status.battery_present;
}

inline char *acpi_battery_power_units(struct acpi_battery *battery)
{
	if (battery->power_unit)
		return ACPI_BATTERY_UNITS_AMPS;
	else
		return ACPI_BATTERY_UNITS_WATTS;
}

inline acpi_handle acpi_battery_handle(struct acpi_battery *battery)
{
	return battery->device->handle;
}

/* --------------------------------------------------------------------------
                               Battery Management
   -------------------------------------------------------------------------- */

static void acpi_battery_check_result(struct acpi_battery *battery, int result)
{
	if (!battery)
		return;

	if (result) {
		battery->init_update = 1;
	}
}

struct acpi_offsets {
	size_t offset;		/* offset inside struct acpi_sbs_battery */
	u8 mode;		/* int or string? */
};

static struct acpi_offsets state_offsets[] = {
	{offsetof(struct acpi_battery, state), 0},
	{offsetof(struct acpi_battery, present_rate), 0},
	{offsetof(struct acpi_battery, remaining_capacity), 0},
	{offsetof(struct acpi_battery, present_voltage), 0},
};

static struct acpi_offsets info_offsets[] = {
	{offsetof(struct acpi_battery, power_unit), 0},
	{offsetof(struct acpi_battery, design_capacity), 0},
	{offsetof(struct acpi_battery, last_full_capacity), 0},
	{offsetof(struct acpi_battery, technology), 0},
	{offsetof(struct acpi_battery, design_voltage), 0},
	{offsetof(struct acpi_battery, design_capacity_warning), 0},
	{offsetof(struct acpi_battery, design_capacity_low), 0},
	{offsetof(struct acpi_battery, capacity_granularity_1), 0},
	{offsetof(struct acpi_battery, capacity_granularity_2), 0},
	{offsetof(struct acpi_battery, model_number), 1},
	{offsetof(struct acpi_battery, serial_number), 1},
	{offsetof(struct acpi_battery, type), 1},
	{offsetof(struct acpi_battery, oem_info), 1},
};

static int extract_package(struct acpi_battery *battery,
			   union acpi_object *package,
			   struct acpi_offsets *offsets, int num)
{
	int i, *x;
	union acpi_object *element;
	if (package->type != ACPI_TYPE_PACKAGE)
		return -EFAULT;
	for (i = 0; i < num; ++i) {
		if (package->package.count <= i)
			return -EFAULT;
		element = &package->package.elements[i];
		if (offsets[i].mode) {
			if (element->type != ACPI_TYPE_STRING &&
			    element->type != ACPI_TYPE_BUFFER)
				return -EFAULT;
			strncpy((u8 *)battery + offsets[i].offset,
				element->string.pointer, 32);
		} else {
			if (element->type != ACPI_TYPE_INTEGER)
				return -EFAULT;
			x = (int *)((u8 *)battery + offsets[i].offset);
			*x = element->integer.value;
		}
	}
	return 0;
}

static int acpi_battery_get_status(struct acpi_battery *battery)
{
	int result = 0;

	result = acpi_bus_get_status(battery->device);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "Evaluating _STA"));
		return -ENODEV;
	}
	return result;
}

static int acpi_battery_get_info(struct acpi_battery *battery)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	battery->update_time[ACPI_BATTERY_INFO] = get_seconds();
	if (!acpi_battery_present(battery))
		return 0;
	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(acpi_battery_handle(battery), "_BIF",
				      NULL, &buffer);
	mutex_unlock(&battery->lock);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _BIF"));
		return -ENODEV;
	}
	result = extract_package(battery, buffer.pointer,
				 info_offsets, ARRAY_SIZE(info_offsets));
	kfree(buffer.pointer);
	return result;
}

static int acpi_battery_get_state(struct acpi_battery *battery)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	battery->update_time[ACPI_BATTERY_STATE] = get_seconds();

	if (!acpi_battery_present(battery))
		return 0;

	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(acpi_battery_handle(battery), "_BST",
				      NULL, &buffer);
	mutex_unlock(&battery->lock);

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _BST"));
		return -ENODEV;
	}
	result = extract_package(battery, buffer.pointer,
				 state_offsets, ARRAY_SIZE(state_offsets));
	kfree(buffer.pointer);
	return result;
}

static int acpi_battery_get_alarm(struct acpi_battery *battery)
{
	battery->update_time[ACPI_BATTERY_ALARM] = get_seconds();

	return 0;
}

static int acpi_battery_set_alarm(struct acpi_battery *battery,
				  unsigned long alarm)
{
	acpi_status status = 0;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };

	battery->update_time[ACPI_BATTERY_ALARM] = get_seconds();

	if (!acpi_battery_present(battery))
		return -ENODEV;

	if (!battery->alarm_present)
		return -ENODEV;

	arg0.integer.value = alarm;

	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(acpi_battery_handle(battery), "_BTP",
				      &arg_list, NULL);
	mutex_unlock(&battery->lock);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Alarm set to %d\n", (u32) alarm));

	battery->alarm = alarm;

	return 0;
}

static int acpi_battery_init_alarm(struct acpi_battery *battery)
{
	int result = 0;
	acpi_status status = AE_OK;
	acpi_handle handle = NULL;
	unsigned long alarm = battery->alarm;

	/* See if alarms are supported, and if so, set default */

	status = acpi_get_handle(acpi_battery_handle(battery), "_BTP", &handle);
	if (ACPI_SUCCESS(status)) {
		battery->alarm_present = 1;
		if (!alarm) {
			alarm = battery->design_capacity_warning;
		}
		result = acpi_battery_set_alarm(battery, alarm);
		if (result)
			goto end;
	} else {
		battery->alarm_present = 0;
	}

      end:

	return result;
}

static int acpi_battery_init_update(struct acpi_battery *battery)
{
	int result = 0;

	result = acpi_battery_get_status(battery);
	if (result)
		return result;

	battery->present_prev = acpi_battery_present(battery);

	if (acpi_battery_present(battery)) {
		result = acpi_battery_get_info(battery);
		if (result)
			return result;
		result = acpi_battery_get_state(battery);
		if (result)
			return result;

		acpi_battery_init_alarm(battery);
	}

	return result;
}

static int acpi_battery_update(struct acpi_battery *battery,
			       int update, int *update_result_ptr)
{
	int result = 0;
	int update_result = ACPI_BATTERY_NONE_UPDATE;

	if (!acpi_battery_present(battery)) {
		update = 1;
	}

	if (battery->init_update) {
		result = acpi_battery_init_update(battery);
		if (result)
			goto end;
		update_result = ACPI_BATTERY_INIT_UPDATE;
	} else if (update) {
		result = acpi_battery_get_status(battery);
		if (result)
			goto end;
		if ((!battery->present_prev & acpi_battery_present(battery))
		    || (battery->present_prev & !acpi_battery_present(battery))) {
			result = acpi_battery_init_update(battery);
			if (result)
				goto end;
			update_result = ACPI_BATTERY_INIT_UPDATE;
		} else {
			update_result = ACPI_BATTERY_EASY_UPDATE;
		}
	}

      end:

	battery->init_update = (result != 0);

	*update_result_ptr = update_result;

	return result;
}

static void acpi_battery_notify_update(struct acpi_battery *battery)
{
	acpi_battery_get_status(battery);

	if (battery->init_update) {
		return;
	}

	if ((!battery->present_prev &
	     acpi_battery_present(battery)) ||
	    (battery->present_prev &
	     !acpi_battery_present(battery))) {
		battery->init_update = 1;
	} else {
		battery->update[ACPI_BATTERY_INFO] = 1;
		battery->update[ACPI_BATTERY_STATE] = 1;
		battery->update[ACPI_BATTERY_ALARM] = 1;
	}
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_battery_dir;

static int acpi_battery_print_info(struct seq_file *seq, int result)
{
	struct acpi_battery *battery = seq->private;
	char *units = "?";

	if (result)
		goto end;

	if (acpi_battery_present(battery))
		seq_printf(seq, "present:                 yes\n");
	else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	/* Battery Units */

	units = acpi_battery_power_units(battery);

	if (battery->design_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "design capacity:         unknown\n");
	else
		seq_printf(seq, "design capacity:         %d %sh\n",
			   (u32) battery->design_capacity, units);

	if (battery->last_full_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "last full capacity:      unknown\n");
	else
		seq_printf(seq, "last full capacity:      %d %sh\n",
			   (u32) battery->last_full_capacity, units);

	switch ((u32) battery->technology) {
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

	if (battery->design_voltage == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "design voltage:          unknown\n");
	else
		seq_printf(seq, "design voltage:          %d mV\n",
			   (u32) battery->design_voltage);
	seq_printf(seq, "design capacity warning: %d %sh\n",
		   (u32) battery->design_capacity_warning, units);
	seq_printf(seq, "design capacity low:     %d %sh\n",
		   (u32) battery->design_capacity_low, units);
	seq_printf(seq, "capacity granularity 1:  %d %sh\n",
		   (u32) battery->capacity_granularity_1, units);
	seq_printf(seq, "capacity granularity 2:  %d %sh\n",
		   (u32) battery->capacity_granularity_2, units);
	seq_printf(seq, "model number:            %s\n", battery->model_number);
	seq_printf(seq, "serial number:           %s\n", battery->serial_number);
	seq_printf(seq, "battery type:            %s\n", battery->type);
	seq_printf(seq, "OEM info:                %s\n", battery->oem_info);

      end:

	if (result)
		seq_printf(seq, "ERROR: Unable to read battery info\n");

	return result;
}

static int acpi_battery_print_state(struct seq_file *seq, int result)
{
	struct acpi_battery *battery = seq->private;
	char *units = "?";

	if (result)
		goto end;

	if (acpi_battery_present(battery))
		seq_printf(seq, "present:                 yes\n");
	else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	/* Battery Units */

	units = acpi_battery_power_units(battery);

	if (!(battery->state & 0x04))
		seq_printf(seq, "capacity state:          ok\n");
	else
		seq_printf(seq, "capacity state:          critical\n");

	if ((battery->state & 0x01) && (battery->state & 0x02)) {
		seq_printf(seq,
			   "charging state:          charging/discharging\n");
	} else if (battery->state & 0x01)
		seq_printf(seq, "charging state:          discharging\n");
	else if (battery->state & 0x02)
		seq_printf(seq, "charging state:          charging\n");
	else {
		seq_printf(seq, "charging state:          charged\n");
	}

	if (battery->present_rate == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present rate:            unknown\n");
	else
		seq_printf(seq, "present rate:            %d %s\n",
			   (u32) battery->present_rate, units);

	if (battery->remaining_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "remaining capacity:      unknown\n");
	else
		seq_printf(seq, "remaining capacity:      %d %sh\n",
			   (u32) battery->remaining_capacity, units);

	if (battery->present_voltage == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present voltage:         unknown\n");
	else
		seq_printf(seq, "present voltage:         %d mV\n",
			   (u32) battery->present_voltage);

      end:

	if (result) {
		seq_printf(seq, "ERROR: Unable to read battery state\n");
	}

	return result;
}

static int acpi_battery_print_alarm(struct seq_file *seq, int result)
{
	struct acpi_battery *battery = seq->private;
	char *units = "?";

	if (result)
		goto end;

	if (!acpi_battery_present(battery)) {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	/* Battery Units */

	units = acpi_battery_power_units(battery);

	seq_printf(seq, "alarm:                   ");
	if (!battery->alarm)
		seq_printf(seq, "unsupported\n");
	else
		seq_printf(seq, "%lu %sh\n", battery->alarm, units);

      end:

	if (result)
		seq_printf(seq, "ERROR: Unable to read battery alarm\n");

	return result;
}

static ssize_t
acpi_battery_write_alarm(struct file *file,
			 const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	int result = 0;
	char alarm_string[12] = { '\0' };
	struct seq_file *m = file->private_data;
	struct acpi_battery *battery = m->private;
	int update_result = ACPI_BATTERY_NONE_UPDATE;

	if (!battery || (count > sizeof(alarm_string) - 1))
		return -EINVAL;

	result = acpi_battery_update(battery, 1, &update_result);
	if (result) {
		result = -ENODEV;
		goto end;
	}

	if (!acpi_battery_present(battery)) {
		result = -ENODEV;
		goto end;
	}

	if (copy_from_user(alarm_string, buffer, count)) {
		result = -EFAULT;
		goto end;
	}

	alarm_string[count] = '\0';

	result = acpi_battery_set_alarm(battery,
					simple_strtoul(alarm_string, NULL, 0));
	if (result)
		goto end;

      end:

	acpi_battery_check_result(battery, result);

	if (!result)
		result = count;
	return result;
}

typedef int(*print_func)(struct seq_file *seq, int result);
typedef int(*get_func)(struct acpi_battery *battery);

static struct acpi_read_mux {
	print_func print;
	get_func get;
} acpi_read_funcs[ACPI_BATTERY_NUMFILES] = {
	{.get = acpi_battery_get_info, .print = acpi_battery_print_info},
	{.get = acpi_battery_get_state, .print = acpi_battery_print_state},
	{.get = acpi_battery_get_alarm, .print = acpi_battery_print_alarm},
};

static int acpi_battery_read(int fid, struct seq_file *seq)
{
	struct acpi_battery *battery = seq->private;
	int result = 0;
	int update_result = ACPI_BATTERY_NONE_UPDATE;
	int update = 0;

	update = (get_seconds() - battery->update_time[fid] >= update_time);
	update = (update | battery->update[fid]);

	result = acpi_battery_update(battery, update, &update_result);
	if (result)
		goto end;

	if (update_result == ACPI_BATTERY_EASY_UPDATE) {
		result = acpi_read_funcs[fid].get(battery);
		if (result)
			goto end;
	}

      end:
	result = acpi_read_funcs[fid].print(seq, result);
	acpi_battery_check_result(battery, result);
	battery->update[fid] = result;
	return result;
}

static int acpi_battery_read_info(struct seq_file *seq, void *offset)
{
	return acpi_battery_read(ACPI_BATTERY_INFO, seq);
}

static int acpi_battery_read_state(struct seq_file *seq, void *offset)
{
	return acpi_battery_read(ACPI_BATTERY_STATE, seq);
}

static int acpi_battery_read_alarm(struct seq_file *seq, void *offset)
{
	return acpi_battery_read(ACPI_BATTERY_ALARM, seq);
}

static int acpi_battery_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_info, PDE(inode)->data);
}

static int acpi_battery_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_state, PDE(inode)->data);
}

static int acpi_battery_alarm_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_alarm, PDE(inode)->data);
}

static struct battery_file {
	struct file_operations ops;
	mode_t mode;
	char *name;
} acpi_battery_file[] = {
	{
	.name = "info",
	.mode = S_IRUGO,
	.ops = {
	.open = acpi_battery_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
	},
	},
	{
	.name = "state",
	.mode = S_IRUGO,
	.ops = {
	.open = acpi_battery_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
	},
	},
	{
	.name = "alarm",
	.mode = S_IFREG | S_IRUGO | S_IWUSR,
	.ops = {
	.open = acpi_battery_alarm_open_fs,
	.read = seq_read,
	.write = acpi_battery_write_alarm,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
	},
	},
};

static int acpi_battery_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;
	int i;

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_battery_dir);
		if (!acpi_device_dir(device))
			return -ENODEV;
		acpi_device_dir(device)->owner = THIS_MODULE;
	}

	for (i = 0; i < ACPI_BATTERY_NUMFILES; ++i) {
		entry = create_proc_entry(acpi_battery_file[i].name,
				  acpi_battery_file[i].mode, acpi_device_dir(device));
		if (!entry)
			return -ENODEV;
		else {
			entry->proc_fops = &acpi_battery_file[i].ops;
			entry->data = acpi_driver_data(device);
			entry->owner = THIS_MODULE;
		}
	}

	return 0;
}

static int acpi_battery_remove_fs(struct acpi_device *device)
{
	int i;
	if (acpi_device_dir(device)) {
		for (i = 0; i < ACPI_BATTERY_NUMFILES; ++i) {
			remove_proc_entry(acpi_battery_file[i].name,
				  acpi_device_dir(device));
		}
		remove_proc_entry(acpi_device_bid(device), acpi_battery_dir);
		acpi_device_dir(device) = NULL;
	}

	return 0;
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_battery_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_battery *battery = data;
	struct acpi_device *device = NULL;

	if (!battery)
		return;

	device = battery->device;

	switch (event) {
	case ACPI_BATTERY_NOTIFY_STATUS:
	case ACPI_BATTERY_NOTIFY_INFO:
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		device = battery->device;
		acpi_battery_notify_update(battery);
		acpi_bus_generate_proc_event(device, event,
					acpi_battery_present(battery));
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  device->dev.bus_id, event,
						  acpi_battery_present(battery));
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return;
}

static int acpi_battery_add(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_battery *battery = NULL;

	if (!device)
		return -EINVAL;

	battery = kzalloc(sizeof(struct acpi_battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	battery->device = device;
	strcpy(acpi_device_name(device), ACPI_BATTERY_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_BATTERY_CLASS);
	acpi_driver_data(device) = battery;
	mutex_init(&battery->lock);
	result = acpi_battery_get_status(battery);
	if (result)
		goto end;

	battery->init_update = 1;

	result = acpi_battery_add_fs(device);
	if (result)
		goto end;

	status = acpi_install_notify_handler(device->handle,
					     ACPI_ALL_NOTIFY,
					     acpi_battery_notify, battery);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Installing notify handler"));
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

	return result;
}

static int acpi_battery_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;
	struct acpi_battery *battery = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	battery = acpi_driver_data(device);

	status = acpi_remove_notify_handler(device->handle,
					    ACPI_ALL_NOTIFY,
					    acpi_battery_notify);

	acpi_battery_remove_fs(device);
	mutex_destroy(&battery->lock);
	kfree(battery);

	return 0;
}

/* this is needed to learn about changes made in suspended state */
static int acpi_battery_resume(struct acpi_device *device)
{
	struct acpi_battery *battery;

	if (!device)
		return -EINVAL;

	battery = device->driver_data;

	battery->init_update = 1;

	return 0;
}

static int __init acpi_battery_init(void)
{
	int result;

	if (acpi_disabled)
		return -ENODEV;

	acpi_battery_dir = acpi_lock_battery_dir();
	if (!acpi_battery_dir)
		return -ENODEV;

	result = acpi_bus_register_driver(&acpi_battery_driver);
	if (result < 0) {
		acpi_unlock_battery_dir(acpi_battery_dir);
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_battery_exit(void)
{
	acpi_bus_unregister_driver(&acpi_battery_driver);

	acpi_unlock_battery_dir(acpi_battery_dir);

	return;
}

module_init(acpi_battery_init);
module_exit(acpi_battery_exit);

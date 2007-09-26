/*
 *  battery.c - ACPI Battery Driver (Revision: 2.0)
 *
 *  Copyright (C) 2007 Alexey Starikovskiy <astarikovskiy@suse.de>
 *  Copyright (C) 2004-2007 Vladimir Lebedev <vladimir.p.lebedev@intel.com>
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
#include <linux/jiffies.h>
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

#define _COMPONENT		ACPI_BATTERY_COMPONENT

ACPI_MODULE_NAME("battery");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_AUTHOR("Alexey Starikovskiy <astarikovskiy@suse.de>");
MODULE_DESCRIPTION("ACPI Battery Driver");
MODULE_LICENSE("GPL");

static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

extern struct proc_dir_entry *acpi_lock_battery_dir(void);
extern void *acpi_unlock_battery_dir(struct proc_dir_entry *acpi_battery_dir);

static const struct acpi_device_id battery_device_ids[] = {
	{"PNP0C0A", 0},
	{"", 0},
};

MODULE_DEVICE_TABLE(acpi, battery_device_ids);

enum acpi_battery_files {
	info_tag = 0,
	state_tag,
	alarm_tag,
	ACPI_BATTERY_NUMFILES,
};

struct acpi_battery {
	struct mutex lock;
	struct acpi_device *device;
	unsigned long update_time;
	int present_rate;
	int remaining_capacity;
	int present_voltage;
	int design_capacity;
	int last_full_capacity;
	int technology;
	int design_voltage;
	int design_capacity_warning;
	int design_capacity_low;
	int capacity_granularity_1;
	int capacity_granularity_2;
	int alarm;
	char model_number[32];
	char serial_number[32];
	char type[32];
	char oem_info[32];
	int state;
	int power_unit;
	u8 alarm_present;
};

inline int acpi_battery_present(struct acpi_battery *battery)
{
	return battery->device->status.battery_present;
}

inline char *acpi_battery_units(struct acpi_battery *battery)
{
	return (battery->power_unit)?"mA":"mW";
}

/* --------------------------------------------------------------------------
                               Battery Management
   -------------------------------------------------------------------------- */
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
	if (acpi_bus_get_status(battery->device)) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "Evaluating _STA"));
		return -ENODEV;
	}
	return 0;
}

static int acpi_battery_get_info(struct acpi_battery *battery)
{
	int result = -EFAULT;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	if (!acpi_battery_present(battery))
		return 0;
	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(battery->device->handle, "_BIF",
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

	if (!acpi_battery_present(battery))
		return 0;

	if (battery->update_time &&
	    time_before(jiffies, battery->update_time +
			msecs_to_jiffies(cache_time)))
		return 0;

	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(battery->device->handle, "_BST",
				      NULL, &buffer);
	mutex_unlock(&battery->lock);

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _BST"));
		return -ENODEV;
	}

	result = extract_package(battery, buffer.pointer,
				 state_offsets, ARRAY_SIZE(state_offsets));
	battery->update_time = jiffies;
	kfree(buffer.pointer);
	return result;
}

static int acpi_battery_set_alarm(struct acpi_battery *battery)
{
	acpi_status status = 0;
	union acpi_object arg0 = { .type = ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };

	if (!acpi_battery_present(battery)|| !battery->alarm_present)
		return -ENODEV;

	arg0.integer.value = battery->alarm;

	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(battery->device->handle, "_BTP",
				 &arg_list, NULL);
	mutex_unlock(&battery->lock);

	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Alarm set to %d\n", battery->alarm));
	return 0;
}

static int acpi_battery_init_alarm(struct acpi_battery *battery)
{
	acpi_status status = AE_OK;
	acpi_handle handle = NULL;

	/* See if alarms are supported, and if so, set default */
	status = acpi_get_handle(battery->device->handle, "_BTP", &handle);
	if (ACPI_FAILURE(status)) {
		battery->alarm_present = 0;
		return 0;
	}
	battery->alarm_present = 1;
	if (!battery->alarm)
		battery->alarm = battery->design_capacity_warning;
	return acpi_battery_set_alarm(battery);
}

static int acpi_battery_update(struct acpi_battery *battery)
{
	int saved_present = acpi_battery_present(battery);
	int result = acpi_battery_get_status(battery);
	if (result || !acpi_battery_present(battery))
		return result;
	if (saved_present != acpi_battery_present(battery) ||
	    !battery->update_time) {
		battery->update_time = 0;
		result = acpi_battery_get_info(battery);
		if (result)
			return result;
		acpi_battery_init_alarm(battery);
	}
	return acpi_battery_get_state(battery);
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_battery_dir;

static int acpi_battery_print_info(struct seq_file *seq, int result)
{
	struct acpi_battery *battery = seq->private;

	if (result)
		goto end;

	seq_printf(seq, "present:                 %s\n",
		   acpi_battery_present(battery)?"yes":"no");
	if (!acpi_battery_present(battery))
		goto end;
	if (battery->design_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "design capacity:         unknown\n");
	else
		seq_printf(seq, "design capacity:         %d %sh\n",
			   battery->design_capacity,
			   acpi_battery_units(battery));

	if (battery->last_full_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "last full capacity:      unknown\n");
	else
		seq_printf(seq, "last full capacity:      %d %sh\n",
			   battery->last_full_capacity,
			   acpi_battery_units(battery));

	seq_printf(seq, "battery technology:      %srechargeable\n",
		   (!battery->technology)?"non-":"");

	if (battery->design_voltage == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "design voltage:          unknown\n");
	else
		seq_printf(seq, "design voltage:          %d mV\n",
			   battery->design_voltage);
	seq_printf(seq, "design capacity warning: %d %sh\n",
		   battery->design_capacity_warning,
		   acpi_battery_units(battery));
	seq_printf(seq, "design capacity low:     %d %sh\n",
		   battery->design_capacity_low,
		   acpi_battery_units(battery));
	seq_printf(seq, "capacity granularity 1:  %d %sh\n",
		   battery->capacity_granularity_1,
		   acpi_battery_units(battery));
	seq_printf(seq, "capacity granularity 2:  %d %sh\n",
		   battery->capacity_granularity_2,
		   acpi_battery_units(battery));
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

	if (result)
		goto end;

	seq_printf(seq, "present:                 %s\n",
		   acpi_battery_present(battery)?"yes":"no");
	if (!acpi_battery_present(battery))
		goto end;

	seq_printf(seq, "capacity state:          %s\n",
			(battery->state & 0x04)?"critical":"ok");
	if ((battery->state & 0x01) && (battery->state & 0x02))
		seq_printf(seq,
			   "charging state:          charging/discharging\n");
	else if (battery->state & 0x01)
		seq_printf(seq, "charging state:          discharging\n");
	else if (battery->state & 0x02)
		seq_printf(seq, "charging state:          charging\n");
	else
		seq_printf(seq, "charging state:          charged\n");

	if (battery->present_rate == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present rate:            unknown\n");
	else
		seq_printf(seq, "present rate:            %d %s\n",
			   battery->present_rate, acpi_battery_units(battery));

	if (battery->remaining_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "remaining capacity:      unknown\n");
	else
		seq_printf(seq, "remaining capacity:      %d %sh\n",
			   battery->remaining_capacity, acpi_battery_units(battery));
	if (battery->present_voltage == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present voltage:         unknown\n");
	else
		seq_printf(seq, "present voltage:         %d mV\n",
			   battery->present_voltage);
      end:
	if (result)
		seq_printf(seq, "ERROR: Unable to read battery state\n");

	return result;
}

static int acpi_battery_print_alarm(struct seq_file *seq, int result)
{
	struct acpi_battery *battery = seq->private;

	if (result)
		goto end;

	if (!acpi_battery_present(battery)) {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}
	seq_printf(seq, "alarm:                   ");
	if (!battery->alarm)
		seq_printf(seq, "unsupported\n");
	else
		seq_printf(seq, "%u %sh\n", battery->alarm,
				acpi_battery_units(battery));
      end:
	if (result)
		seq_printf(seq, "ERROR: Unable to read battery alarm\n");
	return result;
}

static ssize_t acpi_battery_write_alarm(struct file *file,
					const char __user * buffer,
					size_t count, loff_t * ppos)
{
	int result = 0;
	char alarm_string[12] = { '\0' };
	struct seq_file *m = file->private_data;
	struct acpi_battery *battery = m->private;

	if (!battery || (count > sizeof(alarm_string) - 1))
		return -EINVAL;
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
	battery->alarm = simple_strtol(alarm_string, NULL, 0);
	result = acpi_battery_set_alarm(battery);
      end:
	if (!result)
		return count;
	return result;
}

typedef int(*print_func)(struct seq_file *seq, int result);

static print_func acpi_print_funcs[ACPI_BATTERY_NUMFILES] = {
	acpi_battery_print_info,
	acpi_battery_print_state,
	acpi_battery_print_alarm,
};

static int acpi_battery_read(int fid, struct seq_file *seq)
{
	struct acpi_battery *battery = seq->private;
	int result = acpi_battery_update(battery);
	return acpi_print_funcs[fid](seq, result);
}

#define DECLARE_FILE_FUNCTIONS(_name) \
static int acpi_battery_read_##_name(struct seq_file *seq, void *offset) \
{ \
	return acpi_battery_read(_name##_tag, seq); \
} \
static int acpi_battery_##_name##_open_fs(struct inode *inode, struct file *file) \
{ \
	return single_open(file, acpi_battery_read_##_name, PDE(inode)->data); \
}

DECLARE_FILE_FUNCTIONS(info);
DECLARE_FILE_FUNCTIONS(state);
DECLARE_FILE_FUNCTIONS(alarm);

#undef DECLARE_FILE_FUNCTIONS

#define FILE_DESCRIPTION_RO(_name) \
	{ \
	.name = __stringify(_name), \
	.mode = S_IRUGO, \
	.ops = { \
		.open = acpi_battery_##_name##_open_fs, \
		.read = seq_read, \
		.llseek = seq_lseek, \
		.release = single_release, \
		.owner = THIS_MODULE, \
		}, \
	}

#define FILE_DESCRIPTION_RW(_name) \
	{ \
	.name = __stringify(_name), \
	.mode = S_IFREG | S_IRUGO | S_IWUSR, \
	.ops = { \
		.open = acpi_battery_##_name##_open_fs, \
		.read = seq_read, \
		.llseek = seq_lseek, \
		.write = acpi_battery_write_##_name, \
		.release = single_release, \
		.owner = THIS_MODULE, \
		}, \
	}

static struct battery_file {
	struct file_operations ops;
	mode_t mode;
	char *name;
} acpi_battery_file[] = {
	FILE_DESCRIPTION_RO(info),
	FILE_DESCRIPTION_RO(state),
	FILE_DESCRIPTION_RW(alarm),
};

#undef FILE_DESCRIPTION_RO
#undef FILE_DESCRIPTION_RW

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

static void acpi_battery_remove_fs(struct acpi_device *device)
{
	int i;
	if (!acpi_device_dir(device))
		return;
	for (i = 0; i < ACPI_BATTERY_NUMFILES; ++i)
		remove_proc_entry(acpi_battery_file[i].name,
				  acpi_device_dir(device));

	remove_proc_entry(acpi_device_bid(device), acpi_battery_dir);
	acpi_device_dir(device) = NULL;
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_battery_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_battery *battery = data;
	struct acpi_device *device;
	if (!battery)
		return;
	device = battery->device;
	acpi_battery_update(battery);
	acpi_bus_generate_proc_event(device, event,
				     acpi_battery_present(battery));
	acpi_bus_generate_netlink_event(device->pnp.device_class,
					device->dev.bus_id, event,
					acpi_battery_present(battery));
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
	acpi_battery_update(battery);
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
	battery = acpi_driver_data(device);
	battery->update_time = 0;
	return 0;
}

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

static int __init acpi_battery_init(void)
{
	if (acpi_disabled)
		return -ENODEV;
	acpi_battery_dir = acpi_lock_battery_dir();
	if (!acpi_battery_dir)
		return -ENODEV;
	if (acpi_bus_register_driver(&acpi_battery_driver) < 0) {
		acpi_unlock_battery_dir(acpi_battery_dir);
		return -ENODEV;
	}
	return 0;
}

static void __exit acpi_battery_exit(void)
{
	acpi_bus_unregister_driver(&acpi_battery_driver);
	acpi_unlock_battery_dir(acpi_battery_dir);
}

module_init(acpi_battery_init);
module_exit(acpi_battery_exit);

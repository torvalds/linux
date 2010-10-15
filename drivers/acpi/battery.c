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
#include <linux/async.h>
#include <linux/dmi.h>
#include <linux/slab.h>

#ifdef CONFIG_ACPI_PROCFS_POWER
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#endif

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#ifdef CONFIG_ACPI_SYSFS_POWER
#include <linux/power_supply.h>
#endif

#define PREFIX "ACPI: "

#define ACPI_BATTERY_VALUE_UNKNOWN 0xFFFFFFFF

#define ACPI_BATTERY_CLASS		"battery"
#define ACPI_BATTERY_DEVICE_NAME	"Battery"
#define ACPI_BATTERY_NOTIFY_STATUS	0x80
#define ACPI_BATTERY_NOTIFY_INFO	0x81
#define ACPI_BATTERY_NOTIFY_THRESHOLD   0x82

#define _COMPONENT		ACPI_BATTERY_COMPONENT

ACPI_MODULE_NAME("battery");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_AUTHOR("Alexey Starikovskiy <astarikovskiy@suse.de>");
MODULE_DESCRIPTION("ACPI Battery Driver");
MODULE_LICENSE("GPL");

static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

#ifdef CONFIG_ACPI_PROCFS_POWER
extern struct proc_dir_entry *acpi_lock_battery_dir(void);
extern void *acpi_unlock_battery_dir(struct proc_dir_entry *acpi_battery_dir);

enum acpi_battery_files {
	info_tag = 0,
	state_tag,
	alarm_tag,
	ACPI_BATTERY_NUMFILES,
};

#endif

static const struct acpi_device_id battery_device_ids[] = {
	{"PNP0C0A", 0},
	{"", 0},
};

MODULE_DEVICE_TABLE(acpi, battery_device_ids);

enum {
	ACPI_BATTERY_ALARM_PRESENT,
	ACPI_BATTERY_XINFO_PRESENT,
	/* For buggy DSDTs that report negative 16-bit values for either
	 * charging or discharging current and/or report 0 as 65536
	 * due to bad math.
	 */
	ACPI_BATTERY_QUIRK_SIGNED16_CURRENT,
};

struct acpi_battery {
	struct mutex lock;
#ifdef CONFIG_ACPI_SYSFS_POWER
	struct power_supply bat;
#endif
	struct acpi_device *device;
	unsigned long update_time;
	int rate_now;
	int capacity_now;
	int voltage_now;
	int design_capacity;
	int full_charge_capacity;
	int technology;
	int design_voltage;
	int design_capacity_warning;
	int design_capacity_low;
	int cycle_count;
	int measurement_accuracy;
	int max_sampling_time;
	int min_sampling_time;
	int max_averaging_interval;
	int min_averaging_interval;
	int capacity_granularity_1;
	int capacity_granularity_2;
	int alarm;
	char model_number[32];
	char serial_number[32];
	char type[32];
	char oem_info[32];
	int state;
	int power_unit;
	unsigned long flags;
};

#define to_acpi_battery(x) container_of(x, struct acpi_battery, bat);

inline int acpi_battery_present(struct acpi_battery *battery)
{
	return battery->device->status.battery_present;
}

#ifdef CONFIG_ACPI_SYSFS_POWER
static int acpi_battery_technology(struct acpi_battery *battery)
{
	if (!strcasecmp("NiCd", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_NiCd;
	if (!strcasecmp("NiMH", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_NiMH;
	if (!strcasecmp("LION", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_LION;
	if (!strncasecmp("LI-ION", battery->type, 6))
		return POWER_SUPPLY_TECHNOLOGY_LION;
	if (!strcasecmp("LiP", battery->type))
		return POWER_SUPPLY_TECHNOLOGY_LIPO;
	return POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
}

static int acpi_battery_get_state(struct acpi_battery *battery);

static int acpi_battery_is_charged(struct acpi_battery *battery)
{
	/* either charging or discharging */
	if (battery->state != 0)
		return 0;

	/* battery not reporting charge */
	if (battery->capacity_now == ACPI_BATTERY_VALUE_UNKNOWN ||
	    battery->capacity_now == 0)
		return 0;

	/* good batteries update full_charge as the batteries degrade */
	if (battery->full_charge_capacity == battery->capacity_now)
		return 1;

	/* fallback to using design values for broken batteries */
	if (battery->design_capacity == battery->capacity_now)
		return 1;

	/* we don't do any sort of metric based on percentages */
	return 0;
}

static int acpi_battery_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct acpi_battery *battery = to_acpi_battery(psy);

	if (acpi_battery_present(battery)) {
		/* run battery update only if it is present */
		acpi_battery_get_state(battery);
	} else if (psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (battery->state & 0x01)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (battery->state & 0x02)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (acpi_battery_is_charged(battery))
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = acpi_battery_present(battery);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = acpi_battery_technology(battery);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = battery->cycle_count;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = battery->design_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battery->voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_POWER_NOW:
		val->intval = battery->rate_now * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = battery->design_capacity * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = battery->full_charge_capacity * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		val->intval = battery->capacity_now * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = battery->model_number;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = battery->oem_info;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = battery->serial_number;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property charge_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

static enum power_supply_property energy_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};
#endif

#ifdef CONFIG_ACPI_PROCFS_POWER
inline char *acpi_battery_units(struct acpi_battery *battery)
{
	return (battery->power_unit)?"mA":"mW";
}
#endif

/* --------------------------------------------------------------------------
                               Battery Management
   -------------------------------------------------------------------------- */
struct acpi_offsets {
	size_t offset;		/* offset inside struct acpi_sbs_battery */
	u8 mode;		/* int or string? */
};

static struct acpi_offsets state_offsets[] = {
	{offsetof(struct acpi_battery, state), 0},
	{offsetof(struct acpi_battery, rate_now), 0},
	{offsetof(struct acpi_battery, capacity_now), 0},
	{offsetof(struct acpi_battery, voltage_now), 0},
};

static struct acpi_offsets info_offsets[] = {
	{offsetof(struct acpi_battery, power_unit), 0},
	{offsetof(struct acpi_battery, design_capacity), 0},
	{offsetof(struct acpi_battery, full_charge_capacity), 0},
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

static struct acpi_offsets extended_info_offsets[] = {
	{offsetof(struct acpi_battery, power_unit), 0},
	{offsetof(struct acpi_battery, design_capacity), 0},
	{offsetof(struct acpi_battery, full_charge_capacity), 0},
	{offsetof(struct acpi_battery, technology), 0},
	{offsetof(struct acpi_battery, design_voltage), 0},
	{offsetof(struct acpi_battery, design_capacity_warning), 0},
	{offsetof(struct acpi_battery, design_capacity_low), 0},
	{offsetof(struct acpi_battery, cycle_count), 0},
	{offsetof(struct acpi_battery, measurement_accuracy), 0},
	{offsetof(struct acpi_battery, max_sampling_time), 0},
	{offsetof(struct acpi_battery, min_sampling_time), 0},
	{offsetof(struct acpi_battery, max_averaging_interval), 0},
	{offsetof(struct acpi_battery, min_averaging_interval), 0},
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
	int i;
	union acpi_object *element;
	if (package->type != ACPI_TYPE_PACKAGE)
		return -EFAULT;
	for (i = 0; i < num; ++i) {
		if (package->package.count <= i)
			return -EFAULT;
		element = &package->package.elements[i];
		if (offsets[i].mode) {
			u8 *ptr = (u8 *)battery + offsets[i].offset;
			if (element->type == ACPI_TYPE_STRING ||
			    element->type == ACPI_TYPE_BUFFER)
				strncpy(ptr, element->string.pointer, 32);
			else if (element->type == ACPI_TYPE_INTEGER) {
				strncpy(ptr, (u8 *)&element->integer.value,
					sizeof(u64));
				ptr[sizeof(u64)] = 0;
			} else
				*ptr = 0; /* don't have value */
		} else {
			int *x = (int *)((u8 *)battery + offsets[i].offset);
			*x = (element->type == ACPI_TYPE_INTEGER) ?
				element->integer.value : -1;
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
	char *name = test_bit(ACPI_BATTERY_XINFO_PRESENT, &battery->flags)?
			"_BIX" : "_BIF";

	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	if (!acpi_battery_present(battery))
		return 0;
	mutex_lock(&battery->lock);
	status = acpi_evaluate_object(battery->device->handle, name,
						NULL, &buffer);
	mutex_unlock(&battery->lock);

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating %s", name));
		return -ENODEV;
	}
	if (test_bit(ACPI_BATTERY_XINFO_PRESENT, &battery->flags))
		result = extract_package(battery, buffer.pointer,
				extended_info_offsets,
				ARRAY_SIZE(extended_info_offsets));
	else
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

	if (test_bit(ACPI_BATTERY_QUIRK_SIGNED16_CURRENT, &battery->flags) &&
	    battery->rate_now != -1)
		battery->rate_now = abs((s16)battery->rate_now);

	return result;
}

static int acpi_battery_set_alarm(struct acpi_battery *battery)
{
	acpi_status status = 0;
	union acpi_object arg0 = { .type = ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };

	if (!acpi_battery_present(battery) ||
	    !test_bit(ACPI_BATTERY_ALARM_PRESENT, &battery->flags))
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
		clear_bit(ACPI_BATTERY_ALARM_PRESENT, &battery->flags);
		return 0;
	}
	set_bit(ACPI_BATTERY_ALARM_PRESENT, &battery->flags);
	if (!battery->alarm)
		battery->alarm = battery->design_capacity_warning;
	return acpi_battery_set_alarm(battery);
}

#ifdef CONFIG_ACPI_SYSFS_POWER
static ssize_t acpi_battery_alarm_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct acpi_battery *battery = to_acpi_battery(dev_get_drvdata(dev));
	return sprintf(buf, "%d\n", battery->alarm * 1000);
}

static ssize_t acpi_battery_alarm_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long x;
	struct acpi_battery *battery = to_acpi_battery(dev_get_drvdata(dev));
	if (sscanf(buf, "%ld\n", &x) == 1)
		battery->alarm = x/1000;
	if (acpi_battery_present(battery))
		acpi_battery_set_alarm(battery);
	return count;
}

static struct device_attribute alarm_attr = {
	.attr = {.name = "alarm", .mode = 0644},
	.show = acpi_battery_alarm_show,
	.store = acpi_battery_alarm_store,
};

static int sysfs_add_battery(struct acpi_battery *battery)
{
	int result;

	if (battery->power_unit) {
		battery->bat.properties = charge_battery_props;
		battery->bat.num_properties =
			ARRAY_SIZE(charge_battery_props);
	} else {
		battery->bat.properties = energy_battery_props;
		battery->bat.num_properties =
			ARRAY_SIZE(energy_battery_props);
	}

	battery->bat.name = acpi_device_bid(battery->device);
	battery->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->bat.get_property = acpi_battery_get_property;

	result = power_supply_register(&battery->device->dev, &battery->bat);
	if (result)
		return result;
	return device_create_file(battery->bat.dev, &alarm_attr);
}

static void sysfs_remove_battery(struct acpi_battery *battery)
{
	if (!battery->bat.dev)
		return;
	device_remove_file(battery->bat.dev, &alarm_attr);
	power_supply_unregister(&battery->bat);
	battery->bat.dev = NULL;
}
#endif

static void acpi_battery_quirks(struct acpi_battery *battery)
{
	if (dmi_name_in_vendors("Acer") && battery->power_unit) {
		set_bit(ACPI_BATTERY_QUIRK_SIGNED16_CURRENT, &battery->flags);
	}
}

static int acpi_battery_update(struct acpi_battery *battery)
{
	int result, old_present = acpi_battery_present(battery);
	result = acpi_battery_get_status(battery);
	if (result)
		return result;
	if (!acpi_battery_present(battery)) {
#ifdef CONFIG_ACPI_SYSFS_POWER
		sysfs_remove_battery(battery);
#endif
		battery->update_time = 0;
		return 0;
	}
	if (!battery->update_time ||
	    old_present != acpi_battery_present(battery)) {
		result = acpi_battery_get_info(battery);
		if (result)
			return result;
		acpi_battery_quirks(battery);
		acpi_battery_init_alarm(battery);
	}
#ifdef CONFIG_ACPI_SYSFS_POWER
	if (!battery->bat.dev)
		sysfs_add_battery(battery);
#endif
	return acpi_battery_get_state(battery);
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_PROCFS_POWER
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

	if (battery->full_charge_capacity == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "last full capacity:      unknown\n");
	else
		seq_printf(seq, "last full capacity:      %d %sh\n",
			   battery->full_charge_capacity,
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
	seq_printf(seq, "cycle count:		  %i\n", battery->cycle_count);
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

	if (battery->rate_now == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present rate:            unknown\n");
	else
		seq_printf(seq, "present rate:            %d %s\n",
			   battery->rate_now, acpi_battery_units(battery));

	if (battery->capacity_now == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "remaining capacity:      unknown\n");
	else
		seq_printf(seq, "remaining capacity:      %d %sh\n",
			   battery->capacity_now, acpi_battery_units(battery));
	if (battery->voltage_now == ACPI_BATTERY_VALUE_UNKNOWN)
		seq_printf(seq, "present voltage:         unknown\n");
	else
		seq_printf(seq, "present voltage:         %d mV\n",
			   battery->voltage_now);
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
	const char *name;
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
	}

	for (i = 0; i < ACPI_BATTERY_NUMFILES; ++i) {
		entry = proc_create_data(acpi_battery_file[i].name,
					 acpi_battery_file[i].mode,
					 acpi_device_dir(device),
					 &acpi_battery_file[i].ops,
					 acpi_driver_data(device));
		if (!entry)
			return -ENODEV;
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

#endif

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_battery_notify(struct acpi_device *device, u32 event)
{
	struct acpi_battery *battery = acpi_driver_data(device);
#ifdef CONFIG_ACPI_SYSFS_POWER
	struct device *old;
#endif

	if (!battery)
		return;
#ifdef CONFIG_ACPI_SYSFS_POWER
	old = battery->bat.dev;
#endif
	acpi_battery_update(battery);
	acpi_bus_generate_proc_event(device, event,
				     acpi_battery_present(battery));
	acpi_bus_generate_netlink_event(device->pnp.device_class,
					dev_name(&device->dev), event,
					acpi_battery_present(battery));
#ifdef CONFIG_ACPI_SYSFS_POWER
	/* acpi_battery_update could remove power_supply object */
	if (old && battery->bat.dev)
		power_supply_changed(&battery->bat);
#endif
}

static int acpi_battery_add(struct acpi_device *device)
{
	int result = 0;
	struct acpi_battery *battery = NULL;
	acpi_handle handle;
	if (!device)
		return -EINVAL;
	battery = kzalloc(sizeof(struct acpi_battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;
	battery->device = device;
	strcpy(acpi_device_name(device), ACPI_BATTERY_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_BATTERY_CLASS);
	device->driver_data = battery;
	mutex_init(&battery->lock);
	if (ACPI_SUCCESS(acpi_get_handle(battery->device->handle,
			"_BIX", &handle)))
		set_bit(ACPI_BATTERY_XINFO_PRESENT, &battery->flags);
	acpi_battery_update(battery);
#ifdef CONFIG_ACPI_PROCFS_POWER
	result = acpi_battery_add_fs(device);
#endif
	if (!result) {
		printk(KERN_INFO PREFIX "%s Slot [%s] (battery %s)\n",
			ACPI_BATTERY_DEVICE_NAME, acpi_device_bid(device),
			device->status.battery_present ? "present" : "absent");
	} else {
#ifdef CONFIG_ACPI_PROCFS_POWER
		acpi_battery_remove_fs(device);
#endif
		kfree(battery);
	}
	return result;
}

static int acpi_battery_remove(struct acpi_device *device, int type)
{
	struct acpi_battery *battery = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;
	battery = acpi_driver_data(device);
#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_battery_remove_fs(device);
#endif
#ifdef CONFIG_ACPI_SYSFS_POWER
	sysfs_remove_battery(battery);
#endif
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
	acpi_battery_update(battery);
	return 0;
}

static struct acpi_driver acpi_battery_driver = {
	.name = "battery",
	.class = ACPI_BATTERY_CLASS,
	.ids = battery_device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add = acpi_battery_add,
		.resume = acpi_battery_resume,
		.remove = acpi_battery_remove,
		.notify = acpi_battery_notify,
		},
};

static void __init acpi_battery_init_async(void *unused, async_cookie_t cookie)
{
	if (acpi_disabled)
		return;
#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_battery_dir = acpi_lock_battery_dir();
	if (!acpi_battery_dir)
		return;
#endif
	if (acpi_bus_register_driver(&acpi_battery_driver) < 0) {
#ifdef CONFIG_ACPI_PROCFS_POWER
		acpi_unlock_battery_dir(acpi_battery_dir);
#endif
		return;
	}
	return;
}

static int __init acpi_battery_init(void)
{
	async_schedule(acpi_battery_init_async, NULL);
	return 0;
}

static void __exit acpi_battery_exit(void)
{
	acpi_bus_unregister_driver(&acpi_battery_driver);
#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_unlock_battery_dir(acpi_battery_dir);
#endif
}

module_init(acpi_battery_init);
module_exit(acpi_battery_exit);

/*
 *  sbs.c - ACPI Smart Battery System Driver ($Revision: 2.0 $)
 *
 *  Copyright (c) 2007 Alexey Starikovskiy <astarikovskiy@suse.de>
 *  Copyright (c) 2005-2007 Vladimir Lebedev <vladimir.p.lebedev@intel.com>
 *  Copyright (c) 2005 Rich Townsend <rhdt@bartol.udel.edu>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

#ifdef CONFIG_ACPI_PROCFS_POWER
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#endif

#include <linux/acpi.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#ifdef CONFIG_ACPI_SYSFS_POWER
#include <linux/power_supply.h>
#endif

#include "sbshc.h"

#define PREFIX "ACPI: "

#define ACPI_SBS_CLASS			"sbs"
#define ACPI_AC_CLASS			"ac_adapter"
#define ACPI_BATTERY_CLASS		"battery"
#define ACPI_SBS_DEVICE_NAME		"Smart Battery System"
#define ACPI_SBS_FILE_INFO		"info"
#define ACPI_SBS_FILE_STATE		"state"
#define ACPI_SBS_FILE_ALARM		"alarm"
#define ACPI_BATTERY_DIR_NAME		"BAT%i"
#define ACPI_AC_DIR_NAME		"AC0"

#define ACPI_SBS_NOTIFY_STATUS		0x80
#define ACPI_SBS_NOTIFY_INFO		0x81

MODULE_AUTHOR("Alexey Starikovskiy <astarikovskiy@suse.de>");
MODULE_DESCRIPTION("Smart Battery System ACPI interface driver");
MODULE_LICENSE("GPL");

static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

extern struct proc_dir_entry *acpi_lock_ac_dir(void);
extern struct proc_dir_entry *acpi_lock_battery_dir(void);
extern void acpi_unlock_ac_dir(struct proc_dir_entry *acpi_ac_dir);
extern void acpi_unlock_battery_dir(struct proc_dir_entry *acpi_battery_dir);

#define MAX_SBS_BAT			4
#define ACPI_SBS_BLOCK_MAX		32

static const struct acpi_device_id sbs_device_ids[] = {
	{"ACPI0002", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, sbs_device_ids);

struct acpi_battery {
#ifdef CONFIG_ACPI_SYSFS_POWER
	struct power_supply bat;
#endif
	struct acpi_sbs *sbs;
#ifdef CONFIG_ACPI_PROCFS_POWER
	struct proc_dir_entry *proc_entry;
#endif
	unsigned long update_time;
	char name[8];
	char manufacturer_name[ACPI_SBS_BLOCK_MAX];
	char device_name[ACPI_SBS_BLOCK_MAX];
	char device_chemistry[ACPI_SBS_BLOCK_MAX];
	u16 alarm_capacity;
	u16 full_charge_capacity;
	u16 design_capacity;
	u16 design_voltage;
	u16 serial_number;
	u16 cycle_count;
	u16 temp_now;
	u16 voltage_now;
	s16 rate_now;
	s16 rate_avg;
	u16 capacity_now;
	u16 state_of_charge;
	u16 state;
	u16 mode;
	u16 spec;
	u8 id;
	u8 present:1;
	u8 have_sysfs_alarm:1;
};

#define to_acpi_battery(x) container_of(x, struct acpi_battery, bat);

struct acpi_sbs {
#ifdef CONFIG_ACPI_SYSFS_POWER
	struct power_supply charger;
#endif
	struct acpi_device *device;
	struct acpi_smb_hc *hc;
	struct mutex lock;
#ifdef CONFIG_ACPI_PROCFS_POWER
	struct proc_dir_entry *charger_entry;
#endif
	struct acpi_battery battery[MAX_SBS_BAT];
	u8 batteries_supported:4;
	u8 manager_present:1;
	u8 charger_present:1;
};

#define to_acpi_sbs(x) container_of(x, struct acpi_sbs, charger)

static inline int battery_scale(int log)
{
	int scale = 1;
	while (log--)
		scale *= 10;
	return scale;
}

static inline int acpi_battery_vscale(struct acpi_battery *battery)
{
	return battery_scale((battery->spec & 0x0f00) >> 8);
}

static inline int acpi_battery_ipscale(struct acpi_battery *battery)
{
	return battery_scale((battery->spec & 0xf000) >> 12);
}

static inline int acpi_battery_mode(struct acpi_battery *battery)
{
	return (battery->mode & 0x8000);
}

static inline int acpi_battery_scale(struct acpi_battery *battery)
{
	return (acpi_battery_mode(battery) ? 10 : 1) *
	    acpi_battery_ipscale(battery);
}

#ifdef CONFIG_ACPI_SYSFS_POWER
static int sbs_get_ac_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct acpi_sbs *sbs = to_acpi_sbs(psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = sbs->charger_present;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int acpi_battery_technology(struct acpi_battery *battery)
{
	if (!strcasecmp("NiCd", battery->device_chemistry))
		return POWER_SUPPLY_TECHNOLOGY_NiCd;
	if (!strcasecmp("NiMH", battery->device_chemistry))
		return POWER_SUPPLY_TECHNOLOGY_NiMH;
	if (!strcasecmp("LION", battery->device_chemistry))
		return POWER_SUPPLY_TECHNOLOGY_LION;
	if (!strcasecmp("LiP", battery->device_chemistry))
		return POWER_SUPPLY_TECHNOLOGY_LIPO;
	return POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
}

static int acpi_sbs_battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct acpi_battery *battery = to_acpi_battery(psy);

	if ((!battery->present) && psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (battery->rate_now < 0)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (battery->rate_now > 0)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_FULL;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery->present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = acpi_battery_technology(battery);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = battery->design_voltage *
			acpi_battery_vscale(battery) * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battery->voltage_now *
				acpi_battery_vscale(battery) * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_POWER_NOW:
		val->intval = abs(battery->rate_now) *
				acpi_battery_ipscale(battery) * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_POWER_AVG:
		val->intval = abs(battery->rate_avg) *
				acpi_battery_ipscale(battery) * 1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = battery->state_of_charge;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = battery->design_capacity *
			acpi_battery_scale(battery) * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = battery->full_charge_capacity *
			acpi_battery_scale(battery) * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		val->intval = battery->capacity_now *
				acpi_battery_scale(battery) * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery->temp_now - 2730;	// dK -> dC
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = battery->device_name;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = battery->manufacturer_name;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property sbs_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property sbs_charge_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property sbs_energy_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

#endif

/* --------------------------------------------------------------------------
                            Smart Battery System Management
   -------------------------------------------------------------------------- */

struct acpi_battery_reader {
	u8 command;		/* command for battery */
	u8 mode;		/* word or block? */
	size_t offset;		/* offset inside struct acpi_sbs_battery */
};

static struct acpi_battery_reader info_readers[] = {
	{0x01, SMBUS_READ_WORD, offsetof(struct acpi_battery, alarm_capacity)},
	{0x03, SMBUS_READ_WORD, offsetof(struct acpi_battery, mode)},
	{0x10, SMBUS_READ_WORD, offsetof(struct acpi_battery, full_charge_capacity)},
	{0x17, SMBUS_READ_WORD, offsetof(struct acpi_battery, cycle_count)},
	{0x18, SMBUS_READ_WORD, offsetof(struct acpi_battery, design_capacity)},
	{0x19, SMBUS_READ_WORD, offsetof(struct acpi_battery, design_voltage)},
	{0x1a, SMBUS_READ_WORD, offsetof(struct acpi_battery, spec)},
	{0x1c, SMBUS_READ_WORD, offsetof(struct acpi_battery, serial_number)},
	{0x20, SMBUS_READ_BLOCK, offsetof(struct acpi_battery, manufacturer_name)},
	{0x21, SMBUS_READ_BLOCK, offsetof(struct acpi_battery, device_name)},
	{0x22, SMBUS_READ_BLOCK, offsetof(struct acpi_battery, device_chemistry)},
};

static struct acpi_battery_reader state_readers[] = {
	{0x08, SMBUS_READ_WORD, offsetof(struct acpi_battery, temp_now)},
	{0x09, SMBUS_READ_WORD, offsetof(struct acpi_battery, voltage_now)},
	{0x0a, SMBUS_READ_WORD, offsetof(struct acpi_battery, rate_now)},
	{0x0b, SMBUS_READ_WORD, offsetof(struct acpi_battery, rate_avg)},
	{0x0f, SMBUS_READ_WORD, offsetof(struct acpi_battery, capacity_now)},
	{0x0e, SMBUS_READ_WORD, offsetof(struct acpi_battery, state_of_charge)},
	{0x16, SMBUS_READ_WORD, offsetof(struct acpi_battery, state)},
};

static int acpi_manager_get_info(struct acpi_sbs *sbs)
{
	int result = 0;
	u16 battery_system_info;

	result = acpi_smbus_read(sbs->hc, SMBUS_READ_WORD, ACPI_SBS_MANAGER,
				 0x04, (u8 *)&battery_system_info);
	if (!result)
		sbs->batteries_supported = battery_system_info & 0x000f;
	return result;
}

static int acpi_battery_get_info(struct acpi_battery *battery)
{
	int i, result = 0;

	for (i = 0; i < ARRAY_SIZE(info_readers); ++i) {
		result = acpi_smbus_read(battery->sbs->hc,
					 info_readers[i].mode,
					 ACPI_SBS_BATTERY,
					 info_readers[i].command,
					 (u8 *) battery +
						info_readers[i].offset);
		if (result)
			break;
	}
	return result;
}

static int acpi_battery_get_state(struct acpi_battery *battery)
{
	int i, result = 0;

	if (battery->update_time &&
	    time_before(jiffies, battery->update_time +
				msecs_to_jiffies(cache_time)))
		return 0;
	for (i = 0; i < ARRAY_SIZE(state_readers); ++i) {
		result = acpi_smbus_read(battery->sbs->hc,
					 state_readers[i].mode,
					 ACPI_SBS_BATTERY,
					 state_readers[i].command,
				         (u8 *)battery +
						state_readers[i].offset);
		if (result)
			goto end;
	}
      end:
	battery->update_time = jiffies;
	return result;
}

static int acpi_battery_get_alarm(struct acpi_battery *battery)
{
	return acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD,
				 ACPI_SBS_BATTERY, 0x01,
				 (u8 *)&battery->alarm_capacity);
}

static int acpi_battery_set_alarm(struct acpi_battery *battery)
{
	struct acpi_sbs *sbs = battery->sbs;
	u16 value, sel = 1 << (battery->id + 12);

	int ret;


	if (sbs->manager_present) {
		ret = acpi_smbus_read(sbs->hc, SMBUS_READ_WORD, ACPI_SBS_MANAGER,
				0x01, (u8 *)&value);
		if (ret)
			goto end;
		if ((value & 0xf000) != sel) {
			value &= 0x0fff;
			value |= sel;
		ret = acpi_smbus_write(sbs->hc, SMBUS_WRITE_WORD,
					 ACPI_SBS_MANAGER,
					 0x01, (u8 *)&value, 2);
		if (ret)
			goto end;
		}
	}
	ret = acpi_smbus_write(sbs->hc, SMBUS_WRITE_WORD, ACPI_SBS_BATTERY,
				0x01, (u8 *)&battery->alarm_capacity, 2);
      end:
	return ret;
}

static int acpi_ac_get_present(struct acpi_sbs *sbs)
{
	int result;
	u16 status;

	result = acpi_smbus_read(sbs->hc, SMBUS_READ_WORD, ACPI_SBS_CHARGER,
				 0x13, (u8 *) & status);
	if (!result)
		sbs->charger_present = (status >> 15) & 0x1;
	return result;
}

#ifdef CONFIG_ACPI_SYSFS_POWER
static ssize_t acpi_battery_alarm_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct acpi_battery *battery = to_acpi_battery(dev_get_drvdata(dev));
	acpi_battery_get_alarm(battery);
	return sprintf(buf, "%d\n", battery->alarm_capacity *
				acpi_battery_scale(battery) * 1000);
}

static ssize_t acpi_battery_alarm_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long x;
	struct acpi_battery *battery = to_acpi_battery(dev_get_drvdata(dev));
	if (sscanf(buf, "%ld\n", &x) == 1)
		battery->alarm_capacity = x /
			(1000 * acpi_battery_scale(battery));
	if (battery->present)
		acpi_battery_set_alarm(battery);
	return count;
}

static struct device_attribute alarm_attr = {
	.attr = {.name = "alarm", .mode = 0644},
	.show = acpi_battery_alarm_show,
	.store = acpi_battery_alarm_store,
};
#endif

/* --------------------------------------------------------------------------
                              FS Interface (/proc/acpi)
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_PROCFS_POWER
/* Generic Routines */
static int
acpi_sbs_add_fs(struct proc_dir_entry **dir,
		struct proc_dir_entry *parent_dir,
		char *dir_name,
		const struct file_operations *info_fops,
		const struct file_operations *state_fops,
		const struct file_operations *alarm_fops, void *data)
{
	if (!*dir) {
		*dir = proc_mkdir(dir_name, parent_dir);
		if (!*dir) {
			return -ENODEV;
		}
	}

	/* 'info' [R] */
	if (info_fops)
		proc_create_data(ACPI_SBS_FILE_INFO, S_IRUGO, *dir,
				 info_fops, data);

	/* 'state' [R] */
	if (state_fops)
		proc_create_data(ACPI_SBS_FILE_STATE, S_IRUGO, *dir,
				 state_fops, data);

	/* 'alarm' [R/W] */
	if (alarm_fops)
		proc_create_data(ACPI_SBS_FILE_ALARM, S_IRUGO, *dir,
				 alarm_fops, data);
	return 0;
}

static void
acpi_sbs_remove_fs(struct proc_dir_entry **dir,
			   struct proc_dir_entry *parent_dir)
{
	if (*dir) {
		remove_proc_entry(ACPI_SBS_FILE_INFO, *dir);
		remove_proc_entry(ACPI_SBS_FILE_STATE, *dir);
		remove_proc_entry(ACPI_SBS_FILE_ALARM, *dir);
		remove_proc_entry((*dir)->name, parent_dir);
		*dir = NULL;
	}
}

/* Smart Battery Interface */
static struct proc_dir_entry *acpi_battery_dir = NULL;

static inline char *acpi_battery_units(struct acpi_battery *battery)
{
	return acpi_battery_mode(battery) ? " mW" : " mA";
}


static int acpi_battery_read_info(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;

	mutex_lock(&sbs->lock);

	seq_printf(seq, "present:                 %s\n",
		   (battery->present) ? "yes" : "no");
	if (!battery->present)
		goto end;

	seq_printf(seq, "design capacity:         %i%sh\n",
		   battery->design_capacity * acpi_battery_scale(battery),
		   acpi_battery_units(battery));
	seq_printf(seq, "last full capacity:      %i%sh\n",
		   battery->full_charge_capacity * acpi_battery_scale(battery),
		   acpi_battery_units(battery));
	seq_printf(seq, "battery technology:      rechargeable\n");
	seq_printf(seq, "design voltage:          %i mV\n",
		   battery->design_voltage * acpi_battery_vscale(battery));
	seq_printf(seq, "design capacity warning: unknown\n");
	seq_printf(seq, "design capacity low:     unknown\n");
	seq_printf(seq, "capacity granularity 1:  unknown\n");
	seq_printf(seq, "capacity granularity 2:  unknown\n");
	seq_printf(seq, "model number:            %s\n", battery->device_name);
	seq_printf(seq, "serial number:           %i\n",
		   battery->serial_number);
	seq_printf(seq, "battery type:            %s\n",
		   battery->device_chemistry);
	seq_printf(seq, "OEM info:                %s\n",
		   battery->manufacturer_name);
      end:
	mutex_unlock(&sbs->lock);
	return result;
}

static int acpi_battery_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_info, PDE(inode)->data);
}

static int acpi_battery_read_state(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	int rate;

	mutex_lock(&sbs->lock);
	seq_printf(seq, "present:                 %s\n",
		   (battery->present) ? "yes" : "no");
	if (!battery->present)
		goto end;

	acpi_battery_get_state(battery);
	seq_printf(seq, "capacity state:          %s\n",
		   (battery->state & 0x0010) ? "critical" : "ok");
	seq_printf(seq, "charging state:          %s\n",
		   (battery->rate_now < 0) ? "discharging" :
		   ((battery->rate_now > 0) ? "charging" : "charged"));
	rate = abs(battery->rate_now) * acpi_battery_ipscale(battery);
	rate *= (acpi_battery_mode(battery))?(battery->voltage_now *
			acpi_battery_vscale(battery)/1000):1;
	seq_printf(seq, "present rate:            %d%s\n", rate,
		   acpi_battery_units(battery));
	seq_printf(seq, "remaining capacity:      %i%sh\n",
		   battery->capacity_now * acpi_battery_scale(battery),
		   acpi_battery_units(battery));
	seq_printf(seq, "present voltage:         %i mV\n",
		   battery->voltage_now * acpi_battery_vscale(battery));

      end:
	mutex_unlock(&sbs->lock);
	return 0;
}

static int acpi_battery_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_state, PDE(inode)->data);
}

static int acpi_battery_read_alarm(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;

	mutex_lock(&sbs->lock);

	if (!battery->present) {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	acpi_battery_get_alarm(battery);
	seq_printf(seq, "alarm:                   ");
	if (battery->alarm_capacity)
		seq_printf(seq, "%i%sh\n",
			   battery->alarm_capacity *
			   acpi_battery_scale(battery),
			   acpi_battery_units(battery));
	else
		seq_printf(seq, "disabled\n");
      end:
	mutex_unlock(&sbs->lock);
	return result;
}

static ssize_t
acpi_battery_write_alarm(struct file *file, const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	struct seq_file *seq = file->private_data;
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	char alarm_string[12] = { '\0' };
	int result = 0;
	mutex_lock(&sbs->lock);
	if (!battery->present) {
		result = -ENODEV;
		goto end;
	}
	if (count > sizeof(alarm_string) - 1) {
		result = -EINVAL;
		goto end;
	}
	if (copy_from_user(alarm_string, buffer, count)) {
		result = -EFAULT;
		goto end;
	}
	alarm_string[count] = 0;
	battery->alarm_capacity = simple_strtoul(alarm_string, NULL, 0) /
					acpi_battery_scale(battery);
	acpi_battery_set_alarm(battery);
      end:
	mutex_unlock(&sbs->lock);
	if (result)
		return result;
	return count;
}

static int acpi_battery_alarm_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_alarm, PDE(inode)->data);
}

static const struct file_operations acpi_battery_info_fops = {
	.open = acpi_battery_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations acpi_battery_state_fops = {
	.open = acpi_battery_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations acpi_battery_alarm_fops = {
	.open = acpi_battery_alarm_open_fs,
	.read = seq_read,
	.write = acpi_battery_write_alarm,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/* Legacy AC Adapter Interface */

static struct proc_dir_entry *acpi_ac_dir = NULL;

static int acpi_ac_read_state(struct seq_file *seq, void *offset)
{

	struct acpi_sbs *sbs = seq->private;

	mutex_lock(&sbs->lock);

	seq_printf(seq, "state:                   %s\n",
		   sbs->charger_present ? "on-line" : "off-line");

	mutex_unlock(&sbs->lock);
	return 0;
}

static int acpi_ac_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_ac_read_state, PDE(inode)->data);
}

static const struct file_operations acpi_ac_state_fops = {
	.open = acpi_ac_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

#endif

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */
static int acpi_battery_read(struct acpi_battery *battery)
{
	int result = 0, saved_present = battery->present;
	u16 state;

	if (battery->sbs->manager_present) {
		result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD,
				ACPI_SBS_MANAGER, 0x01, (u8 *)&state);
		if (!result)
			battery->present = state & (1 << battery->id);
		state &= 0x0fff;
		state |= 1 << (battery->id + 12);
		acpi_smbus_write(battery->sbs->hc, SMBUS_WRITE_WORD,
				  ACPI_SBS_MANAGER, 0x01, (u8 *)&state, 2);
	} else if (battery->id == 0)
		battery->present = 1;
	if (result || !battery->present)
		return result;

	if (saved_present != battery->present) {
		battery->update_time = 0;
		result = acpi_battery_get_info(battery);
		if (result)
			return result;
	}
	result = acpi_battery_get_state(battery);
	return result;
}

/* Smart Battery */
static int acpi_battery_add(struct acpi_sbs *sbs, int id)
{
	struct acpi_battery *battery = &sbs->battery[id];
	int result;

	battery->id = id;
	battery->sbs = sbs;
	result = acpi_battery_read(battery);
	if (result)
		return result;

	sprintf(battery->name, ACPI_BATTERY_DIR_NAME, id);
#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_sbs_add_fs(&battery->proc_entry, acpi_battery_dir,
			battery->name, &acpi_battery_info_fops,
			&acpi_battery_state_fops, &acpi_battery_alarm_fops,
			battery);
#endif
#ifdef CONFIG_ACPI_SYSFS_POWER
	battery->bat.name = battery->name;
	battery->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	if (!acpi_battery_mode(battery)) {
		battery->bat.properties = sbs_charge_battery_props;
		battery->bat.num_properties =
		    ARRAY_SIZE(sbs_charge_battery_props);
	} else {
		battery->bat.properties = sbs_energy_battery_props;
		battery->bat.num_properties =
		    ARRAY_SIZE(sbs_energy_battery_props);
	}
	battery->bat.get_property = acpi_sbs_battery_get_property;
	result = power_supply_register(&sbs->device->dev, &battery->bat);
	if (result)
		goto end;
	result = device_create_file(battery->bat.dev, &alarm_attr);
	if (result)
		goto end;
	battery->have_sysfs_alarm = 1;
      end:
#endif
	printk(KERN_INFO PREFIX "%s [%s]: Battery Slot [%s] (battery %s)\n",
	       ACPI_SBS_DEVICE_NAME, acpi_device_bid(sbs->device),
	       battery->name, battery->present ? "present" : "absent");
	return result;
}

static void acpi_battery_remove(struct acpi_sbs *sbs, int id)
{
#if defined(CONFIG_ACPI_SYSFS_POWER) || defined(CONFIG_ACPI_PROCFS_POWER)
	struct acpi_battery *battery = &sbs->battery[id];
#endif

#ifdef CONFIG_ACPI_SYSFS_POWER
	if (battery->bat.dev) {
		if (battery->have_sysfs_alarm)
			device_remove_file(battery->bat.dev, &alarm_attr);
		power_supply_unregister(&battery->bat);
	}
#endif
#ifdef CONFIG_ACPI_PROCFS_POWER
	if (battery->proc_entry)
		acpi_sbs_remove_fs(&battery->proc_entry, acpi_battery_dir);
#endif
}

static int acpi_charger_add(struct acpi_sbs *sbs)
{
	int result;

	result = acpi_ac_get_present(sbs);
	if (result)
		goto end;
#ifdef CONFIG_ACPI_PROCFS_POWER
	result = acpi_sbs_add_fs(&sbs->charger_entry, acpi_ac_dir,
				 ACPI_AC_DIR_NAME, NULL,
				 &acpi_ac_state_fops, NULL, sbs);
	if (result)
		goto end;
#endif
#ifdef CONFIG_ACPI_SYSFS_POWER
	sbs->charger.name = "sbs-charger";
	sbs->charger.type = POWER_SUPPLY_TYPE_MAINS;
	sbs->charger.properties = sbs_ac_props;
	sbs->charger.num_properties = ARRAY_SIZE(sbs_ac_props);
	sbs->charger.get_property = sbs_get_ac_property;
	power_supply_register(&sbs->device->dev, &sbs->charger);
#endif
	printk(KERN_INFO PREFIX "%s [%s]: AC Adapter [%s] (%s)\n",
	       ACPI_SBS_DEVICE_NAME, acpi_device_bid(sbs->device),
	       ACPI_AC_DIR_NAME, sbs->charger_present ? "on-line" : "off-line");
      end:
	return result;
}

static void acpi_charger_remove(struct acpi_sbs *sbs)
{
#ifdef CONFIG_ACPI_SYSFS_POWER
	if (sbs->charger.dev)
		power_supply_unregister(&sbs->charger);
#endif
#ifdef CONFIG_ACPI_PROCFS_POWER
	if (sbs->charger_entry)
		acpi_sbs_remove_fs(&sbs->charger_entry, acpi_ac_dir);
#endif
}

static void acpi_sbs_callback(void *context)
{
	int id;
	struct acpi_sbs *sbs = context;
	struct acpi_battery *bat;
	u8 saved_charger_state = sbs->charger_present;
	u8 saved_battery_state;
	acpi_ac_get_present(sbs);
	if (sbs->charger_present != saved_charger_state) {
#ifdef CONFIG_ACPI_PROC_EVENT
		acpi_bus_generate_proc_event4(ACPI_AC_CLASS, ACPI_AC_DIR_NAME,
					      ACPI_SBS_NOTIFY_STATUS,
					      sbs->charger_present);
#endif
#ifdef CONFIG_ACPI_SYSFS_POWER
		kobject_uevent(&sbs->charger.dev->kobj, KOBJ_CHANGE);
#endif
	}
	if (sbs->manager_present) {
		for (id = 0; id < MAX_SBS_BAT; ++id) {
			if (!(sbs->batteries_supported & (1 << id)))
				continue;
			bat = &sbs->battery[id];
			saved_battery_state = bat->present;
			acpi_battery_read(bat);
			if (saved_battery_state == bat->present)
				continue;
#ifdef CONFIG_ACPI_PROC_EVENT
			acpi_bus_generate_proc_event4(ACPI_BATTERY_CLASS,
						      bat->name,
						      ACPI_SBS_NOTIFY_STATUS,
						      bat->present);
#endif
#ifdef CONFIG_ACPI_SYSFS_POWER
			kobject_uevent(&bat->bat.dev->kobj, KOBJ_CHANGE);
#endif
		}
	}
}

static int acpi_sbs_remove(struct acpi_device *device, int type);

static int acpi_sbs_add(struct acpi_device *device)
{
	struct acpi_sbs *sbs;
	int result = 0;
	int id;

	sbs = kzalloc(sizeof(struct acpi_sbs), GFP_KERNEL);
	if (!sbs) {
		result = -ENOMEM;
		goto end;
	}

	mutex_init(&sbs->lock);

	sbs->hc = acpi_driver_data(device->parent);
	sbs->device = device;
	strcpy(acpi_device_name(device), ACPI_SBS_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_SBS_CLASS);
	device->driver_data = sbs;

	result = acpi_charger_add(sbs);
	if (result)
		goto end;

	result = acpi_manager_get_info(sbs);
	if (!result) {
		sbs->manager_present = 1;
		for (id = 0; id < MAX_SBS_BAT; ++id)
			if ((sbs->batteries_supported & (1 << id)))
				acpi_battery_add(sbs, id);
	} else
		acpi_battery_add(sbs, 0);
	acpi_smbus_register_callback(sbs->hc, acpi_sbs_callback, sbs);
      end:
	if (result)
		acpi_sbs_remove(device, 0);
	return result;
}

static int acpi_sbs_remove(struct acpi_device *device, int type)
{
	struct acpi_sbs *sbs;
	int id;

	if (!device)
		return -EINVAL;
	sbs = acpi_driver_data(device);
	if (!sbs)
		return -EINVAL;
	mutex_lock(&sbs->lock);
	acpi_smbus_unregister_callback(sbs->hc);
	for (id = 0; id < MAX_SBS_BAT; ++id)
		acpi_battery_remove(sbs, id);
	acpi_charger_remove(sbs);
	mutex_unlock(&sbs->lock);
	mutex_destroy(&sbs->lock);
	kfree(sbs);
	return 0;
}

static void acpi_sbs_rmdirs(void)
{
#ifdef CONFIG_ACPI_PROCFS_POWER
	if (acpi_ac_dir) {
		acpi_unlock_ac_dir(acpi_ac_dir);
		acpi_ac_dir = NULL;
	}
	if (acpi_battery_dir) {
		acpi_unlock_battery_dir(acpi_battery_dir);
		acpi_battery_dir = NULL;
	}
#endif
}

static int acpi_sbs_resume(struct acpi_device *device)
{
	struct acpi_sbs *sbs;
	if (!device)
		return -EINVAL;
	sbs = device->driver_data;
	acpi_sbs_callback(sbs);
	return 0;
}

static struct acpi_driver acpi_sbs_driver = {
	.name = "sbs",
	.class = ACPI_SBS_CLASS,
	.ids = sbs_device_ids,
	.ops = {
		.add = acpi_sbs_add,
		.remove = acpi_sbs_remove,
		.resume = acpi_sbs_resume,
		},
};

static int __init acpi_sbs_init(void)
{
	int result = 0;

	if (acpi_disabled)
		return -ENODEV;
#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_ac_dir = acpi_lock_ac_dir();
	if (!acpi_ac_dir)
		return -ENODEV;
	acpi_battery_dir = acpi_lock_battery_dir();
	if (!acpi_battery_dir) {
		acpi_sbs_rmdirs();
		return -ENODEV;
	}
#endif
	result = acpi_bus_register_driver(&acpi_sbs_driver);
	if (result < 0) {
		acpi_sbs_rmdirs();
		return -ENODEV;
	}
	return 0;
}

static void __exit acpi_sbs_exit(void)
{
	acpi_bus_unregister_driver(&acpi_sbs_driver);
	acpi_sbs_rmdirs();
	return;
}

module_init(acpi_sbs_init);
module_exit(acpi_sbs_exit);

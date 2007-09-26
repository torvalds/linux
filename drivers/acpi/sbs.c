/*
 *  acpi_sbs.c - ACPI Smart Battery System Driver ($Revision: 1.16 $)
 *
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
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/acpi.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#include "sbshc.h"

#define ACPI_SBS_COMPONENT		0x00080000
#define ACPI_SBS_CLASS			"sbs"
#define ACPI_AC_CLASS			"ac_adapter"
#define ACPI_BATTERY_CLASS		"battery"
#define ACPI_SBS_DEVICE_NAME		"Smart Battery System"
#define ACPI_SBS_FILE_INFO		"info"
#define ACPI_SBS_FILE_STATE		"state"
#define ACPI_SBS_FILE_ALARM		"alarm"
#define ACPI_BATTERY_DIR_NAME		"BAT%i"
#define ACPI_AC_DIR_NAME		"AC0"
#define ACPI_SBC_SMBUS_ADDR		0x9
#define ACPI_SBSM_SMBUS_ADDR		0xa
#define ACPI_SB_SMBUS_ADDR		0xb
#define ACPI_SBS_AC_NOTIFY_STATUS	0x80
#define ACPI_SBS_BATTERY_NOTIFY_STATUS	0x80
#define ACPI_SBS_BATTERY_NOTIFY_INFO	0x81

#define _COMPONENT			ACPI_SBS_COMPONENT

ACPI_MODULE_NAME("sbs");

MODULE_AUTHOR("Rich Townsend");
MODULE_DESCRIPTION("Smart Battery System ACPI interface driver");
MODULE_LICENSE("GPL");

#define	DEF_CAPACITY_UNIT	3
#define	MAH_CAPACITY_UNIT	1
#define	MWH_CAPACITY_UNIT	2
#define	CAPACITY_UNIT		DEF_CAPACITY_UNIT

#define	REQUEST_UPDATE_MODE	1
#define	QUEUE_UPDATE_MODE	2

#define	DATA_TYPE_COMMON	0
#define	DATA_TYPE_INFO		1
#define	DATA_TYPE_STATE		2
#define	DATA_TYPE_ALARM		3
#define	DATA_TYPE_AC_STATE	4

extern struct proc_dir_entry *acpi_lock_ac_dir(void);
extern struct proc_dir_entry *acpi_lock_battery_dir(void);
extern void acpi_unlock_ac_dir(struct proc_dir_entry *acpi_ac_dir);
extern void acpi_unlock_battery_dir(struct proc_dir_entry *acpi_battery_dir);

#define	MAX_SBS_BAT			4
#define ACPI_SBS_BLOCK_MAX		32

#define	UPDATE_DELAY	10

/* 0 - every time, > 0 - by update_time */
static unsigned int update_time = 120;

static unsigned int mode = CAPACITY_UNIT;

module_param(update_time, uint, 0644);
module_param(mode, uint, 0444);

static int acpi_sbs_add(struct acpi_device *device);
static int acpi_sbs_remove(struct acpi_device *device, int type);
static int acpi_sbs_resume(struct acpi_device *device);

static const struct acpi_device_id sbs_device_ids[] = {
	{"ACPI0002", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, sbs_device_ids);

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

struct acpi_battery {
	struct acpi_sbs *sbs;
	struct proc_dir_entry *proc_entry;
	int vscale;
	int ipscale;
	char manufacturer_name[ACPI_SBS_BLOCK_MAX];
	char device_name[ACPI_SBS_BLOCK_MAX];
	char device_chemistry[ACPI_SBS_BLOCK_MAX];
	u16 full_charge_capacity;
	u16 design_capacity;
	u16 design_voltage;
	u16 serial_number;
	u16 voltage_now;
	s16 current_now;
	u16 capacity_now;
	u16 state;
	u16 alarm_capacity;
	u16 mode;
	u8 id;
	u8 alive:1;
	u8 init_state:1;
	u8 present:1;
};

struct acpi_sbs {
	struct acpi_device *device;
	struct acpi_smb_hc *hc;
	struct mutex mutex;
	struct proc_dir_entry *ac_entry;
	struct acpi_battery battery[MAX_SBS_BAT];
	int zombie;
	struct timer_list update_timer;
	int run_cnt;
	int update_proc_flg;
	u8 batteries_supported;
	u8 manager_present:1;
	u8 charger_present:1;
};

static int acpi_sbs_update_run(struct acpi_sbs *sbs, int id, int data_type);
static void acpi_sbs_update_time(void *data);

static int sbs_zombie(struct acpi_sbs *sbs)
{
	return (sbs->zombie);
}

static int sbs_mutex_lock(struct acpi_sbs *sbs)
{
	if (sbs_zombie(sbs)) {
		return -ENODEV;
	}
	mutex_lock(&sbs->mutex);
	return 0;
}

static void sbs_mutex_unlock(struct acpi_sbs *sbs)
{
	mutex_unlock(&sbs->mutex);
}

/* --------------------------------------------------------------------------
                            Smart Battery System Management
   -------------------------------------------------------------------------- */

static int acpi_check_update_proc(struct acpi_sbs *sbs)
{
	acpi_status status = AE_OK;

	if (update_time == 0) {
		sbs->update_proc_flg = 0;
		return 0;
	}
	if (sbs->update_proc_flg == 0) {
		status = acpi_os_execute(OSL_GPE_HANDLER,
					 acpi_sbs_update_time, sbs);
		if (status != AE_OK) {
			ACPI_EXCEPTION((AE_INFO, status,
					"acpi_os_execute() failed"));
			return 1;
		}
		sbs->update_proc_flg = 1;
	}
	return 0;
}

static int acpi_battery_get_present(struct acpi_battery *battery)
{
	s16 state;
	int result = 0;
	int is_present = 0;

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD,
				    ACPI_SBSM_SMBUS_ADDR, 0x01, (u8 *)&state);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
	}
	if (!result) {
		is_present = (state & 0x000f) & (1 << battery->id);
	}
	battery->present = is_present;

	return result;
}

static int acpi_battery_select(struct acpi_battery *battery)
{
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;
	s16 state;
	int foo;

	if (sbs->manager_present) {

		/* Take special care not to knobble other nibbles of
		 * state (aka selector_state), since
		 * it causes charging to halt on SBSELs */

		result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD,
					 ACPI_SBSM_SMBUS_ADDR, 0x01, (u8 *)&state);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_smbus_read() failed"));
			goto end;
		}

		foo = (state & 0x0fff) | (1 << (battery->id + 12));
		result = acpi_smbus_write(battery->sbs->hc, SMBUS_WRITE_WORD,
					  ACPI_SBSM_SMBUS_ADDR, 0x01, (u8 *)&foo, 2);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_smbus_write() failed"));
			goto end;
		}
	}

      end:
	return result;
}

static int acpi_sbsm_get_info(struct acpi_sbs *sbs)
{
	int result = 0;
	s16 battery_system_info;

	result = acpi_smbus_read(sbs->hc, SMBUS_READ_WORD, ACPI_SBSM_SMBUS_ADDR, 0x04,
				    (u8 *)&battery_system_info);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}
	sbs->manager_present = 1;

      end:

	return result;
}

static int acpi_battery_get_info(struct acpi_battery *battery)
{
	int result = 0;
	s16 battery_mode;
	s16 specification_info;

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x03,
				    (u8 *)&battery_mode);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}
	battery->mode = (battery_mode & 0x8000) >> 15;

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x10,
				    (u8 *)&battery->full_charge_capacity);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x18,
				    (u8 *)&battery->design_capacity);

	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x19,
				    (u8 *)&battery->design_voltage);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x1a,
				    (u8 *)&specification_info);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	switch ((specification_info & 0x0f00) >> 8) {
	case 1:
		battery->vscale = 10;
		break;
	case 2:
		battery->vscale = 100;
		break;
	case 3:
		battery->vscale = 1000;
		break;
	default:
		battery->vscale = 1;
	}

	switch ((specification_info & 0xf000) >> 12) {
	case 1:
		battery->ipscale = 10;
		break;
	case 2:
		battery->ipscale = 100;
		break;
	case 3:
		battery->ipscale = 1000;
		break;
	default:
		battery->ipscale = 1;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x1c,
				    (u8 *)&battery->serial_number);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_BLOCK, ACPI_SB_SMBUS_ADDR, 0x20,
				   (u8 *)battery->manufacturer_name);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_str() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_BLOCK, ACPI_SB_SMBUS_ADDR, 0x21,
				   (u8 *)battery->device_name);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_str() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_BLOCK, ACPI_SB_SMBUS_ADDR, 0x22,
				   (u8 *)battery->device_chemistry);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_str() failed"));
		goto end;
	}

      end:
	return result;
}

static int acpi_battery_get_state(struct acpi_battery *battery)
{
	int result = 0;

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x09,
				    (u8 *)&battery->voltage_now);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x0a,
				    (u8 *)&battery->current_now);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x0f,
				    (u8 *)&battery->capacity_now);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x16,
				    (u8 *)&battery->state);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

      end:
	return result;
}

static int acpi_battery_get_alarm(struct acpi_battery *battery)
{
	int result = 0;

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x01,
				    (u8 *)&battery->alarm_capacity);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

      end:

	return result;
}

static int acpi_battery_set_alarm(struct acpi_battery *battery,
				  unsigned long alarm)
{
	int result = 0;
	s16 battery_mode;
	int foo;

	result = acpi_battery_select(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_select() failed"));
		goto end;
	}

	/* If necessary, enable the alarm */

	if (alarm > 0) {
		result =
		    acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x03,
				       (u8 *)&battery_mode);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_smbus_read() failed"));
			goto end;
		}

		battery_mode &= 0xbfff;
		result =
		    acpi_smbus_write(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x01,
					(u8 *)&battery_mode, 2);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_smbus_write() failed"));
			goto end;
		}
	}

	foo = alarm / (battery->mode ? 10 : 1);
	result = acpi_smbus_write(battery->sbs->hc, SMBUS_READ_WORD, ACPI_SB_SMBUS_ADDR, 0x01,
				  (u8 *)&foo, 2);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_write() failed"));
		goto end;
	}

      end:

	return result;
}

static int acpi_battery_set_mode(struct acpi_battery *battery)
{
	int result = 0;
	s16 battery_mode;

	if (mode == DEF_CAPACITY_UNIT) {
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD,
				 ACPI_SB_SMBUS_ADDR, 0x03, (u8 *)&battery_mode);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	if (mode == MAH_CAPACITY_UNIT) {
		battery_mode &= 0x7fff;
	} else {
		battery_mode |= 0x8000;
	}
	result = acpi_smbus_write(battery->sbs->hc, SMBUS_READ_WORD,
				  ACPI_SB_SMBUS_ADDR, 0x03, (u8 *)&battery_mode, 2);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_write() failed"));
		goto end;
	}

	result = acpi_smbus_read(battery->sbs->hc, SMBUS_READ_WORD,
				 ACPI_SB_SMBUS_ADDR, 0x03, (u8 *)&battery_mode);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

      end:
	return result;
}

static int acpi_battery_init(struct acpi_battery *battery)
{
	int result = 0;

	result = acpi_battery_select(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_select() failed"));
		goto end;
	}

	result = acpi_battery_set_mode(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_set_mode() failed"));
		goto end;
	}

	result = acpi_battery_get_info(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_info() failed"));
		goto end;
	}

	result = acpi_battery_get_state(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_state() failed"));
		goto end;
	}

	result = acpi_battery_get_alarm(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_alarm() failed"));
		goto end;
	}

      end:
	return result;
}

static int acpi_ac_get_present(struct acpi_sbs *sbs)
{
	int result = 0;
	s16 charger_status;

	result = acpi_smbus_read(sbs->hc, SMBUS_READ_WORD, ACPI_SBC_SMBUS_ADDR, 0x13,
				 (u8 *)&charger_status);

	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_smbus_read() failed"));
		goto end;
	}

	sbs->charger_present = (charger_status & 0x8000) >> 15;

      end:

	return result;
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc/acpi)
   -------------------------------------------------------------------------- */

/* Generic Routines */

static int
acpi_sbs_generic_add_fs(struct proc_dir_entry **dir,
			struct proc_dir_entry *parent_dir,
			char *dir_name,
			struct file_operations *info_fops,
			struct file_operations *state_fops,
			struct file_operations *alarm_fops, void *data)
{
	struct proc_dir_entry *entry = NULL;

	if (!*dir) {
		*dir = proc_mkdir(dir_name, parent_dir);
		if (!*dir) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"proc_mkdir() failed"));
			return -ENODEV;
		}
		(*dir)->owner = THIS_MODULE;
	}

	/* 'info' [R] */
	if (info_fops) {
		entry = create_proc_entry(ACPI_SBS_FILE_INFO, S_IRUGO, *dir);
		if (!entry) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"create_proc_entry() failed"));
		} else {
			entry->proc_fops = info_fops;
			entry->data = data;
			entry->owner = THIS_MODULE;
		}
	}

	/* 'state' [R] */
	if (state_fops) {
		entry = create_proc_entry(ACPI_SBS_FILE_STATE, S_IRUGO, *dir);
		if (!entry) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"create_proc_entry() failed"));
		} else {
			entry->proc_fops = state_fops;
			entry->data = data;
			entry->owner = THIS_MODULE;
		}
	}

	/* 'alarm' [R/W] */
	if (alarm_fops) {
		entry = create_proc_entry(ACPI_SBS_FILE_ALARM, S_IRUGO, *dir);
		if (!entry) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"create_proc_entry() failed"));
		} else {
			entry->proc_fops = alarm_fops;
			entry->data = data;
			entry->owner = THIS_MODULE;
		}
	}

	return 0;
}

static void
acpi_sbs_generic_remove_fs(struct proc_dir_entry **dir,
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

static int acpi_battery_read_info(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	int cscale;
	int result = 0;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, battery->id, DATA_TYPE_INFO);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	if (battery->present) {
		seq_printf(seq, "present:                 yes\n");
	} else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	if (battery->mode) {
		cscale = battery->vscale * battery->ipscale;
	} else {
		cscale = battery->ipscale;
	}
	seq_printf(seq, "design capacity:         %i%s\n",
		   battery->design_capacity * cscale,
		   battery->mode ? "0 mWh" : " mAh");

	seq_printf(seq, "last full capacity:      %i%s\n",
		   battery->full_charge_capacity * cscale,
		   battery->mode ? "0 mWh" : " mAh");

	seq_printf(seq, "battery technology:      rechargeable\n");

	seq_printf(seq, "design voltage:          %i mV\n",
		   battery->design_voltage * battery->vscale);

	seq_printf(seq, "design capacity warning: unknown\n");
	seq_printf(seq, "design capacity low:     unknown\n");
	seq_printf(seq, "capacity granularity 1:  unknown\n");
	seq_printf(seq, "capacity granularity 2:  unknown\n");

	seq_printf(seq, "model number:            %s\n",
		   battery->device_name);

	seq_printf(seq, "serial number:           %i\n",
		   battery->serial_number);

	seq_printf(seq, "battery type:            %s\n",
		   battery->device_chemistry);

	seq_printf(seq, "OEM info:                %s\n",
		   battery->manufacturer_name);

      end:

	sbs_mutex_unlock(sbs);

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
	int result = 0;
	int cscale;
	int foo;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, battery->id, DATA_TYPE_STATE);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	if (battery->present) {
		seq_printf(seq, "present:                 yes\n");
	} else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	if (battery->mode) {
		cscale = battery->vscale * battery->ipscale;
	} else {
		cscale = battery->ipscale;
	}

	if (battery->state & 0x0010) {
		seq_printf(seq, "capacity state:          critical\n");
	} else {
		seq_printf(seq, "capacity state:          ok\n");
	}

	foo = (s16) battery->current_now * battery->ipscale;
	if (battery->mode) {
		foo = foo * battery->design_voltage / 1000;
	}
	if (battery->current_now < 0) {
		seq_printf(seq, "charging state:          discharging\n");
		seq_printf(seq, "present rate:            %d %s\n",
			   -foo, battery->mode ? "mW" : "mA");
	} else if (battery->current_now > 0) {
		seq_printf(seq, "charging state:          charging\n");
		seq_printf(seq, "present rate:            %d %s\n",
			   foo, battery->mode ? "mW" : "mA");
	} else {
		seq_printf(seq, "charging state:          charged\n");
		seq_printf(seq, "present rate:            0 %s\n",
			   battery->mode ? "mW" : "mA");
	}

	seq_printf(seq, "remaining capacity:      %i%s\n",
		   battery->capacity_now * cscale,
		   battery->mode ? "0 mWh" : " mAh");

	seq_printf(seq, "present voltage:         %i mV\n",
		   battery->voltage_now * battery->vscale);

      end:

	sbs_mutex_unlock(sbs);

	return result;
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
	int cscale;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, battery->id, DATA_TYPE_ALARM);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	if (!battery->present) {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	if (battery->mode) {
		cscale = battery->vscale * battery->ipscale;
	} else {
		cscale = battery->ipscale;
	}

	seq_printf(seq, "alarm:                   ");
	if (battery->alarm_capacity) {
		seq_printf(seq, "%i%s\n",
			   battery->alarm_capacity * cscale,
			   battery->mode ? "0 mWh" : " mAh");
	} else {
		seq_printf(seq, "disabled\n");
	}

      end:

	sbs_mutex_unlock(sbs);

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
	int result, old_alarm, new_alarm;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

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

	old_alarm = battery->alarm_capacity;
	new_alarm = simple_strtoul(alarm_string, NULL, 0);

	result = acpi_battery_set_alarm(battery, new_alarm);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_set_alarm() failed"));
		acpi_battery_set_alarm(battery, old_alarm);
		goto end;
	}
	result = acpi_battery_get_alarm(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_alarm() failed"));
		acpi_battery_set_alarm(battery, old_alarm);
		goto end;
	}

      end:
	sbs_mutex_unlock(sbs);

	if (result) {
		return result;
	} else {
		return count;
	}
}

static int acpi_battery_alarm_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_alarm, PDE(inode)->data);
}

static struct file_operations acpi_battery_info_fops = {
	.open = acpi_battery_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct file_operations acpi_battery_state_fops = {
	.open = acpi_battery_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct file_operations acpi_battery_alarm_fops = {
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
	int result;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, -1, DATA_TYPE_AC_STATE);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	seq_printf(seq, "state:                   %s\n",
		   sbs->charger_present ? "on-line" : "off-line");

	sbs_mutex_unlock(sbs);

	return 0;
}

static int acpi_ac_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_ac_read_state, PDE(inode)->data);
}

static struct file_operations acpi_ac_state_fops = {
	.open = acpi_ac_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

/* Smart Battery */

static int acpi_battery_add(struct acpi_sbs *sbs, int id)
{
	int is_present;
	int result;
	char dir_name[32];
	struct acpi_battery *battery;

	battery = &sbs->battery[id];

	battery->alive = 0;

	battery->init_state = 0;
	battery->id = id;
	battery->sbs = sbs;

	result = acpi_battery_select(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_select() failed"));
		goto end;
	}

	result = acpi_battery_get_present(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_present() failed"));
		goto end;
	}

	is_present = battery->present;

	if (is_present) {
		result = acpi_battery_init(battery);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_init() failed"));
			goto end;
		}
		battery->init_state = 1;
	}

	sprintf(dir_name, ACPI_BATTERY_DIR_NAME, id);

	result = acpi_sbs_generic_add_fs(&battery->proc_entry,
					 acpi_battery_dir,
					 dir_name,
					 &acpi_battery_info_fops,
					 &acpi_battery_state_fops,
					 &acpi_battery_alarm_fops, battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_generic_add_fs() failed"));
		goto end;
	}
	battery->alive = 1;

	printk(KERN_INFO PREFIX "%s [%s]: Battery Slot [%s] (battery %s)\n",
	       ACPI_SBS_DEVICE_NAME, acpi_device_bid(sbs->device), dir_name,
	       sbs->battery->present ? "present" : "absent");

      end:
	return result;
}

static void acpi_battery_remove(struct acpi_sbs *sbs, int id)
{

	if (sbs->battery[id].proc_entry) {
		acpi_sbs_generic_remove_fs(&(sbs->battery[id].proc_entry),
					   acpi_battery_dir);
	}
}

static int acpi_ac_add(struct acpi_sbs *sbs)
{
	int result;

	result = acpi_ac_get_present(sbs);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_ac_get_present() failed"));
		goto end;
	}

	result = acpi_sbs_generic_add_fs(&sbs->ac_entry,
					 acpi_ac_dir,
					 ACPI_AC_DIR_NAME,
					 NULL, &acpi_ac_state_fops, NULL, sbs);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_generic_add_fs() failed"));
		goto end;
	}

	printk(KERN_INFO PREFIX "%s [%s]: AC Adapter [%s] (%s)\n",
	       ACPI_SBS_DEVICE_NAME, acpi_device_bid(sbs->device),
	       ACPI_AC_DIR_NAME, sbs->charger_present ? "on-line" : "off-line");

      end:

	return result;
}

static void acpi_ac_remove(struct acpi_sbs *sbs)
{

	if (sbs->ac_entry) {
		acpi_sbs_generic_remove_fs(&sbs->ac_entry, acpi_ac_dir);
	}
}

static void acpi_sbs_update_time_run(unsigned long data)
{
	acpi_os_execute(OSL_GPE_HANDLER, acpi_sbs_update_time, (void *)data);
}

static int acpi_sbs_update_run(struct acpi_sbs *sbs, int id, int data_type)
{
	struct acpi_battery *battery;
	int result = 0, cnt;
	int old_ac_present = -1;
	int old_present = -1;
	int new_ac_present = -1;
	int new_present = -1;
	int id_min = 0, id_max = MAX_SBS_BAT - 1;
	char dir_name[32];
	int do_battery_init = 0, do_ac_init = 0;
	int old_remaining_capacity = 0;
	int update_battery = 1;
	int up_tm = update_time;

	if (sbs_zombie(sbs)) {
		goto end;
	}

	if (id >= 0) {
		id_min = id_max = id;
	}

	if (data_type == DATA_TYPE_COMMON && up_tm > 0) {
		cnt = up_tm / (up_tm > UPDATE_DELAY ? UPDATE_DELAY : up_tm);
		if (sbs->run_cnt % cnt != 0) {
			update_battery = 0;
		}
	}

	sbs->run_cnt++;

	if (!update_battery) {
		goto end;
	}

	old_ac_present = sbs->charger_present;

	result = acpi_ac_get_present(sbs);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_ac_get_present() failed"));
	}

	new_ac_present = sbs->charger_present;

	do_ac_init = (old_ac_present != new_ac_present);
	if (sbs->run_cnt == 1 && data_type == DATA_TYPE_COMMON) {
		do_ac_init = 1;
	}

	if (do_ac_init) {
		result = acpi_bus_generate_proc_event4(ACPI_AC_CLASS,
						 ACPI_AC_DIR_NAME,
						 ACPI_SBS_AC_NOTIFY_STATUS,
						 new_ac_present);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_bus_generate_event4() failed"));
		}
		acpi_bus_generate_netlink_event(ACPI_AC_CLASS, ACPI_AC_DIR_NAME,
						ACPI_SBS_AC_NOTIFY_STATUS,
						new_ac_present);
	}

	if (data_type == DATA_TYPE_COMMON) {
		if (!do_ac_init && !update_battery) {
			goto end;
		}
	}

	if (data_type == DATA_TYPE_AC_STATE && !do_ac_init) {
		goto end;
	}

	for (id = id_min; id <= id_max; id++) {
		battery = &sbs->battery[id];
		if (battery->alive == 0) {
			continue;
		}

		old_remaining_capacity = battery->capacity_now;

		old_present = battery->present;

		result = acpi_battery_select(battery);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_select() failed"));
		}

		result = acpi_battery_get_present(battery);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_get_present() failed"));
		}

		new_present = battery->present;

		do_battery_init = ((old_present != new_present)
				   && new_present);
		if (!new_present)
			goto event;
		if (do_ac_init || do_battery_init) {
			result = acpi_battery_init(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_init() "
						"failed"));
			}
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

		if ((data_type == DATA_TYPE_COMMON
		     || data_type == DATA_TYPE_INFO)
		    && new_present) {
			result = acpi_battery_get_info(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_get_info() failed"));
			}
		}
		if (data_type == DATA_TYPE_INFO) {
			continue;
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

		if ((data_type == DATA_TYPE_COMMON
		     || data_type == DATA_TYPE_STATE)
		    && new_present) {
			result = acpi_battery_get_state(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_get_state() failed"));
			}
		}
		if (data_type == DATA_TYPE_STATE) {
			goto event;
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

		if ((data_type == DATA_TYPE_COMMON
		     || data_type == DATA_TYPE_ALARM)
		    && new_present) {
			result = acpi_battery_get_alarm(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_get_alarm() "
						"failed"));
			}
		}
		if (data_type == DATA_TYPE_ALARM) {
			continue;
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

	      event:

		if (old_present != new_present || do_ac_init ||
		    old_remaining_capacity !=
		    battery->capacity_now) {
			sprintf(dir_name, ACPI_BATTERY_DIR_NAME, id);
			result = acpi_bus_generate_proc_event4(ACPI_BATTERY_CLASS,
							 dir_name,
							 ACPI_SBS_BATTERY_NOTIFY_STATUS,
							 new_present);
			acpi_bus_generate_netlink_event(ACPI_BATTERY_CLASS, dir_name,
							ACPI_SBS_BATTERY_NOTIFY_STATUS,
							new_present);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_bus_generate_proc_event4() "
						"failed"));
			}
		}
	}

      end:

	return result;
}

static void acpi_sbs_update_time(void *data)
{
	struct acpi_sbs *sbs = data;
	unsigned long delay = -1;
	int result;
	unsigned int up_tm = update_time;

	if (sbs_mutex_lock(sbs))
		return;

	result = acpi_sbs_update_run(sbs, -1, DATA_TYPE_COMMON);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_update_run() failed"));
	}

	if (sbs_zombie(sbs)) {
		goto end;
	}

	if (!up_tm) {
		if (timer_pending(&sbs->update_timer))
			del_timer(&sbs->update_timer);
	} else {
		delay = (up_tm > UPDATE_DELAY ? UPDATE_DELAY : up_tm);
		delay = jiffies + HZ * delay;
		if (timer_pending(&sbs->update_timer)) {
			mod_timer(&sbs->update_timer, delay);
		} else {
			sbs->update_timer.data = (unsigned long)data;
			sbs->update_timer.function = acpi_sbs_update_time_run;
			sbs->update_timer.expires = delay;
			add_timer(&sbs->update_timer);
		}
	}

      end:

	sbs_mutex_unlock(sbs);
}

static int acpi_sbs_add(struct acpi_device *device)
{
	struct acpi_sbs *sbs = NULL;
	int result = 0, remove_result = 0;
	int id;

	sbs = kzalloc(sizeof(struct acpi_sbs), GFP_KERNEL);
	if (!sbs) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "kzalloc() failed"));
		result = -ENOMEM;
		goto end;
	}

	mutex_init(&sbs->mutex);

	sbs_mutex_lock(sbs);

	sbs->device = device;
	sbs->hc = acpi_driver_data(device->parent);

	strcpy(acpi_device_name(device), ACPI_SBS_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_SBS_CLASS);
	acpi_driver_data(device) = sbs;

	result = acpi_ac_add(sbs);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "acpi_ac_add() failed"));
		goto end;
	}

	acpi_sbsm_get_info(sbs);

	if (!sbs->manager_present) {
		result = acpi_battery_add(sbs, 0);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_add() failed"));
			goto end;
		}
	} else {
		for (id = 0; id < MAX_SBS_BAT; id++) {
			if ((sbs->batteries_supported & (1 << id))) {
				result = acpi_battery_add(sbs, id);
				if (result) {
					ACPI_EXCEPTION((AE_INFO, AE_ERROR,
							"acpi_battery_add() failed"));
					goto end;
				}
			}
		}
	}

	init_timer(&sbs->update_timer);
	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

      end:

	sbs_mutex_unlock(sbs);

	if (result) {
		remove_result = acpi_sbs_remove(device, 0);
		if (remove_result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_remove() failed"));
		}
	}

	return result;
}

static int acpi_sbs_remove(struct acpi_device *device, int type)
{
	struct acpi_sbs *sbs;
	int id;

	if (!device) {
		return -EINVAL;
	}

	sbs = acpi_driver_data(device);
	if (!sbs) {
		return -EINVAL;
	}

	sbs_mutex_lock(sbs);

	sbs->zombie = 1;
	del_timer_sync(&sbs->update_timer);
	acpi_os_wait_events_complete(NULL);
	del_timer_sync(&sbs->update_timer);

	for (id = 0; id < MAX_SBS_BAT; id++) {
		acpi_battery_remove(sbs, id);
	}

	acpi_ac_remove(sbs);

	sbs_mutex_unlock(sbs);

	mutex_destroy(&sbs->mutex);

	kfree(sbs);

	return 0;
}

static void acpi_sbs_rmdirs(void)
{
	if (acpi_ac_dir) {
		acpi_unlock_ac_dir(acpi_ac_dir);
		acpi_ac_dir = NULL;
	}
	if (acpi_battery_dir) {
		acpi_unlock_battery_dir(acpi_battery_dir);
		acpi_battery_dir = NULL;
	}
}

static int acpi_sbs_resume(struct acpi_device *device)
{
	struct acpi_sbs *sbs;

	if (!device)
		return -EINVAL;

	sbs = device->driver_data;

	sbs->run_cnt = 0;

	return 0;
}

static int __init acpi_sbs_init(void)
{
	int result = 0;

	if (acpi_disabled)
		return -ENODEV;

	if (mode != DEF_CAPACITY_UNIT
	    && mode != MAH_CAPACITY_UNIT
	    && mode != MWH_CAPACITY_UNIT) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"invalid mode = %d", mode));
		return -EINVAL;
	}

	acpi_ac_dir = acpi_lock_ac_dir();
	if (!acpi_ac_dir) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_lock_ac_dir() failed"));
		return -ENODEV;
	}

	acpi_battery_dir = acpi_lock_battery_dir();
	if (!acpi_battery_dir) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_lock_battery_dir() failed"));
		acpi_sbs_rmdirs();
		return -ENODEV;
	}

	result = acpi_bus_register_driver(&acpi_sbs_driver);
	if (result < 0) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_bus_register_driver() failed"));
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

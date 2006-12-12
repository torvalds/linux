/*
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
#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acmacros.h>
#include <acpi/actypes.h>
#include <acpi/acutils.h>

ACPI_MODULE_NAME("cm_sbs")
#define ACPI_AC_CLASS		"ac_adapter"
#define ACPI_BATTERY_CLASS	"battery"
#define ACPI_SBS_COMPONENT	0x00080000
#define _COMPONENT		ACPI_SBS_COMPONENT
static struct proc_dir_entry *acpi_ac_dir;
static struct proc_dir_entry *acpi_battery_dir;

static DEFINE_MUTEX(cm_sbs_mutex);

static int lock_ac_dir_cnt;
static int lock_battery_dir_cnt;

struct proc_dir_entry *acpi_lock_ac_dir(void)
{
	mutex_lock(&cm_sbs_mutex);
	if (!acpi_ac_dir)
		acpi_ac_dir = proc_mkdir(ACPI_AC_CLASS, acpi_root_dir);
	if (acpi_ac_dir) {
		lock_ac_dir_cnt++;
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Cannot create %s\n", ACPI_AC_CLASS));
	}
	mutex_unlock(&cm_sbs_mutex);
	return acpi_ac_dir;
}
EXPORT_SYMBOL(acpi_lock_ac_dir);

void acpi_unlock_ac_dir(struct proc_dir_entry *acpi_ac_dir_param)
{
	mutex_lock(&cm_sbs_mutex);
	if (acpi_ac_dir_param)
		lock_ac_dir_cnt--;
	if (lock_ac_dir_cnt == 0 && acpi_ac_dir_param && acpi_ac_dir) {
		remove_proc_entry(ACPI_AC_CLASS, acpi_root_dir);
		acpi_ac_dir = NULL;
	}
	mutex_unlock(&cm_sbs_mutex);
}
EXPORT_SYMBOL(acpi_unlock_ac_dir);

struct proc_dir_entry *acpi_lock_battery_dir(void)
{
	mutex_lock(&cm_sbs_mutex);
	if (!acpi_battery_dir) {
		acpi_battery_dir =
		    proc_mkdir(ACPI_BATTERY_CLASS, acpi_root_dir);
	}
	if (acpi_battery_dir) {
		lock_battery_dir_cnt++;
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Cannot create %s\n", ACPI_BATTERY_CLASS));
	}
	mutex_unlock(&cm_sbs_mutex);
	return acpi_battery_dir;
}
EXPORT_SYMBOL(acpi_lock_battery_dir);

void acpi_unlock_battery_dir(struct proc_dir_entry *acpi_battery_dir_param)
{
	mutex_lock(&cm_sbs_mutex);
	if (acpi_battery_dir_param)
		lock_battery_dir_cnt--;
	if (lock_battery_dir_cnt == 0 && acpi_battery_dir_param
	    && acpi_battery_dir) {
		remove_proc_entry(ACPI_BATTERY_CLASS, acpi_root_dir);
		acpi_battery_dir = NULL;
	}
	mutex_unlock(&cm_sbs_mutex);
	return;
}
EXPORT_SYMBOL(acpi_unlock_battery_dir);

static int __init acpi_cm_sbs_init(void)
{
	return 0;
}
subsys_initcall(acpi_cm_sbs_init);

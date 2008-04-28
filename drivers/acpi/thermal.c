/*
 *  acpi_thermal.c - ACPI Thermal Zone Driver ($Revision: 41 $)
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
 *
 *  This driver fully implements the ACPI thermal policy as described in the
 *  ACPI 2.0 Specification.
 *
 *  TBD: 1. Implement passive cooling hysteresis.
 *       2. Enhance passive cooling (CPU) states/limit interface to support
 *          concepts of 'multiple limiters', upper/lower limits, etc.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/kmod.h>
#include <linux/seq_file.h>
#include <linux/reboot.h>
#include <asm/uaccess.h>
#include <linux/thermal.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_THERMAL_COMPONENT		0x04000000
#define ACPI_THERMAL_CLASS		"thermal_zone"
#define ACPI_THERMAL_DEVICE_NAME	"Thermal Zone"
#define ACPI_THERMAL_FILE_STATE		"state"
#define ACPI_THERMAL_FILE_TEMPERATURE	"temperature"
#define ACPI_THERMAL_FILE_TRIP_POINTS	"trip_points"
#define ACPI_THERMAL_FILE_COOLING_MODE	"cooling_mode"
#define ACPI_THERMAL_FILE_POLLING_FREQ	"polling_frequency"
#define ACPI_THERMAL_NOTIFY_TEMPERATURE	0x80
#define ACPI_THERMAL_NOTIFY_THRESHOLDS	0x81
#define ACPI_THERMAL_NOTIFY_DEVICES	0x82
#define ACPI_THERMAL_NOTIFY_CRITICAL	0xF0
#define ACPI_THERMAL_NOTIFY_HOT		0xF1
#define ACPI_THERMAL_MODE_ACTIVE	0x00

#define ACPI_THERMAL_MAX_ACTIVE	10
#define ACPI_THERMAL_MAX_LIMIT_STR_LEN 65

#define _COMPONENT		ACPI_THERMAL_COMPONENT
ACPI_MODULE_NAME("thermal");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Thermal Zone Driver");
MODULE_LICENSE("GPL");

static int act;
module_param(act, int, 0644);
MODULE_PARM_DESC(act, "Disable or override all lowest active trip points.");

static int crt;
module_param(crt, int, 0644);
MODULE_PARM_DESC(crt, "Disable or lower all critical trip points.");

static int tzp;
module_param(tzp, int, 0444);
MODULE_PARM_DESC(tzp, "Thermal zone polling frequency, in 1/10 seconds.");

static int nocrt;
module_param(nocrt, int, 0);
MODULE_PARM_DESC(nocrt, "Set to take no action upon ACPI thermal zone critical trips points.");

static int off;
module_param(off, int, 0);
MODULE_PARM_DESC(off, "Set to disable ACPI thermal support.");

static int psv;
module_param(psv, int, 0644);
MODULE_PARM_DESC(psv, "Disable or override all passive trip points.");

static int acpi_thermal_add(struct acpi_device *device);
static int acpi_thermal_remove(struct acpi_device *device, int type);
static int acpi_thermal_resume(struct acpi_device *device);
static int acpi_thermal_state_open_fs(struct inode *inode, struct file *file);
static int acpi_thermal_temp_open_fs(struct inode *inode, struct file *file);
static int acpi_thermal_trip_open_fs(struct inode *inode, struct file *file);
static int acpi_thermal_cooling_open_fs(struct inode *inode, struct file *file);
static ssize_t acpi_thermal_write_cooling_mode(struct file *,
					       const char __user *, size_t,
					       loff_t *);
static int acpi_thermal_polling_open_fs(struct inode *inode, struct file *file);
static ssize_t acpi_thermal_write_polling(struct file *, const char __user *,
					  size_t, loff_t *);

static const struct acpi_device_id  thermal_device_ids[] = {
	{ACPI_THERMAL_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, thermal_device_ids);

static struct acpi_driver acpi_thermal_driver = {
	.name = "thermal",
	.class = ACPI_THERMAL_CLASS,
	.ids = thermal_device_ids,
	.ops = {
		.add = acpi_thermal_add,
		.remove = acpi_thermal_remove,
		.resume = acpi_thermal_resume,
		},
};

struct acpi_thermal_state {
	u8 critical:1;
	u8 hot:1;
	u8 passive:1;
	u8 active:1;
	u8 reserved:4;
	int active_index;
};

struct acpi_thermal_state_flags {
	u8 valid:1;
	u8 enabled:1;
	u8 reserved:6;
};

struct acpi_thermal_critical {
	struct acpi_thermal_state_flags flags;
	unsigned long temperature;
};

struct acpi_thermal_hot {
	struct acpi_thermal_state_flags flags;
	unsigned long temperature;
};

struct acpi_thermal_passive {
	struct acpi_thermal_state_flags flags;
	unsigned long temperature;
	unsigned long tc1;
	unsigned long tc2;
	unsigned long tsp;
	struct acpi_handle_list devices;
};

struct acpi_thermal_active {
	struct acpi_thermal_state_flags flags;
	unsigned long temperature;
	struct acpi_handle_list devices;
};

struct acpi_thermal_trips {
	struct acpi_thermal_critical critical;
	struct acpi_thermal_hot hot;
	struct acpi_thermal_passive passive;
	struct acpi_thermal_active active[ACPI_THERMAL_MAX_ACTIVE];
};

struct acpi_thermal_flags {
	u8 cooling_mode:1;	/* _SCP */
	u8 devices:1;		/* _TZD */
	u8 reserved:6;
};

struct acpi_thermal {
	struct acpi_device * device;
	acpi_bus_id name;
	unsigned long temperature;
	unsigned long last_temperature;
	unsigned long polling_frequency;
	volatile u8 zombie;
	struct acpi_thermal_flags flags;
	struct acpi_thermal_state state;
	struct acpi_thermal_trips trips;
	struct acpi_handle_list devices;
	struct timer_list timer;
	struct thermal_zone_device *thermal_zone;
	int tz_enabled;
	struct mutex lock;
};

static const struct file_operations acpi_thermal_state_fops = {
	.open = acpi_thermal_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations acpi_thermal_temp_fops = {
	.open = acpi_thermal_temp_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations acpi_thermal_trip_fops = {
	.open = acpi_thermal_trip_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations acpi_thermal_cooling_fops = {
	.open = acpi_thermal_cooling_open_fs,
	.read = seq_read,
	.write = acpi_thermal_write_cooling_mode,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations acpi_thermal_polling_fops = {
	.open = acpi_thermal_polling_open_fs,
	.read = seq_read,
	.write = acpi_thermal_write_polling,
	.llseek = seq_lseek,
	.release = single_release,
};

/* --------------------------------------------------------------------------
                             Thermal Zone Management
   -------------------------------------------------------------------------- */

static int acpi_thermal_get_temperature(struct acpi_thermal *tz)
{
	acpi_status status = AE_OK;


	if (!tz)
		return -EINVAL;

	tz->last_temperature = tz->temperature;

	status =
	    acpi_evaluate_integer(tz->device->handle, "_TMP", NULL, &tz->temperature);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Temperature is %lu dK\n",
			  tz->temperature));

	return 0;
}

static int acpi_thermal_get_polling_frequency(struct acpi_thermal *tz)
{
	acpi_status status = AE_OK;


	if (!tz)
		return -EINVAL;

	status =
	    acpi_evaluate_integer(tz->device->handle, "_TZP", NULL,
				  &tz->polling_frequency);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Polling frequency is %lu dS\n",
			  tz->polling_frequency));

	return 0;
}

static int acpi_thermal_set_polling(struct acpi_thermal *tz, int seconds)
{

	if (!tz)
		return -EINVAL;

	tz->polling_frequency = seconds * 10;	/* Convert value to deci-seconds */

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Polling frequency set to %lu seconds\n",
			  tz->polling_frequency/10));

	return 0;
}

static int acpi_thermal_set_cooling_mode(struct acpi_thermal *tz, int mode)
{
	acpi_status status = AE_OK;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };
	acpi_handle handle = NULL;


	if (!tz)
		return -EINVAL;

	status = acpi_get_handle(tz->device->handle, "_SCP", &handle);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "_SCP not present\n"));
		return -ENODEV;
	}

	arg0.integer.value = mode;

	status = acpi_evaluate_object(handle, NULL, &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

#define ACPI_TRIPS_CRITICAL	0x01
#define ACPI_TRIPS_HOT		0x02
#define ACPI_TRIPS_PASSIVE	0x04
#define ACPI_TRIPS_ACTIVE	0x08
#define ACPI_TRIPS_DEVICES	0x10

#define ACPI_TRIPS_REFRESH_THRESHOLDS	(ACPI_TRIPS_PASSIVE | ACPI_TRIPS_ACTIVE)
#define ACPI_TRIPS_REFRESH_DEVICES	ACPI_TRIPS_DEVICES

#define ACPI_TRIPS_INIT      (ACPI_TRIPS_CRITICAL | ACPI_TRIPS_HOT |	\
			      ACPI_TRIPS_PASSIVE | ACPI_TRIPS_ACTIVE |	\
			      ACPI_TRIPS_DEVICES)

/*
 * This exception is thrown out in two cases:
 * 1.An invalid trip point becomes invalid or a valid trip point becomes invalid
 *   when re-evaluating the AML code.
 * 2.TODO: Devices listed in _PSL, _ALx, _TZD may change.
 *   We need to re-bind the cooling devices of a thermal zone when this occurs.
 */
#define ACPI_THERMAL_TRIPS_EXCEPTION(flags, str)	\
do {	\
	if (flags != ACPI_TRIPS_INIT)	\
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,	\
		"ACPI thermal trip point %s changed\n"	\
		"Please send acpidump to linux-acpi@vger.kernel.org\n", str)); \
} while (0)

static int acpi_thermal_trips_update(struct acpi_thermal *tz, int flag)
{
	acpi_status status = AE_OK;
	struct acpi_handle_list devices;
	int valid = 0;
	int i;

	/* Critical Shutdown (required) */
	if (flag & ACPI_TRIPS_CRITICAL) {
		status = acpi_evaluate_integer(tz->device->handle,
				"_CRT", NULL, &tz->trips.critical.temperature);
		if (ACPI_FAILURE(status)) {
			tz->trips.critical.flags.valid = 0;
			ACPI_EXCEPTION((AE_INFO, status,
					"No critical threshold"));
			return -ENODEV;
		} else {
			tz->trips.critical.flags.valid = 1;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					"Found critical threshold [%lu]\n",
					tz->trips.critical.temperature));
		}
		if (tz->trips.critical.flags.valid == 1) {
			if (crt == -1) {
				tz->trips.critical.flags.valid = 0;
			} else if (crt > 0) {
				unsigned long crt_k = CELSIUS_TO_KELVIN(crt);
				/*
				 * Allow override to lower critical threshold
				 */
				if (crt_k < tz->trips.critical.temperature)
					tz->trips.critical.temperature = crt_k;
			}
		}
	}

	/* Critical Sleep (optional) */
	if (flag & ACPI_TRIPS_HOT) {
		status = acpi_evaluate_integer(tz->device->handle,
				"_HOT", NULL, &tz->trips.hot.temperature);
		if (ACPI_FAILURE(status)) {
			tz->trips.hot.flags.valid = 0;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					"No hot threshold\n"));
		} else {
			tz->trips.hot.flags.valid = 1;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					"Found hot threshold [%lu]\n",
					tz->trips.critical.temperature));
		}
	}

	/* Passive (optional) */
	if (flag & ACPI_TRIPS_PASSIVE) {
		valid = tz->trips.passive.flags.valid;
		if (psv == -1) {
			status = AE_SUPPORT;
		} else if (psv > 0) {
			tz->trips.passive.temperature = CELSIUS_TO_KELVIN(psv);
			status = AE_OK;
		} else {
			status = acpi_evaluate_integer(tz->device->handle,
				"_PSV", NULL, &tz->trips.passive.temperature);
		}

		if (ACPI_FAILURE(status))
			tz->trips.passive.flags.valid = 0;
		else {
			tz->trips.passive.flags.valid = 1;
			if (flag == ACPI_TRIPS_INIT) {
				status = acpi_evaluate_integer(
						tz->device->handle, "_TC1",
						NULL, &tz->trips.passive.tc1);
				if (ACPI_FAILURE(status))
					tz->trips.passive.flags.valid = 0;
				status = acpi_evaluate_integer(
						tz->device->handle, "_TC2",
						NULL, &tz->trips.passive.tc2);
				if (ACPI_FAILURE(status))
					tz->trips.passive.flags.valid = 0;
				status = acpi_evaluate_integer(
						tz->device->handle, "_TSP",
						NULL, &tz->trips.passive.tsp);
				if (ACPI_FAILURE(status))
					tz->trips.passive.flags.valid = 0;
			}
		}
	}
	if ((flag & ACPI_TRIPS_DEVICES) && tz->trips.passive.flags.valid) {
		memset(&devices, 0, sizeof(struct acpi_handle_list));
		status = acpi_evaluate_reference(tz->device->handle, "_PSL",
							NULL, &devices);
		if (ACPI_FAILURE(status))
			tz->trips.passive.flags.valid = 0;
		else
			tz->trips.passive.flags.valid = 1;

		if (memcmp(&tz->trips.passive.devices, &devices,
				sizeof(struct acpi_handle_list))) {
			memcpy(&tz->trips.passive.devices, &devices,
				sizeof(struct acpi_handle_list));
			ACPI_THERMAL_TRIPS_EXCEPTION(flag, "device");
		}
	}
	if ((flag & ACPI_TRIPS_PASSIVE) || (flag & ACPI_TRIPS_DEVICES)) {
		if (valid != tz->trips.passive.flags.valid)
				ACPI_THERMAL_TRIPS_EXCEPTION(flag, "state");
	}

	/* Active (optional) */
	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		char name[5] = { '_', 'A', 'C', ('0' + i), '\0' };
		valid = tz->trips.active[i].flags.valid;

		if (act == -1)
			break; /* disable all active trip points */

		if (flag & ACPI_TRIPS_ACTIVE) {
			status = acpi_evaluate_integer(tz->device->handle,
				name, NULL, &tz->trips.active[i].temperature);
			if (ACPI_FAILURE(status)) {
				tz->trips.active[i].flags.valid = 0;
				if (i == 0)
					break;
				if (act <= 0)
					break;
				if (i == 1)
					tz->trips.active[0].temperature =
						CELSIUS_TO_KELVIN(act);
				else
					/*
					 * Don't allow override higher than
					 * the next higher trip point
					 */
					tz->trips.active[i - 1].temperature =
						(tz->trips.active[i - 2].temperature <
						CELSIUS_TO_KELVIN(act) ?
						tz->trips.active[i - 2].temperature :
						CELSIUS_TO_KELVIN(act));
				break;
			} else
				tz->trips.active[i].flags.valid = 1;
		}

		name[2] = 'L';
		if ((flag & ACPI_TRIPS_DEVICES) && tz->trips.active[i].flags.valid ) {
			memset(&devices, 0, sizeof(struct acpi_handle_list));
			status = acpi_evaluate_reference(tz->device->handle,
						name, NULL, &devices);
			if (ACPI_FAILURE(status))
				tz->trips.active[i].flags.valid = 0;
			else
				tz->trips.active[i].flags.valid = 1;

			if (memcmp(&tz->trips.active[i].devices, &devices,
					sizeof(struct acpi_handle_list))) {
				memcpy(&tz->trips.active[i].devices, &devices,
					sizeof(struct acpi_handle_list));
				ACPI_THERMAL_TRIPS_EXCEPTION(flag, "device");
			}
		}
		if ((flag & ACPI_TRIPS_ACTIVE) || (flag & ACPI_TRIPS_DEVICES))
			if (valid != tz->trips.active[i].flags.valid)
				ACPI_THERMAL_TRIPS_EXCEPTION(flag, "state");

		if (!tz->trips.active[i].flags.valid)
			break;
	}

	if (flag & ACPI_TRIPS_DEVICES) {
		memset(&devices, 0, sizeof(struct acpi_handle_list));
		status = acpi_evaluate_reference(tz->device->handle, "_TZD",
						NULL, &devices);
		if (memcmp(&tz->devices, &devices,
				sizeof(struct acpi_handle_list))) {
			memcpy(&tz->devices, &devices,
				sizeof(struct acpi_handle_list));
			ACPI_THERMAL_TRIPS_EXCEPTION(flag, "device");
		}
	}

	return 0;
}

static int acpi_thermal_get_trip_points(struct acpi_thermal *tz)
{
	return acpi_thermal_trips_update(tz, ACPI_TRIPS_INIT);
}

static int acpi_thermal_critical(struct acpi_thermal *tz)
{
	if (!tz || !tz->trips.critical.flags.valid)
		return -EINVAL;

	if (tz->temperature >= tz->trips.critical.temperature) {
		printk(KERN_WARNING PREFIX "Critical trip point\n");
		tz->trips.critical.flags.enabled = 1;
	} else if (tz->trips.critical.flags.enabled)
		tz->trips.critical.flags.enabled = 0;

	acpi_bus_generate_proc_event(tz->device, ACPI_THERMAL_NOTIFY_CRITICAL,
				tz->trips.critical.flags.enabled);
	acpi_bus_generate_netlink_event(tz->device->pnp.device_class,
					  tz->device->dev.bus_id,
					  ACPI_THERMAL_NOTIFY_CRITICAL,
					  tz->trips.critical.flags.enabled);

	/* take no action if nocrt is set */
	if(!nocrt) {
		printk(KERN_EMERG
			"Critical temperature reached (%ld C), shutting down.\n",
			KELVIN_TO_CELSIUS(tz->temperature));
		orderly_poweroff(true);
	}

	return 0;
}

static int acpi_thermal_hot(struct acpi_thermal *tz)
{
	if (!tz || !tz->trips.hot.flags.valid)
		return -EINVAL;

	if (tz->temperature >= tz->trips.hot.temperature) {
		printk(KERN_WARNING PREFIX "Hot trip point\n");
		tz->trips.hot.flags.enabled = 1;
	} else if (tz->trips.hot.flags.enabled)
		tz->trips.hot.flags.enabled = 0;

	acpi_bus_generate_proc_event(tz->device, ACPI_THERMAL_NOTIFY_HOT,
				tz->trips.hot.flags.enabled);
	acpi_bus_generate_netlink_event(tz->device->pnp.device_class,
					  tz->device->dev.bus_id,
					  ACPI_THERMAL_NOTIFY_HOT,
					  tz->trips.hot.flags.enabled);

	/* TBD: Call user-mode "sleep(S4)" function if nocrt is cleared */

	return 0;
}

static void acpi_thermal_passive(struct acpi_thermal *tz)
{
	int result = 1;
	struct acpi_thermal_passive *passive = NULL;
	int trend = 0;
	int i = 0;


	if (!tz || !tz->trips.passive.flags.valid)
		return;

	passive = &(tz->trips.passive);

	/*
	 * Above Trip?
	 * -----------
	 * Calculate the thermal trend (using the passive cooling equation)
	 * and modify the performance limit for all passive cooling devices
	 * accordingly.  Note that we assume symmetry.
	 */
	if (tz->temperature >= passive->temperature) {
		trend =
		    (passive->tc1 * (tz->temperature - tz->last_temperature)) +
		    (passive->tc2 * (tz->temperature - passive->temperature));
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "trend[%d]=(tc1[%lu]*(tmp[%lu]-last[%lu]))+(tc2[%lu]*(tmp[%lu]-psv[%lu]))\n",
				  trend, passive->tc1, tz->temperature,
				  tz->last_temperature, passive->tc2,
				  tz->temperature, passive->temperature));
		passive->flags.enabled = 1;
		/* Heating up? */
		if (trend > 0)
			for (i = 0; i < passive->devices.count; i++)
				acpi_processor_set_thermal_limit(passive->
								 devices.
								 handles[i],
								 ACPI_PROCESSOR_LIMIT_INCREMENT);
		/* Cooling off? */
		else if (trend < 0) {
			for (i = 0; i < passive->devices.count; i++)
				/*
				 * assume that we are on highest
				 * freq/lowest thrott and can leave
				 * passive mode, even in error case
				 */
				if (!acpi_processor_set_thermal_limit
				    (passive->devices.handles[i],
				     ACPI_PROCESSOR_LIMIT_DECREMENT))
					result = 0;
			/*
			 * Leave cooling mode, even if the temp might
			 * higher than trip point This is because some
			 * machines might have long thermal polling
			 * frequencies (tsp) defined. We will fall back
			 * into passive mode in next cycle (probably quicker)
			 */
			if (result) {
				passive->flags.enabled = 0;
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Disabling passive cooling, still above threshold,"
						  " but we are cooling down\n"));
			}
		}
		return;
	}

	/*
	 * Below Trip?
	 * -----------
	 * Implement passive cooling hysteresis to slowly increase performance
	 * and avoid thrashing around the passive trip point.  Note that we
	 * assume symmetry.
	 */
	if (!passive->flags.enabled)
		return;
	for (i = 0; i < passive->devices.count; i++)
		if (!acpi_processor_set_thermal_limit
		    (passive->devices.handles[i],
		     ACPI_PROCESSOR_LIMIT_DECREMENT))
			result = 0;
	if (result) {
		passive->flags.enabled = 0;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Disabling passive cooling (zone is cool)\n"));
	}
}

static void acpi_thermal_active(struct acpi_thermal *tz)
{
	int result = 0;
	struct acpi_thermal_active *active = NULL;
	int i = 0;
	int j = 0;
	unsigned long maxtemp = 0;


	if (!tz)
		return;

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		active = &(tz->trips.active[i]);
		if (!active || !active->flags.valid)
			break;
		if (tz->temperature >= active->temperature) {
			/*
			 * Above Threshold?
			 * ----------------
			 * If not already enabled, turn ON all cooling devices
			 * associated with this active threshold.
			 */
			if (active->temperature > maxtemp)
				tz->state.active_index = i;
			maxtemp = active->temperature;
			if (active->flags.enabled)
				continue;
			for (j = 0; j < active->devices.count; j++) {
				result =
				    acpi_bus_set_power(active->devices.
						       handles[j],
						       ACPI_STATE_D0);
				if (result) {
					printk(KERN_WARNING PREFIX
						      "Unable to turn cooling device [%p] 'on'\n",
						      active->devices.
						      handles[j]);
					continue;
				}
				active->flags.enabled = 1;
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Cooling device [%p] now 'on'\n",
						  active->devices.handles[j]));
			}
			continue;
		}
		if (!active->flags.enabled)
			continue;
		/*
		 * Below Threshold?
		 * ----------------
		 * Turn OFF all cooling devices associated with this
		 * threshold.
		 */
		for (j = 0; j < active->devices.count; j++) {
			result = acpi_bus_set_power(active->devices.handles[j],
						    ACPI_STATE_D3);
			if (result) {
				printk(KERN_WARNING PREFIX
					      "Unable to turn cooling device [%p] 'off'\n",
					      active->devices.handles[j]);
				continue;
			}
			active->flags.enabled = 0;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Cooling device [%p] now 'off'\n",
					  active->devices.handles[j]));
		}
	}
}

static void acpi_thermal_check(void *context);

static void acpi_thermal_run(unsigned long data)
{
	struct acpi_thermal *tz = (struct acpi_thermal *)data;
	if (!tz->zombie)
		acpi_os_execute(OSL_GPE_HANDLER, acpi_thermal_check, (void *)data);
}

static void acpi_thermal_check(void *data)
{
	int result = 0;
	struct acpi_thermal *tz = data;
	unsigned long sleep_time = 0;
	unsigned long timeout_jiffies = 0;
	int i = 0;
	struct acpi_thermal_state state;


	if (!tz) {
		printk(KERN_ERR PREFIX "Invalid (NULL) context\n");
		return;
	}

	/* Check if someone else is already running */
	if (!mutex_trylock(&tz->lock))
		return;

	state = tz->state;

	result = acpi_thermal_get_temperature(tz);
	if (result)
		goto unlock;

	if (!tz->tz_enabled)
		goto unlock;

	memset(&tz->state, 0, sizeof(tz->state));

	/*
	 * Check Trip Points
	 * -----------------
	 * Compare the current temperature to the trip point values to see
	 * if we've entered one of the thermal policy states.  Note that
	 * this function determines when a state is entered, but the 
	 * individual policy decides when it is exited (e.g. hysteresis).
	 */
	if (tz->trips.critical.flags.valid)
		state.critical |=
		    (tz->temperature >= tz->trips.critical.temperature);
	if (tz->trips.hot.flags.valid)
		state.hot |= (tz->temperature >= tz->trips.hot.temperature);
	if (tz->trips.passive.flags.valid)
		state.passive |=
		    (tz->temperature >= tz->trips.passive.temperature);
	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++)
		if (tz->trips.active[i].flags.valid)
			state.active |=
			    (tz->temperature >=
			     tz->trips.active[i].temperature);

	/*
	 * Invoke Policy
	 * -------------
	 * Separated from the above check to allow individual policy to 
	 * determine when to exit a given state.
	 */
	if (state.critical)
		acpi_thermal_critical(tz);
	if (state.hot)
		acpi_thermal_hot(tz);
	if (state.passive)
		acpi_thermal_passive(tz);
	if (state.active)
		acpi_thermal_active(tz);

	/*
	 * Calculate State
	 * ---------------
	 * Again, separated from the above two to allow independent policy
	 * decisions.
	 */
	tz->state.critical = tz->trips.critical.flags.enabled;
	tz->state.hot = tz->trips.hot.flags.enabled;
	tz->state.passive = tz->trips.passive.flags.enabled;
	tz->state.active = 0;
	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++)
		tz->state.active |= tz->trips.active[i].flags.enabled;

	/*
	 * Calculate Sleep Time
	 * --------------------
	 * If we're in the passive state, use _TSP's value.  Otherwise
	 * use the default polling frequency (e.g. _TZP).  If no polling
	 * frequency is specified then we'll wait forever (at least until
	 * a thermal event occurs).  Note that _TSP and _TZD values are
	 * given in 1/10th seconds (we must covert to milliseconds).
	 */
	if (tz->state.passive) {
		sleep_time = tz->trips.passive.tsp * 100;
		timeout_jiffies =  jiffies + (HZ * sleep_time) / 1000;
	} else if (tz->polling_frequency > 0) {
		sleep_time = tz->polling_frequency * 100;
		timeout_jiffies =  round_jiffies(jiffies + (HZ * sleep_time) / 1000);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "%s: temperature[%lu] sleep[%lu]\n",
			  tz->name, tz->temperature, sleep_time));

	/*
	 * Schedule Next Poll
	 * ------------------
	 */
	if (!sleep_time) {
		if (timer_pending(&(tz->timer)))
			del_timer(&(tz->timer));
	} else {
		if (timer_pending(&(tz->timer)))
			mod_timer(&(tz->timer), timeout_jiffies);
		else {
			tz->timer.data = (unsigned long)tz;
			tz->timer.function = acpi_thermal_run;
			tz->timer.expires = timeout_jiffies;
			add_timer(&(tz->timer));
		}
	}
      unlock:
	mutex_unlock(&tz->lock);
}

/* sys I/F for generic thermal sysfs support */
#define KELVIN_TO_MILLICELSIUS(t) (t * 100 - 273200)

static int thermal_get_temp(struct thermal_zone_device *thermal, char *buf)
{
	struct acpi_thermal *tz = thermal->devdata;

	if (!tz)
		return -EINVAL;

	return sprintf(buf, "%ld\n", KELVIN_TO_MILLICELSIUS(tz->temperature));
}

static const char enabled[] = "kernel";
static const char disabled[] = "user";
static int thermal_get_mode(struct thermal_zone_device *thermal,
				char *buf)
{
	struct acpi_thermal *tz = thermal->devdata;

	if (!tz)
		return -EINVAL;

	return sprintf(buf, "%s\n", tz->tz_enabled ?
			enabled : disabled);
}

static int thermal_set_mode(struct thermal_zone_device *thermal,
				const char *buf)
{
	struct acpi_thermal *tz = thermal->devdata;
	int enable;

	if (!tz)
		return -EINVAL;

	/*
	 * enable/disable thermal management from ACPI thermal driver
	 */
	if (!strncmp(buf, enabled, sizeof enabled - 1))
		enable = 1;
	else if (!strncmp(buf, disabled, sizeof disabled - 1))
		enable = 0;
	else
		return -EINVAL;

	if (enable != tz->tz_enabled) {
		tz->tz_enabled = enable;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"%s ACPI thermal control\n",
			tz->tz_enabled ? enabled : disabled));
		acpi_thermal_check(tz);
	}
	return 0;
}

static int thermal_get_trip_type(struct thermal_zone_device *thermal,
				 int trip, char *buf)
{
	struct acpi_thermal *tz = thermal->devdata;
	int i;

	if (!tz || trip < 0)
		return -EINVAL;

	if (tz->trips.critical.flags.valid) {
		if (!trip)
			return sprintf(buf, "critical\n");
		trip--;
	}

	if (tz->trips.hot.flags.valid) {
		if (!trip)
			return sprintf(buf, "hot\n");
		trip--;
	}

	if (tz->trips.passive.flags.valid) {
		if (!trip)
			return sprintf(buf, "passive\n");
		trip--;
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE &&
		tz->trips.active[i].flags.valid; i++) {
		if (!trip)
			return sprintf(buf, "active%d\n", i);
		trip--;
	}

	return -EINVAL;
}

static int thermal_get_trip_temp(struct thermal_zone_device *thermal,
				 int trip, char *buf)
{
	struct acpi_thermal *tz = thermal->devdata;
	int i;

	if (!tz || trip < 0)
		return -EINVAL;

	if (tz->trips.critical.flags.valid) {
		if (!trip)
			return sprintf(buf, "%ld\n", KELVIN_TO_MILLICELSIUS(
				tz->trips.critical.temperature));
		trip--;
	}

	if (tz->trips.hot.flags.valid) {
		if (!trip)
			return sprintf(buf, "%ld\n", KELVIN_TO_MILLICELSIUS(
					tz->trips.hot.temperature));
		trip--;
	}

	if (tz->trips.passive.flags.valid) {
		if (!trip)
			return sprintf(buf, "%ld\n", KELVIN_TO_MILLICELSIUS(
					tz->trips.passive.temperature));
		trip--;
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE &&
		tz->trips.active[i].flags.valid; i++) {
		if (!trip)
			return sprintf(buf, "%ld\n", KELVIN_TO_MILLICELSIUS(
					tz->trips.active[i].temperature));
		trip--;
	}

	return -EINVAL;
}

typedef int (*cb)(struct thermal_zone_device *, int,
		  struct thermal_cooling_device *);
static int acpi_thermal_cooling_device_cb(struct thermal_zone_device *thermal,
					struct thermal_cooling_device *cdev,
					cb action)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_thermal *tz = thermal->devdata;
	struct acpi_device *dev;
	acpi_status status;
	acpi_handle handle;
	int i;
	int j;
	int trip = -1;
	int result = 0;

	if (tz->trips.critical.flags.valid)
		trip++;

	if (tz->trips.hot.flags.valid)
		trip++;

	if (tz->trips.passive.flags.valid) {
		trip++;
		for (i = 0; i < tz->trips.passive.devices.count;
		    i++) {
			handle = tz->trips.passive.devices.handles[i];
			status = acpi_bus_get_device(handle, &dev);
			if (ACPI_SUCCESS(status) && (dev == device)) {
				result = action(thermal, trip, cdev);
				if (result)
					goto failed;
			}
		}
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		if (!tz->trips.active[i].flags.valid)
			break;
		trip++;
		for (j = 0;
		    j < tz->trips.active[i].devices.count;
		    j++) {
			handle = tz->trips.active[i].devices.handles[j];
			status = acpi_bus_get_device(handle, &dev);
			if (ACPI_SUCCESS(status) && (dev == device)) {
				result = action(thermal, trip, cdev);
				if (result)
					goto failed;
			}
		}
	}

	for (i = 0; i < tz->devices.count; i++) {
		handle = tz->devices.handles[i];
		status = acpi_bus_get_device(handle, &dev);
		if (ACPI_SUCCESS(status) && (dev == device)) {
			result = action(thermal, -1, cdev);
			if (result)
				goto failed;
		}
	}

failed:
	return result;
}

static int
acpi_thermal_bind_cooling_device(struct thermal_zone_device *thermal,
					struct thermal_cooling_device *cdev)
{
	return acpi_thermal_cooling_device_cb(thermal, cdev,
				thermal_zone_bind_cooling_device);
}

static int
acpi_thermal_unbind_cooling_device(struct thermal_zone_device *thermal,
					struct thermal_cooling_device *cdev)
{
	return acpi_thermal_cooling_device_cb(thermal, cdev,
				thermal_zone_unbind_cooling_device);
}

static struct thermal_zone_device_ops acpi_thermal_zone_ops = {
	.bind = acpi_thermal_bind_cooling_device,
	.unbind	= acpi_thermal_unbind_cooling_device,
	.get_temp = thermal_get_temp,
	.get_mode = thermal_get_mode,
	.set_mode = thermal_set_mode,
	.get_trip_type = thermal_get_trip_type,
	.get_trip_temp = thermal_get_trip_temp,
};

static int acpi_thermal_register_thermal_zone(struct acpi_thermal *tz)
{
	int trips = 0;
	int result;
	acpi_status status;
	int i;

	if (tz->trips.critical.flags.valid)
		trips++;

	if (tz->trips.hot.flags.valid)
		trips++;

	if (tz->trips.passive.flags.valid)
		trips++;

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE &&
			tz->trips.active[i].flags.valid; i++, trips++);
	tz->thermal_zone = thermal_zone_device_register("ACPI thermal zone",
					trips, tz, &acpi_thermal_zone_ops);
	if (IS_ERR(tz->thermal_zone))
		return -ENODEV;

	result = sysfs_create_link(&tz->device->dev.kobj,
				   &tz->thermal_zone->device.kobj, "thermal_zone");
	if (result)
		return result;

	result = sysfs_create_link(&tz->thermal_zone->device.kobj,
				   &tz->device->dev.kobj, "device");
	if (result)
		return result;

	status = acpi_attach_data(tz->device->handle,
				  acpi_bus_private_data_handler,
				  tz->thermal_zone);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				"Error attaching device data\n"));
		return -ENODEV;
	}

	tz->tz_enabled = 1;

	printk(KERN_INFO PREFIX "%s is registered as thermal_zone%d\n",
			tz->device->dev.bus_id, tz->thermal_zone->id);
	return 0;
}

static void acpi_thermal_unregister_thermal_zone(struct acpi_thermal *tz)
{
	sysfs_remove_link(&tz->device->dev.kobj, "thermal_zone");
	sysfs_remove_link(&tz->thermal_zone->device.kobj, "device");
	thermal_zone_device_unregister(tz->thermal_zone);
	tz->thermal_zone = NULL;
	acpi_detach_data(tz->device->handle, acpi_bus_private_data_handler);
}


/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_thermal_dir;

static int acpi_thermal_state_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_thermal *tz = seq->private;


	if (!tz)
		goto end;

	seq_puts(seq, "state:                   ");

	if (!tz->state.critical && !tz->state.hot && !tz->state.passive
	    && !tz->state.active)
		seq_puts(seq, "ok\n");
	else {
		if (tz->state.critical)
			seq_puts(seq, "critical ");
		if (tz->state.hot)
			seq_puts(seq, "hot ");
		if (tz->state.passive)
			seq_puts(seq, "passive ");
		if (tz->state.active)
			seq_printf(seq, "active[%d]", tz->state.active_index);
		seq_puts(seq, "\n");
	}

      end:
	return 0;
}

static int acpi_thermal_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_thermal_state_seq_show, PDE(inode)->data);
}

static int acpi_thermal_temp_seq_show(struct seq_file *seq, void *offset)
{
	int result = 0;
	struct acpi_thermal *tz = seq->private;


	if (!tz)
		goto end;

	result = acpi_thermal_get_temperature(tz);
	if (result)
		goto end;

	seq_printf(seq, "temperature:             %ld C\n",
		   KELVIN_TO_CELSIUS(tz->temperature));

      end:
	return 0;
}

static int acpi_thermal_temp_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_thermal_temp_seq_show, PDE(inode)->data);
}

static int acpi_thermal_trip_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_thermal *tz = seq->private;
	struct acpi_device *device;
	acpi_status status;

	int i = 0;
	int j = 0;


	if (!tz)
		goto end;

	if (tz->trips.critical.flags.valid)
		seq_printf(seq, "critical (S5):           %ld C%s",
			   KELVIN_TO_CELSIUS(tz->trips.critical.temperature),
			   nocrt ? " <disabled>\n" : "\n");

	if (tz->trips.hot.flags.valid)
		seq_printf(seq, "hot (S4):                %ld C%s",
			   KELVIN_TO_CELSIUS(tz->trips.hot.temperature),
			   nocrt ? " <disabled>\n" : "\n");

	if (tz->trips.passive.flags.valid) {
		seq_printf(seq,
			   "passive:                 %ld C: tc1=%lu tc2=%lu tsp=%lu devices=",
			   KELVIN_TO_CELSIUS(tz->trips.passive.temperature),
			   tz->trips.passive.tc1, tz->trips.passive.tc2,
			   tz->trips.passive.tsp);
		for (j = 0; j < tz->trips.passive.devices.count; j++) {
			status = acpi_bus_get_device(tz->trips.passive.devices.
						     handles[j], &device);
			seq_printf(seq, "%4.4s ", status ? "" :
				   acpi_device_bid(device));
		}
		seq_puts(seq, "\n");
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		if (!(tz->trips.active[i].flags.valid))
			break;
		seq_printf(seq, "active[%d]:               %ld C: devices=",
			   i,
			   KELVIN_TO_CELSIUS(tz->trips.active[i].temperature));
		for (j = 0; j < tz->trips.active[i].devices.count; j++){
			status = acpi_bus_get_device(tz->trips.active[i].
						     devices.handles[j],
						     &device);
			seq_printf(seq, "%4.4s ", status ? "" :
				   acpi_device_bid(device));
		}
		seq_puts(seq, "\n");
	}

      end:
	return 0;
}

static int acpi_thermal_trip_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_thermal_trip_seq_show, PDE(inode)->data);
}

static int acpi_thermal_cooling_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_thermal *tz = seq->private;


	if (!tz)
		goto end;

	if (!tz->flags.cooling_mode)
		seq_puts(seq, "<setting not supported>\n");
	else
		seq_puts(seq, "0 - Active; 1 - Passive\n");

      end:
	return 0;
}

static int acpi_thermal_cooling_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_thermal_cooling_seq_show,
			   PDE(inode)->data);
}

static ssize_t
acpi_thermal_write_cooling_mode(struct file *file,
				const char __user * buffer,
				size_t count, loff_t * ppos)
{
	struct seq_file *m = file->private_data;
	struct acpi_thermal *tz = m->private;
	int result = 0;
	char mode_string[12] = { '\0' };


	if (!tz || (count > sizeof(mode_string) - 1))
		return -EINVAL;

	if (!tz->flags.cooling_mode)
		return -ENODEV;

	if (copy_from_user(mode_string, buffer, count))
		return -EFAULT;

	mode_string[count] = '\0';

	result = acpi_thermal_set_cooling_mode(tz,
					       simple_strtoul(mode_string, NULL,
							      0));
	if (result)
		return result;

	acpi_thermal_check(tz);

	return count;
}

static int acpi_thermal_polling_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_thermal *tz = seq->private;


	if (!tz)
		goto end;

	if (!tz->polling_frequency) {
		seq_puts(seq, "<polling disabled>\n");
		goto end;
	}

	seq_printf(seq, "polling frequency:       %lu seconds\n",
		   (tz->polling_frequency / 10));

      end:
	return 0;
}

static int acpi_thermal_polling_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_thermal_polling_seq_show,
			   PDE(inode)->data);
}

static ssize_t
acpi_thermal_write_polling(struct file *file,
			   const char __user * buffer,
			   size_t count, loff_t * ppos)
{
	struct seq_file *m = file->private_data;
	struct acpi_thermal *tz = m->private;
	int result = 0;
	char polling_string[12] = { '\0' };
	int seconds = 0;


	if (!tz || (count > sizeof(polling_string) - 1))
		return -EINVAL;

	if (copy_from_user(polling_string, buffer, count))
		return -EFAULT;

	polling_string[count] = '\0';

	seconds = simple_strtoul(polling_string, NULL, 0);

	result = acpi_thermal_set_polling(tz, seconds);
	if (result)
		return result;

	acpi_thermal_check(tz);

	return count;
}

static int acpi_thermal_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;


	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_thermal_dir);
		if (!acpi_device_dir(device))
			return -ENODEV;
		acpi_device_dir(device)->owner = THIS_MODULE;
	}

	/* 'state' [R] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_STATE,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		return -ENODEV;
	else {
		entry->proc_fops = &acpi_thermal_state_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'temperature' [R] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_TEMPERATURE,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		return -ENODEV;
	else {
		entry->proc_fops = &acpi_thermal_temp_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'trip_points' [R] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_TRIP_POINTS,
				  S_IRUGO,
				  acpi_device_dir(device));
	if (!entry)
		return -ENODEV;
	else {
		entry->proc_fops = &acpi_thermal_trip_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'cooling_mode' [R/W] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_COOLING_MODE,
				  S_IFREG | S_IRUGO | S_IWUSR,
				  acpi_device_dir(device));
	if (!entry)
		return -ENODEV;
	else {
		entry->proc_fops = &acpi_thermal_cooling_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'polling_frequency' [R/W] */
	entry = create_proc_entry(ACPI_THERMAL_FILE_POLLING_FREQ,
				  S_IFREG | S_IRUGO | S_IWUSR,
				  acpi_device_dir(device));
	if (!entry)
		return -ENODEV;
	else {
		entry->proc_fops = &acpi_thermal_polling_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	return 0;
}

static int acpi_thermal_remove_fs(struct acpi_device *device)
{

	if (acpi_device_dir(device)) {
		remove_proc_entry(ACPI_THERMAL_FILE_POLLING_FREQ,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_THERMAL_FILE_COOLING_MODE,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_THERMAL_FILE_TRIP_POINTS,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_THERMAL_FILE_TEMPERATURE,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_THERMAL_FILE_STATE,
				  acpi_device_dir(device));
		remove_proc_entry(acpi_device_bid(device), acpi_thermal_dir);
		acpi_device_dir(device) = NULL;
	}

	return 0;
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_thermal_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_thermal *tz = data;
	struct acpi_device *device = NULL;


	if (!tz)
		return;

	device = tz->device;

	switch (event) {
	case ACPI_THERMAL_NOTIFY_TEMPERATURE:
		acpi_thermal_check(tz);
		break;
	case ACPI_THERMAL_NOTIFY_THRESHOLDS:
		acpi_thermal_trips_update(tz, ACPI_TRIPS_REFRESH_THRESHOLDS);
		acpi_thermal_check(tz);
		acpi_bus_generate_proc_event(device, event, 0);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  device->dev.bus_id, event, 0);
		break;
	case ACPI_THERMAL_NOTIFY_DEVICES:
		acpi_thermal_trips_update(tz, ACPI_TRIPS_REFRESH_DEVICES);
		acpi_thermal_check(tz);
		acpi_bus_generate_proc_event(device, event, 0);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  device->dev.bus_id, event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return;
}

static int acpi_thermal_get_info(struct acpi_thermal *tz)
{
	int result = 0;


	if (!tz)
		return -EINVAL;

	/* Get temperature [_TMP] (required) */
	result = acpi_thermal_get_temperature(tz);
	if (result)
		return result;

	/* Get trip points [_CRT, _PSV, etc.] (required) */
	result = acpi_thermal_get_trip_points(tz);
	if (result)
		return result;

	/* Set the cooling mode [_SCP] to active cooling (default) */
	result = acpi_thermal_set_cooling_mode(tz, ACPI_THERMAL_MODE_ACTIVE);
	if (!result)
		tz->flags.cooling_mode = 1;

	/* Get default polling frequency [_TZP] (optional) */
	if (tzp)
		tz->polling_frequency = tzp;
	else
		acpi_thermal_get_polling_frequency(tz);

	return 0;
}

static int acpi_thermal_add(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_thermal *tz = NULL;


	if (!device)
		return -EINVAL;

	tz = kzalloc(sizeof(struct acpi_thermal), GFP_KERNEL);
	if (!tz)
		return -ENOMEM;

	tz->device = device;
	strcpy(tz->name, device->pnp.bus_id);
	strcpy(acpi_device_name(device), ACPI_THERMAL_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_THERMAL_CLASS);
	acpi_driver_data(device) = tz;
	mutex_init(&tz->lock);


	result = acpi_thermal_get_info(tz);
	if (result)
		goto free_memory;

	result = acpi_thermal_register_thermal_zone(tz);
	if (result)
		goto free_memory;

	result = acpi_thermal_add_fs(device);
	if (result)
		goto unregister_thermal_zone;

	init_timer(&tz->timer);

	acpi_thermal_check(tz);

	status = acpi_install_notify_handler(device->handle,
					     ACPI_DEVICE_NOTIFY,
					     acpi_thermal_notify, tz);
	if (ACPI_FAILURE(status)) {
		result = -ENODEV;
		goto remove_fs;
	}

	printk(KERN_INFO PREFIX "%s [%s] (%ld C)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       KELVIN_TO_CELSIUS(tz->temperature));
	goto end;

remove_fs:
	acpi_thermal_remove_fs(device);
unregister_thermal_zone:
	thermal_zone_device_unregister(tz->thermal_zone);
free_memory:
	kfree(tz);
end:
	return result;
}

static int acpi_thermal_remove(struct acpi_device *device, int type)
{
	acpi_status status = AE_OK;
	struct acpi_thermal *tz = NULL;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	tz = acpi_driver_data(device);

	/* avoid timer adding new defer task */
	tz->zombie = 1;
	/* wait for running timer (on other CPUs) finish */
	del_timer_sync(&(tz->timer));
	/* synchronize deferred task */
	acpi_os_wait_events_complete(NULL);
	/* deferred task may reinsert timer */
	del_timer_sync(&(tz->timer));

	status = acpi_remove_notify_handler(device->handle,
					    ACPI_DEVICE_NOTIFY,
					    acpi_thermal_notify);

	/* Terminate policy */
	if (tz->trips.passive.flags.valid && tz->trips.passive.flags.enabled) {
		tz->trips.passive.flags.enabled = 0;
		acpi_thermal_passive(tz);
	}
	if (tz->trips.active[0].flags.valid
	    && tz->trips.active[0].flags.enabled) {
		tz->trips.active[0].flags.enabled = 0;
		acpi_thermal_active(tz);
	}

	acpi_thermal_remove_fs(device);
	acpi_thermal_unregister_thermal_zone(tz);
	mutex_destroy(&tz->lock);
	kfree(tz);
	return 0;
}

static int acpi_thermal_resume(struct acpi_device *device)
{
	struct acpi_thermal *tz = NULL;
	int i, j, power_state, result;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	tz = acpi_driver_data(device);

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		if (!(&tz->trips.active[i]))
			break;
		if (!tz->trips.active[i].flags.valid)
			break;
		tz->trips.active[i].flags.enabled = 1;
		for (j = 0; j < tz->trips.active[i].devices.count; j++) {
			result = acpi_bus_get_power(tz->trips.active[i].devices.
			    handles[j], &power_state);
			if (result || (power_state != ACPI_STATE_D0)) {
				tz->trips.active[i].flags.enabled = 0;
				break;
			}
		}
		tz->state.active |= tz->trips.active[i].flags.enabled;
	}

	acpi_thermal_check(tz);

	return AE_OK;
}

static int thermal_act(const struct dmi_system_id *d) {

	if (act == 0) {
		printk(KERN_NOTICE "ACPI: %s detected: "
			"disabling all active thermal trip points\n", d->ident);
		act = -1;
	}
	return 0;
}
static int thermal_nocrt(const struct dmi_system_id *d) {

	printk(KERN_NOTICE "ACPI: %s detected: "
		"disabling all critical thermal trip point actions.\n", d->ident);
	nocrt = 1;
	return 0;
}
static int thermal_tzp(const struct dmi_system_id *d) {

	if (tzp == 0) {
		printk(KERN_NOTICE "ACPI: %s detected: "
			"enabling thermal zone polling\n", d->ident);
		tzp = 300;	/* 300 dS = 30 Seconds */
	}
	return 0;
}
static int thermal_psv(const struct dmi_system_id *d) {

	if (psv == 0) {
		printk(KERN_NOTICE "ACPI: %s detected: "
			"disabling all passive thermal trip points\n", d->ident);
		psv = -1;
	}
	return 0;
}

static struct dmi_system_id thermal_dmi_table[] __initdata = {
	/*
	 * Award BIOS on this AOpen makes thermal control almost worthless.
	 * http://bugzilla.kernel.org/show_bug.cgi?id=8842
	 */
	{
	 .callback = thermal_act,
	 .ident = "AOpen i915GMm-HFS",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "AOpen"),
		DMI_MATCH(DMI_BOARD_NAME, "i915GMm-HFS"),
		},
	},
	{
	 .callback = thermal_psv,
	 .ident = "AOpen i915GMm-HFS",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "AOpen"),
		DMI_MATCH(DMI_BOARD_NAME, "i915GMm-HFS"),
		},
	},
	{
	 .callback = thermal_tzp,
	 .ident = "AOpen i915GMm-HFS",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "AOpen"),
		DMI_MATCH(DMI_BOARD_NAME, "i915GMm-HFS"),
		},
	},
	{
	 .callback = thermal_nocrt,
	 .ident = "Gigabyte GA-7ZX",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Gigabyte Technology Co., Ltd."),
		DMI_MATCH(DMI_BOARD_NAME, "7ZX"),
		},
	},
	{}
};

static int __init acpi_thermal_init(void)
{
	int result = 0;

	dmi_check_system(thermal_dmi_table);

	if (off) {
		printk(KERN_NOTICE "ACPI: thermal control disabled\n");
		return -ENODEV;
	}
	acpi_thermal_dir = proc_mkdir(ACPI_THERMAL_CLASS, acpi_root_dir);
	if (!acpi_thermal_dir)
		return -ENODEV;
	acpi_thermal_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&acpi_thermal_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_THERMAL_CLASS, acpi_root_dir);
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_thermal_exit(void)
{

	acpi_bus_unregister_driver(&acpi_thermal_driver);

	remove_proc_entry(ACPI_THERMAL_CLASS, acpi_root_dir);

	return;
}

module_init(acpi_thermal_init);
module_exit(acpi_thermal_exit);

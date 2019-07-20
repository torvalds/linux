// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_thermal.c - ACPI Thermal Zone Driver ($Revision: 41 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 *  This driver fully implements the ACPI thermal policy as described in the
 *  ACPI 2.0 Specification.
 *
 *  TBD: 1. Implement passive cooling hysteresis.
 *       2. Enhance passive cooling (CPU) states/limit interface to support
 *          concepts of 'multiple limiters', upper/lower limits, etc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/kmod.h>
#include <linux/reboot.h>
#include <linux/device.h>
#include <linux/thermal.h>
#include <linux/acpi.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>

#define PREFIX "ACPI: "

#define ACPI_THERMAL_CLASS		"thermal_zone"
#define ACPI_THERMAL_DEVICE_NAME	"Thermal Zone"
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

static struct workqueue_struct *acpi_thermal_pm_queue;

static int acpi_thermal_add(struct acpi_device *device);
static int acpi_thermal_remove(struct acpi_device *device);
static void acpi_thermal_notify(struct acpi_device *device, u32 event);

static const struct acpi_device_id  thermal_device_ids[] = {
	{ACPI_THERMAL_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, thermal_device_ids);

#ifdef CONFIG_PM_SLEEP
static int acpi_thermal_suspend(struct device *dev);
static int acpi_thermal_resume(struct device *dev);
#else
#define acpi_thermal_suspend NULL
#define acpi_thermal_resume NULL
#endif
static SIMPLE_DEV_PM_OPS(acpi_thermal_pm, acpi_thermal_suspend, acpi_thermal_resume);

static struct acpi_driver acpi_thermal_driver = {
	.name = "thermal",
	.class = ACPI_THERMAL_CLASS,
	.ids = thermal_device_ids,
	.ops = {
		.add = acpi_thermal_add,
		.remove = acpi_thermal_remove,
		.notify = acpi_thermal_notify,
		},
	.drv.pm = &acpi_thermal_pm,
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
	struct thermal_zone_device *thermal_zone;
	int tz_enabled;
	int kelvin_offset;
	struct work_struct thermal_check_work;
};

/* --------------------------------------------------------------------------
                             Thermal Zone Management
   -------------------------------------------------------------------------- */

static int acpi_thermal_get_temperature(struct acpi_thermal *tz)
{
	acpi_status status = AE_OK;
	unsigned long long tmp;

	if (!tz)
		return -EINVAL;

	tz->last_temperature = tz->temperature;

	status = acpi_evaluate_integer(tz->device->handle, "_TMP", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	tz->temperature = tmp;
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Temperature is %lu dK\n",
			  tz->temperature));

	return 0;
}

static int acpi_thermal_get_polling_frequency(struct acpi_thermal *tz)
{
	acpi_status status = AE_OK;
	unsigned long long tmp;

	if (!tz)
		return -EINVAL;

	status = acpi_evaluate_integer(tz->device->handle, "_TZP", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	tz->polling_frequency = tmp;
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Polling frequency is %lu dS\n",
			  tz->polling_frequency));

	return 0;
}

static int acpi_thermal_set_cooling_mode(struct acpi_thermal *tz, int mode)
{
	if (!tz)
		return -EINVAL;

	if (!acpi_has_method(tz->device->handle, "_SCP")) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "_SCP not present\n"));
		return -ENODEV;
	} else if (ACPI_FAILURE(acpi_execute_simple_method(tz->device->handle,
							   "_SCP", mode))) {
		return -ENODEV;
	}

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
		"Please send acpidump to linux-acpi@vger.kernel.org", str)); \
} while (0)

static int acpi_thermal_trips_update(struct acpi_thermal *tz, int flag)
{
	acpi_status status = AE_OK;
	unsigned long long tmp;
	struct acpi_handle_list devices;
	int valid = 0;
	int i;

	/* Critical Shutdown */
	if (flag & ACPI_TRIPS_CRITICAL) {
		status = acpi_evaluate_integer(tz->device->handle,
				"_CRT", NULL, &tmp);
		tz->trips.critical.temperature = tmp;
		/*
		 * Treat freezing temperatures as invalid as well; some
		 * BIOSes return really low values and cause reboots at startup.
		 * Below zero (Celsius) values clearly aren't right for sure..
		 * ... so lets discard those as invalid.
		 */
		if (ACPI_FAILURE(status)) {
			tz->trips.critical.flags.valid = 0;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "No critical threshold\n"));
		} else if (tmp <= 2732) {
			pr_warn(FW_BUG "Invalid critical threshold (%llu)\n",
				tmp);
			tz->trips.critical.flags.valid = 0;
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
				unsigned long crt_k = CELSIUS_TO_DECI_KELVIN(crt);
				/*
				 * Allow override critical threshold
				 */
				if (crt_k > tz->trips.critical.temperature)
					pr_warn(PREFIX "Critical threshold %d C\n",
						crt);
				tz->trips.critical.temperature = crt_k;
			}
		}
	}

	/* Critical Sleep (optional) */
	if (flag & ACPI_TRIPS_HOT) {
		status = acpi_evaluate_integer(tz->device->handle,
				"_HOT", NULL, &tmp);
		if (ACPI_FAILURE(status)) {
			tz->trips.hot.flags.valid = 0;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					"No hot threshold\n"));
		} else {
			tz->trips.hot.temperature = tmp;
			tz->trips.hot.flags.valid = 1;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					"Found hot threshold [%lu]\n",
					tz->trips.hot.temperature));
		}
	}

	/* Passive (optional) */
	if (((flag & ACPI_TRIPS_PASSIVE) && tz->trips.passive.flags.valid) ||
		(flag == ACPI_TRIPS_INIT)) {
		valid = tz->trips.passive.flags.valid;
		if (psv == -1) {
			status = AE_SUPPORT;
		} else if (psv > 0) {
			tmp = CELSIUS_TO_DECI_KELVIN(psv);
			status = AE_OK;
		} else {
			status = acpi_evaluate_integer(tz->device->handle,
				"_PSV", NULL, &tmp);
		}

		if (ACPI_FAILURE(status))
			tz->trips.passive.flags.valid = 0;
		else {
			tz->trips.passive.temperature = tmp;
			tz->trips.passive.flags.valid = 1;
			if (flag == ACPI_TRIPS_INIT) {
				status = acpi_evaluate_integer(
						tz->device->handle, "_TC1",
						NULL, &tmp);
				if (ACPI_FAILURE(status))
					tz->trips.passive.flags.valid = 0;
				else
					tz->trips.passive.tc1 = tmp;
				status = acpi_evaluate_integer(
						tz->device->handle, "_TC2",
						NULL, &tmp);
				if (ACPI_FAILURE(status))
					tz->trips.passive.flags.valid = 0;
				else
					tz->trips.passive.tc2 = tmp;
				status = acpi_evaluate_integer(
						tz->device->handle, "_TSP",
						NULL, &tmp);
				if (ACPI_FAILURE(status))
					tz->trips.passive.flags.valid = 0;
				else
					tz->trips.passive.tsp = tmp;
			}
		}
	}
	if ((flag & ACPI_TRIPS_DEVICES) && tz->trips.passive.flags.valid) {
		memset(&devices, 0, sizeof(struct acpi_handle_list));
		status = acpi_evaluate_reference(tz->device->handle, "_PSL",
							NULL, &devices);
		if (ACPI_FAILURE(status)) {
			pr_warn(PREFIX "Invalid passive threshold\n");
			tz->trips.passive.flags.valid = 0;
		}
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

		if ((flag == ACPI_TRIPS_INIT) || ((flag & ACPI_TRIPS_ACTIVE) &&
			tz->trips.active[i].flags.valid)) {
			status = acpi_evaluate_integer(tz->device->handle,
							name, NULL, &tmp);
			if (ACPI_FAILURE(status)) {
				tz->trips.active[i].flags.valid = 0;
				if (i == 0)
					break;
				if (act <= 0)
					break;
				if (i == 1)
					tz->trips.active[0].temperature =
						CELSIUS_TO_DECI_KELVIN(act);
				else
					/*
					 * Don't allow override higher than
					 * the next higher trip point
					 */
					tz->trips.active[i - 1].temperature =
						(tz->trips.active[i - 2].temperature <
						CELSIUS_TO_DECI_KELVIN(act) ?
						tz->trips.active[i - 2].temperature :
						CELSIUS_TO_DECI_KELVIN(act));
				break;
			} else {
				tz->trips.active[i].temperature = tmp;
				tz->trips.active[i].flags.valid = 1;
			}
		}

		name[2] = 'L';
		if ((flag & ACPI_TRIPS_DEVICES) && tz->trips.active[i].flags.valid ) {
			memset(&devices, 0, sizeof(struct acpi_handle_list));
			status = acpi_evaluate_reference(tz->device->handle,
						name, NULL, &devices);
			if (ACPI_FAILURE(status)) {
				pr_warn(PREFIX "Invalid active%d threshold\n",
					i);
				tz->trips.active[i].flags.valid = 0;
			}
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

	if ((flag & ACPI_TRIPS_DEVICES)
	    && acpi_has_method(tz->device->handle, "_TZD")) {
		memset(&devices, 0, sizeof(devices));
		status = acpi_evaluate_reference(tz->device->handle, "_TZD",
						NULL, &devices);
		if (ACPI_SUCCESS(status)
		    && memcmp(&tz->devices, &devices, sizeof(devices))) {
			tz->devices = devices;
			ACPI_THERMAL_TRIPS_EXCEPTION(flag, "device");
		}
	}

	return 0;
}

static int acpi_thermal_get_trip_points(struct acpi_thermal *tz)
{
	int i, valid, ret = acpi_thermal_trips_update(tz, ACPI_TRIPS_INIT);

	if (ret)
		return ret;

	valid = tz->trips.critical.flags.valid |
		tz->trips.hot.flags.valid |
		tz->trips.passive.flags.valid;

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++)
		valid |= tz->trips.active[i].flags.valid;

	if (!valid) {
		pr_warn(FW_BUG "No valid trip found\n");
		return -ENODEV;
	}
	return 0;
}

static void acpi_thermal_check(void *data)
{
	struct acpi_thermal *tz = data;

	if (!tz->tz_enabled)
		return;

	thermal_zone_device_update(tz->thermal_zone,
				   THERMAL_EVENT_UNSPECIFIED);
}

/* sys I/F for generic thermal sysfs support */

static int thermal_get_temp(struct thermal_zone_device *thermal, int *temp)
{
	struct acpi_thermal *tz = thermal->devdata;
	int result;

	if (!tz)
		return -EINVAL;

	result = acpi_thermal_get_temperature(tz);
	if (result)
		return result;

	*temp = DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(tz->temperature,
							tz->kelvin_offset);
	return 0;
}

static int thermal_get_mode(struct thermal_zone_device *thermal,
				enum thermal_device_mode *mode)
{
	struct acpi_thermal *tz = thermal->devdata;

	if (!tz)
		return -EINVAL;

	*mode = tz->tz_enabled ? THERMAL_DEVICE_ENABLED :
		THERMAL_DEVICE_DISABLED;

	return 0;
}

static int thermal_set_mode(struct thermal_zone_device *thermal,
				enum thermal_device_mode mode)
{
	struct acpi_thermal *tz = thermal->devdata;
	int enable;

	if (!tz)
		return -EINVAL;

	/*
	 * enable/disable thermal management from ACPI thermal driver
	 */
	if (mode == THERMAL_DEVICE_ENABLED)
		enable = 1;
	else if (mode == THERMAL_DEVICE_DISABLED) {
		enable = 0;
		pr_warn("thermal zone will be disabled\n");
	} else
		return -EINVAL;

	if (enable != tz->tz_enabled) {
		tz->tz_enabled = enable;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"%s kernel ACPI thermal control\n",
			tz->tz_enabled ? "Enable" : "Disable"));
		acpi_thermal_check(tz);
	}
	return 0;
}

static int thermal_get_trip_type(struct thermal_zone_device *thermal,
				 int trip, enum thermal_trip_type *type)
{
	struct acpi_thermal *tz = thermal->devdata;
	int i;

	if (!tz || trip < 0)
		return -EINVAL;

	if (tz->trips.critical.flags.valid) {
		if (!trip) {
			*type = THERMAL_TRIP_CRITICAL;
			return 0;
		}
		trip--;
	}

	if (tz->trips.hot.flags.valid) {
		if (!trip) {
			*type = THERMAL_TRIP_HOT;
			return 0;
		}
		trip--;
	}

	if (tz->trips.passive.flags.valid) {
		if (!trip) {
			*type = THERMAL_TRIP_PASSIVE;
			return 0;
		}
		trip--;
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE &&
		tz->trips.active[i].flags.valid; i++) {
		if (!trip) {
			*type = THERMAL_TRIP_ACTIVE;
			return 0;
		}
		trip--;
	}

	return -EINVAL;
}

static int thermal_get_trip_temp(struct thermal_zone_device *thermal,
				 int trip, int *temp)
{
	struct acpi_thermal *tz = thermal->devdata;
	int i;

	if (!tz || trip < 0)
		return -EINVAL;

	if (tz->trips.critical.flags.valid) {
		if (!trip) {
			*temp = DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(
				tz->trips.critical.temperature,
				tz->kelvin_offset);
			return 0;
		}
		trip--;
	}

	if (tz->trips.hot.flags.valid) {
		if (!trip) {
			*temp = DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(
				tz->trips.hot.temperature,
				tz->kelvin_offset);
			return 0;
		}
		trip--;
	}

	if (tz->trips.passive.flags.valid) {
		if (!trip) {
			*temp = DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(
				tz->trips.passive.temperature,
				tz->kelvin_offset);
			return 0;
		}
		trip--;
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE &&
		tz->trips.active[i].flags.valid; i++) {
		if (!trip) {
			*temp = DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(
				tz->trips.active[i].temperature,
				tz->kelvin_offset);
			return 0;
		}
		trip--;
	}

	return -EINVAL;
}

static int thermal_get_crit_temp(struct thermal_zone_device *thermal,
				int *temperature)
{
	struct acpi_thermal *tz = thermal->devdata;

	if (tz->trips.critical.flags.valid) {
		*temperature = DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(
				tz->trips.critical.temperature,
				tz->kelvin_offset);
		return 0;
	} else
		return -EINVAL;
}

static int thermal_get_trend(struct thermal_zone_device *thermal,
				int trip, enum thermal_trend *trend)
{
	struct acpi_thermal *tz = thermal->devdata;
	enum thermal_trip_type type;
	int i;

	if (thermal_get_trip_type(thermal, trip, &type))
		return -EINVAL;

	if (type == THERMAL_TRIP_ACTIVE) {
		int trip_temp;
		int temp = DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(
					tz->temperature, tz->kelvin_offset);
		if (thermal_get_trip_temp(thermal, trip, &trip_temp))
			return -EINVAL;

		if (temp > trip_temp) {
			*trend = THERMAL_TREND_RAISING;
			return 0;
		} else {
			/* Fall back on default trend */
			return -EINVAL;
		}
	}

	/*
	 * tz->temperature has already been updated by generic thermal layer,
	 * before this callback being invoked
	 */
	i = (tz->trips.passive.tc1 * (tz->temperature - tz->last_temperature))
		+ (tz->trips.passive.tc2
		* (tz->temperature - tz->trips.passive.temperature));

	if (i > 0)
		*trend = THERMAL_TREND_RAISING;
	else if (i < 0)
		*trend = THERMAL_TREND_DROPPING;
	else
		*trend = THERMAL_TREND_STABLE;
	return 0;
}


static int thermal_notify(struct thermal_zone_device *thermal, int trip,
			   enum thermal_trip_type trip_type)
{
	u8 type = 0;
	struct acpi_thermal *tz = thermal->devdata;

	if (trip_type == THERMAL_TRIP_CRITICAL)
		type = ACPI_THERMAL_NOTIFY_CRITICAL;
	else if (trip_type == THERMAL_TRIP_HOT)
		type = ACPI_THERMAL_NOTIFY_HOT;
	else
		return 0;

	acpi_bus_generate_netlink_event(tz->device->pnp.device_class,
					dev_name(&tz->device->dev), type, 1);

	if (trip_type == THERMAL_TRIP_CRITICAL && nocrt)
		return 1;

	return 0;
}

static int acpi_thermal_cooling_device_cb(struct thermal_zone_device *thermal,
					struct thermal_cooling_device *cdev,
					bool bind)
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
			if (ACPI_FAILURE(status) || dev != device)
				continue;
			if (bind)
				result =
					thermal_zone_bind_cooling_device
					(thermal, trip, cdev,
					 THERMAL_NO_LIMIT, THERMAL_NO_LIMIT,
					 THERMAL_WEIGHT_DEFAULT);
			else
				result =
					thermal_zone_unbind_cooling_device
					(thermal, trip, cdev);
			if (result)
				goto failed;
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
			if (ACPI_FAILURE(status) || dev != device)
				continue;
			if (bind)
				result = thermal_zone_bind_cooling_device
					(thermal, trip, cdev,
					 THERMAL_NO_LIMIT, THERMAL_NO_LIMIT,
					 THERMAL_WEIGHT_DEFAULT);
			else
				result = thermal_zone_unbind_cooling_device
					(thermal, trip, cdev);
			if (result)
				goto failed;
		}
	}

	for (i = 0; i < tz->devices.count; i++) {
		handle = tz->devices.handles[i];
		status = acpi_bus_get_device(handle, &dev);
		if (ACPI_SUCCESS(status) && (dev == device)) {
			if (bind)
				result = thermal_zone_bind_cooling_device
						(thermal, THERMAL_TRIPS_NONE,
						 cdev, THERMAL_NO_LIMIT,
						 THERMAL_NO_LIMIT,
						 THERMAL_WEIGHT_DEFAULT);
			else
				result = thermal_zone_unbind_cooling_device
						(thermal, THERMAL_TRIPS_NONE,
						 cdev);
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
	return acpi_thermal_cooling_device_cb(thermal, cdev, true);
}

static int
acpi_thermal_unbind_cooling_device(struct thermal_zone_device *thermal,
					struct thermal_cooling_device *cdev)
{
	return acpi_thermal_cooling_device_cb(thermal, cdev, false);
}

static struct thermal_zone_device_ops acpi_thermal_zone_ops = {
	.bind = acpi_thermal_bind_cooling_device,
	.unbind	= acpi_thermal_unbind_cooling_device,
	.get_temp = thermal_get_temp,
	.get_mode = thermal_get_mode,
	.set_mode = thermal_set_mode,
	.get_trip_type = thermal_get_trip_type,
	.get_trip_temp = thermal_get_trip_temp,
	.get_crit_temp = thermal_get_crit_temp,
	.get_trend = thermal_get_trend,
	.notify = thermal_notify,
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

	if (tz->trips.passive.flags.valid)
		tz->thermal_zone =
			thermal_zone_device_register("acpitz", trips, 0, tz,
						&acpi_thermal_zone_ops, NULL,
						     tz->trips.passive.tsp*100,
						     tz->polling_frequency*100);
	else
		tz->thermal_zone =
			thermal_zone_device_register("acpitz", trips, 0, tz,
						&acpi_thermal_zone_ops, NULL,
						0, tz->polling_frequency*100);
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

	status =  acpi_bus_attach_private_data(tz->device->handle,
					       tz->thermal_zone);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	tz->tz_enabled = 1;

	dev_info(&tz->device->dev, "registered as thermal_zone%d\n",
		 tz->thermal_zone->id);
	return 0;
}

static void acpi_thermal_unregister_thermal_zone(struct acpi_thermal *tz)
{
	sysfs_remove_link(&tz->device->dev.kobj, "thermal_zone");
	sysfs_remove_link(&tz->thermal_zone->device.kobj, "device");
	thermal_zone_device_unregister(tz->thermal_zone);
	tz->thermal_zone = NULL;
	acpi_bus_detach_private_data(tz->device->handle);
}


/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_thermal_notify(struct acpi_device *device, u32 event)
{
	struct acpi_thermal *tz = acpi_driver_data(device);


	if (!tz)
		return;

	switch (event) {
	case ACPI_THERMAL_NOTIFY_TEMPERATURE:
		acpi_thermal_check(tz);
		break;
	case ACPI_THERMAL_NOTIFY_THRESHOLDS:
		acpi_thermal_trips_update(tz, ACPI_TRIPS_REFRESH_THRESHOLDS);
		acpi_thermal_check(tz);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  dev_name(&device->dev), event, 0);
		break;
	case ACPI_THERMAL_NOTIFY_DEVICES:
		acpi_thermal_trips_update(tz, ACPI_TRIPS_REFRESH_DEVICES);
		acpi_thermal_check(tz);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  dev_name(&device->dev), event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}
}

/*
 * On some platforms, the AML code has dependency about
 * the evaluating order of _TMP and _CRT/_HOT/_PSV/_ACx.
 * 1. On HP Pavilion G4-1016tx, _TMP must be invoked after
 *    /_CRT/_HOT/_PSV/_ACx, or else system will be power off.
 * 2. On HP Compaq 6715b/6715s, the return value of _PSV is 0
 *    if _TMP has never been evaluated.
 *
 * As this dependency is totally transparent to OS, evaluate
 * all of them once, in the order of _CRT/_HOT/_PSV/_ACx,
 * _TMP, before they are actually used.
 */
static void acpi_thermal_aml_dependency_fix(struct acpi_thermal *tz)
{
	acpi_handle handle = tz->device->handle;
	unsigned long long value;
	int i;

	acpi_evaluate_integer(handle, "_CRT", NULL, &value);
	acpi_evaluate_integer(handle, "_HOT", NULL, &value);
	acpi_evaluate_integer(handle, "_PSV", NULL, &value);
	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		char name[5] = { '_', 'A', 'C', ('0' + i), '\0' };
		acpi_status status;

		status = acpi_evaluate_integer(handle, name, NULL, &value);
		if (status == AE_NOT_FOUND)
			break;
	}
	acpi_evaluate_integer(handle, "_TMP", NULL, &value);
}

static int acpi_thermal_get_info(struct acpi_thermal *tz)
{
	int result = 0;


	if (!tz)
		return -EINVAL;

	acpi_thermal_aml_dependency_fix(tz);

	/* Get trip points [_CRT, _PSV, etc.] (required) */
	result = acpi_thermal_get_trip_points(tz);
	if (result)
		return result;

	/* Get temperature [_TMP] (required) */
	result = acpi_thermal_get_temperature(tz);
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

/*
 * The exact offset between Kelvin and degree Celsius is 273.15. However ACPI
 * handles temperature values with a single decimal place. As a consequence,
 * some implementations use an offset of 273.1 and others use an offset of
 * 273.2. Try to find out which one is being used, to present the most
 * accurate and visually appealing number.
 *
 * The heuristic below should work for all ACPI thermal zones which have a
 * critical trip point with a value being a multiple of 0.5 degree Celsius.
 */
static void acpi_thermal_guess_offset(struct acpi_thermal *tz)
{
	if (tz->trips.critical.flags.valid &&
	    (tz->trips.critical.temperature % 5) == 1)
		tz->kelvin_offset = 2731;
	else
		tz->kelvin_offset = 2732;
}

static void acpi_thermal_check_fn(struct work_struct *work)
{
	struct acpi_thermal *tz = container_of(work, struct acpi_thermal,
					       thermal_check_work);
	acpi_thermal_check(tz);
}

static int acpi_thermal_add(struct acpi_device *device)
{
	int result = 0;
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
	device->driver_data = tz;

	result = acpi_thermal_get_info(tz);
	if (result)
		goto free_memory;

	acpi_thermal_guess_offset(tz);

	result = acpi_thermal_register_thermal_zone(tz);
	if (result)
		goto free_memory;

	INIT_WORK(&tz->thermal_check_work, acpi_thermal_check_fn);

	pr_info(PREFIX "%s [%s] (%ld C)\n", acpi_device_name(device),
		acpi_device_bid(device), DECI_KELVIN_TO_CELSIUS(tz->temperature));
	goto end;

free_memory:
	kfree(tz);
end:
	return result;
}

static int acpi_thermal_remove(struct acpi_device *device)
{
	struct acpi_thermal *tz = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	flush_workqueue(acpi_thermal_pm_queue);
	tz = acpi_driver_data(device);

	acpi_thermal_unregister_thermal_zone(tz);
	kfree(tz);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int acpi_thermal_suspend(struct device *dev)
{
	/* Make sure the previously queued thermal check work has been done */
	flush_workqueue(acpi_thermal_pm_queue);
	return 0;
}

static int acpi_thermal_resume(struct device *dev)
{
	struct acpi_thermal *tz;
	int i, j, power_state, result;

	if (!dev)
		return -EINVAL;

	tz = acpi_driver_data(to_acpi_device(dev));
	if (!tz)
		return -EINVAL;

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		if (!(&tz->trips.active[i]))
			break;
		if (!tz->trips.active[i].flags.valid)
			break;
		tz->trips.active[i].flags.enabled = 1;
		for (j = 0; j < tz->trips.active[i].devices.count; j++) {
			result = acpi_bus_update_power(
					tz->trips.active[i].devices.handles[j],
					&power_state);
			if (result || (power_state != ACPI_STATE_D0)) {
				tz->trips.active[i].flags.enabled = 0;
				break;
			}
		}
		tz->state.active |= tz->trips.active[i].flags.enabled;
	}

	queue_work(acpi_thermal_pm_queue, &tz->thermal_check_work);

	return AE_OK;
}
#endif

static int thermal_act(const struct dmi_system_id *d) {

	if (act == 0) {
		pr_notice(PREFIX "%s detected: "
			  "disabling all active thermal trip points\n", d->ident);
		act = -1;
	}
	return 0;
}
static int thermal_nocrt(const struct dmi_system_id *d) {

	pr_notice(PREFIX "%s detected: "
		  "disabling all critical thermal trip point actions.\n", d->ident);
	nocrt = 1;
	return 0;
}
static int thermal_tzp(const struct dmi_system_id *d) {

	if (tzp == 0) {
		pr_notice(PREFIX "%s detected: "
			  "enabling thermal zone polling\n", d->ident);
		tzp = 300;	/* 300 dS = 30 Seconds */
	}
	return 0;
}
static int thermal_psv(const struct dmi_system_id *d) {

	if (psv == 0) {
		pr_notice(PREFIX "%s detected: "
			  "disabling all passive thermal trip points\n", d->ident);
		psv = -1;
	}
	return 0;
}

static const struct dmi_system_id thermal_dmi_table[] __initconst = {
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
		pr_notice(PREFIX "thermal control disabled\n");
		return -ENODEV;
	}

	acpi_thermal_pm_queue = alloc_workqueue("acpi_thermal_pm",
						WQ_HIGHPRI | WQ_MEM_RECLAIM, 0);
	if (!acpi_thermal_pm_queue)
		return -ENODEV;

	result = acpi_bus_register_driver(&acpi_thermal_driver);
	if (result < 0) {
		destroy_workqueue(acpi_thermal_pm_queue);
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_thermal_exit(void)
{
	acpi_bus_unregister_driver(&acpi_thermal_driver);
	destroy_workqueue(acpi_thermal_pm_queue);

	return;
}

module_init(acpi_thermal_init);
module_exit(acpi_thermal_exit);

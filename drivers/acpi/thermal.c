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

#define pr_fmt(fmt) "ACPI: thermal: " fmt

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
#include <linux/units.h>

#define ACPI_THERMAL_CLASS		"thermal_zone"
#define ACPI_THERMAL_DEVICE_NAME	"Thermal Zone"
#define ACPI_THERMAL_NOTIFY_TEMPERATURE	0x80
#define ACPI_THERMAL_NOTIFY_THRESHOLDS	0x81
#define ACPI_THERMAL_NOTIFY_DEVICES	0x82
#define ACPI_THERMAL_NOTIFY_CRITICAL	0xF0
#define ACPI_THERMAL_NOTIFY_HOT		0xF1
#define ACPI_THERMAL_MODE_ACTIVE	0x00

#define ACPI_THERMAL_MAX_ACTIVE		10
#define ACPI_THERMAL_MAX_LIMIT_STR_LEN	65

#define ACPI_THERMAL_TRIP_PASSIVE	(-1)

/*
 * This exception is thrown out in two cases:
 * 1.An invalid trip point becomes invalid or a valid trip point becomes invalid
 *   when re-evaluating the AML code.
 * 2.TODO: Devices listed in _PSL, _ALx, _TZD may change.
 *   We need to re-bind the cooling devices of a thermal zone when this occurs.
 */
#define ACPI_THERMAL_TRIPS_EXCEPTION(tz, str) \
do { \
	acpi_handle_info(tz->device->handle, \
			 "ACPI thermal trip point %s changed\n" \
			 "Please report to linux-acpi@vger.kernel.org\n", str); \
} while (0)

static int act;
module_param(act, int, 0644);
MODULE_PARM_DESC(act, "Disable or override all lowest active trip points.");

static int crt;
module_param(crt, int, 0644);
MODULE_PARM_DESC(crt, "Disable or lower all critical trip points.");

static int tzp;
module_param(tzp, int, 0444);
MODULE_PARM_DESC(tzp, "Thermal zone polling frequency, in 1/10 seconds.");

static int off;
module_param(off, int, 0);
MODULE_PARM_DESC(off, "Set to disable ACPI thermal support.");

static int psv;
module_param(psv, int, 0644);
MODULE_PARM_DESC(psv, "Disable or override all passive trip points.");

static struct workqueue_struct *acpi_thermal_pm_queue;

struct acpi_thermal_trip {
	unsigned long temperature;
	struct acpi_handle_list devices;
};

struct acpi_thermal_passive {
	struct acpi_thermal_trip trip;
	unsigned long tc1;
	unsigned long tc2;
	unsigned long tsp;
};

struct acpi_thermal_active {
	struct acpi_thermal_trip trip;
};

struct acpi_thermal_trips {
	struct acpi_thermal_passive passive;
	struct acpi_thermal_active active[ACPI_THERMAL_MAX_ACTIVE];
	bool critical_valid;
	bool hot_valid;
};

struct acpi_thermal {
	struct acpi_device *device;
	acpi_bus_id name;
	unsigned long temperature;
	unsigned long last_temperature;
	unsigned long polling_frequency;
	volatile u8 zombie;
	struct acpi_thermal_trips trips;
	struct thermal_trip *trip_table;
	struct acpi_handle_list devices;
	struct thermal_zone_device *thermal_zone;
	int kelvin_offset;	/* in millidegrees */
	struct work_struct thermal_check_work;
	struct mutex thermal_check_lock;
	refcount_t thermal_check_count;
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

	acpi_handle_debug(tz->device->handle, "Temperature is %lu dK\n",
			  tz->temperature);

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
	acpi_handle_debug(tz->device->handle, "Polling frequency is %lu dS\n",
			  tz->polling_frequency);

	return 0;
}

static int acpi_thermal_temp(struct acpi_thermal *tz, int temp_deci_k)
{
	if (temp_deci_k == THERMAL_TEMP_INVALID)
		return THERMAL_TEMP_INVALID;

	return deci_kelvin_to_millicelsius_with_offset(temp_deci_k,
						       tz->kelvin_offset);
}

static bool acpi_thermal_trip_valid(struct acpi_thermal_trip *acpi_trip)
{
	return acpi_trip->temperature != THERMAL_TEMP_INVALID;
}

static long get_passive_temp(struct acpi_thermal *tz)
{
	unsigned long long tmp;
	acpi_status status;

	status = acpi_evaluate_integer(tz->device->handle, "_PSV", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return THERMAL_TEMP_INVALID;

	return tmp;
}

static void acpi_thermal_update_passive_trip(struct acpi_thermal *tz)
{
	struct acpi_thermal_trip *acpi_trip = &tz->trips.passive.trip;

	if (!acpi_thermal_trip_valid(acpi_trip) || psv > 0)
		return;

	acpi_trip->temperature = get_passive_temp(tz);
	if (!acpi_thermal_trip_valid(acpi_trip))
		ACPI_THERMAL_TRIPS_EXCEPTION(tz, "state");
}

static bool update_trip_devices(struct acpi_thermal *tz,
				struct acpi_thermal_trip *acpi_trip,
				int index, bool compare)
{
	struct acpi_handle_list devices = { 0 };
	char method[] = "_PSL";
	acpi_status status;

	if (index != ACPI_THERMAL_TRIP_PASSIVE) {
		method[1] = 'A';
		method[2] = 'L';
		method[3] = '0' + index;
	}

	status = acpi_evaluate_reference(tz->device->handle, method, NULL, &devices);
	if (ACPI_FAILURE(status)) {
		acpi_handle_info(tz->device->handle, "%s evaluation failure\n", method);
		return false;
	}

	if (acpi_handle_list_equal(&acpi_trip->devices, &devices)) {
		acpi_handle_list_free(&devices);
		return true;
	}

	if (compare)
		ACPI_THERMAL_TRIPS_EXCEPTION(tz, "device");

	acpi_handle_list_replace(&acpi_trip->devices, &devices);
	return true;
}

static void acpi_thermal_update_trip_devices(struct acpi_thermal *tz, int index)
{
	struct acpi_thermal_trip *acpi_trip;

	acpi_trip = index == ACPI_THERMAL_TRIP_PASSIVE ?
			&tz->trips.passive.trip : &tz->trips.active[index].trip;
	if (!acpi_thermal_trip_valid(acpi_trip))
		return;

	if (update_trip_devices(tz, acpi_trip, index, true)) {
		return;
	}

	acpi_trip->temperature = THERMAL_TEMP_INVALID;
	ACPI_THERMAL_TRIPS_EXCEPTION(tz, "state");
}

static long get_active_temp(struct acpi_thermal *tz, int index)
{
	char method[] = { '_', 'A', 'C', '0' + index, '\0' };
	unsigned long long tmp;
	acpi_status status;

	status = acpi_evaluate_integer(tz->device->handle, method, NULL, &tmp);
	if (ACPI_FAILURE(status))
		return THERMAL_TEMP_INVALID;

	/*
	 * If an override has been provided, apply it so there are no active
	 * trips with thresholds greater than the override.
	 */
	if (act > 0) {
		unsigned long long override = celsius_to_deci_kelvin(act);

		if (tmp > override)
			tmp = override;
	}
	return tmp;
}

static void acpi_thermal_update_active_trip(struct acpi_thermal *tz, int index)
{
	struct acpi_thermal_trip *acpi_trip = &tz->trips.active[index].trip;

	if (!acpi_thermal_trip_valid(acpi_trip))
		return;

	acpi_trip->temperature = get_active_temp(tz, index);
	if (!acpi_thermal_trip_valid(acpi_trip))
		ACPI_THERMAL_TRIPS_EXCEPTION(tz, "state");
}

static int acpi_thermal_adjust_trip(struct thermal_trip *trip, void *data)
{
	struct acpi_thermal_trip *acpi_trip = trip->priv;
	struct acpi_thermal *tz = data;

	if (!acpi_trip)
		return 0;

	if (acpi_thermal_trip_valid(acpi_trip))
		trip->temperature = acpi_thermal_temp(tz, acpi_trip->temperature);
	else
		trip->temperature = THERMAL_TEMP_INVALID;

	return 0;
}

static void acpi_thermal_adjust_thermal_zone(struct thermal_zone_device *thermal,
					     unsigned long data)
{
	struct acpi_thermal *tz = thermal_zone_device_priv(thermal);
	int i;

	if (data == ACPI_THERMAL_NOTIFY_THRESHOLDS) {
		acpi_thermal_update_passive_trip(tz);
		for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++)
			acpi_thermal_update_active_trip(tz, i);
	} else {
		acpi_thermal_update_trip_devices(tz, ACPI_THERMAL_TRIP_PASSIVE);
		for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++)
			acpi_thermal_update_trip_devices(tz, i);
	}

	for_each_thermal_trip(tz->thermal_zone, acpi_thermal_adjust_trip, tz);
}

static void acpi_queue_thermal_check(struct acpi_thermal *tz)
{
	if (!work_pending(&tz->thermal_check_work))
		queue_work(acpi_thermal_pm_queue, &tz->thermal_check_work);
}

static void acpi_thermal_trips_update(struct acpi_thermal *tz, u32 event)
{
	struct acpi_device *adev = tz->device;

	/*
	 * Use thermal_zone_device_exec() to carry out the trip points
	 * update, so as to protect thermal_get_trend() from getting stale
	 * trip point temperatures and to prevent thermal_zone_device_update()
	 * invoked from acpi_thermal_check_fn() from producing inconsistent
	 * results.
	 */
	thermal_zone_device_exec(tz->thermal_zone,
				 acpi_thermal_adjust_thermal_zone, event);
	acpi_queue_thermal_check(tz);
	acpi_bus_generate_netlink_event(adev->pnp.device_class,
					dev_name(&adev->dev), event, 0);
}

static long acpi_thermal_get_critical_trip(struct acpi_thermal *tz)
{
	unsigned long long tmp;
	acpi_status status;

	if (crt > 0) {
		tmp = celsius_to_deci_kelvin(crt);
		goto set;
	}
	if (crt == -1) {
		acpi_handle_debug(tz->device->handle, "Critical threshold disabled\n");
		goto fail;
	}

	status = acpi_evaluate_integer(tz->device->handle, "_CRT", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(tz->device->handle, "No critical threshold\n");
		goto fail;
	}
	if (tmp <= 2732) {
		/*
		 * Below zero (Celsius) values clearly aren't right for sure,
		 * so discard them as invalid.
		 */
		pr_info(FW_BUG "Invalid critical threshold (%llu)\n", tmp);
		goto fail;
	}

set:
	tz->trips.critical_valid = true;
	acpi_handle_debug(tz->device->handle, "Critical threshold [%llu]\n", tmp);
	return tmp;

fail:
	tz->trips.critical_valid = false;
	return THERMAL_TEMP_INVALID;
}

static long acpi_thermal_get_hot_trip(struct acpi_thermal *tz)
{
	unsigned long long tmp;
	acpi_status status;

	status = acpi_evaluate_integer(tz->device->handle, "_HOT", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		tz->trips.hot_valid = false;
		acpi_handle_debug(tz->device->handle, "No hot threshold\n");
		return THERMAL_TEMP_INVALID;
	}

	tz->trips.hot_valid = true;
	acpi_handle_debug(tz->device->handle, "Hot threshold [%llu]\n", tmp);
	return tmp;
}

static bool passive_trip_params_init(struct acpi_thermal *tz)
{
	unsigned long long tmp;
	acpi_status status;

	status = acpi_evaluate_integer(tz->device->handle, "_TC1", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return false;

	tz->trips.passive.tc1 = tmp;

	status = acpi_evaluate_integer(tz->device->handle, "_TC2", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return false;

	tz->trips.passive.tc2 = tmp;

	status = acpi_evaluate_integer(tz->device->handle, "_TSP", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return false;

	tz->trips.passive.tsp = tmp;

	return true;
}

static bool acpi_thermal_init_trip(struct acpi_thermal *tz, int index)
{
	struct acpi_thermal_trip *acpi_trip;
	long temp;

	if (index == ACPI_THERMAL_TRIP_PASSIVE) {
		acpi_trip = &tz->trips.passive.trip;

		if (psv == -1)
			goto fail;

		if (!passive_trip_params_init(tz))
			goto fail;

		temp = psv > 0 ? celsius_to_deci_kelvin(psv) :
				 get_passive_temp(tz);
	} else {
		acpi_trip = &tz->trips.active[index].trip;

		if (act == -1)
			goto fail;

		temp = get_active_temp(tz, index);
	}

	if (temp == THERMAL_TEMP_INVALID)
		goto fail;

	if (!update_trip_devices(tz, acpi_trip, index, false))
		goto fail;

	acpi_trip->temperature = temp;
	return true;

fail:
	acpi_trip->temperature = THERMAL_TEMP_INVALID;
	return false;
}

static int acpi_thermal_get_trip_points(struct acpi_thermal *tz)
{
	unsigned int count = 0;
	int i;

	if (acpi_thermal_init_trip(tz, ACPI_THERMAL_TRIP_PASSIVE))
		count++;

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		if (acpi_thermal_init_trip(tz, i))
			count++;
		else
			break;

	}

	return count;
}

/* sys I/F for generic thermal sysfs support */

static int thermal_get_temp(struct thermal_zone_device *thermal, int *temp)
{
	struct acpi_thermal *tz = thermal_zone_device_priv(thermal);
	int result;

	if (!tz)
		return -EINVAL;

	result = acpi_thermal_get_temperature(tz);
	if (result)
		return result;

	*temp = deci_kelvin_to_millicelsius_with_offset(tz->temperature,
							tz->kelvin_offset);
	return 0;
}

static int thermal_get_trend(struct thermal_zone_device *thermal,
			     const struct thermal_trip *trip,
			     enum thermal_trend *trend)
{
	struct acpi_thermal *tz = thermal_zone_device_priv(thermal);
	struct acpi_thermal_trip *acpi_trip;
	int t;

	if (!tz || !trip)
		return -EINVAL;

	acpi_trip = trip->priv;
	if (!acpi_trip || !acpi_thermal_trip_valid(acpi_trip))
		return -EINVAL;

	switch (trip->type) {
	case THERMAL_TRIP_PASSIVE:
		t = tz->trips.passive.tc1 * (tz->temperature -
						tz->last_temperature) +
			tz->trips.passive.tc2 * (tz->temperature -
						acpi_trip->temperature);
		if (t > 0)
			*trend = THERMAL_TREND_RAISING;
		else if (t < 0)
			*trend = THERMAL_TREND_DROPPING;
		else
			*trend = THERMAL_TREND_STABLE;

		return 0;

	case THERMAL_TRIP_ACTIVE:
		t = acpi_thermal_temp(tz, tz->temperature);
		if (t <= trip->temperature)
			break;

		*trend = THERMAL_TREND_RAISING;

		return 0;

	default:
		break;
	}

	return -EINVAL;
}

static void acpi_thermal_zone_device_hot(struct thermal_zone_device *thermal)
{
	struct acpi_thermal *tz = thermal_zone_device_priv(thermal);

	acpi_bus_generate_netlink_event(tz->device->pnp.device_class,
					dev_name(&tz->device->dev),
					ACPI_THERMAL_NOTIFY_HOT, 1);
}

static void acpi_thermal_zone_device_critical(struct thermal_zone_device *thermal)
{
	struct acpi_thermal *tz = thermal_zone_device_priv(thermal);

	acpi_bus_generate_netlink_event(tz->device->pnp.device_class,
					dev_name(&tz->device->dev),
					ACPI_THERMAL_NOTIFY_CRITICAL, 1);

	thermal_zone_device_critical(thermal);
}

static int acpi_thermal_cooling_device_cb(struct thermal_zone_device *thermal,
					  struct thermal_cooling_device *cdev,
					  bool bind)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_thermal *tz = thermal_zone_device_priv(thermal);
	struct acpi_thermal_trip *acpi_trip;
	struct acpi_device *dev;
	acpi_handle handle;
	int i;
	int j;
	int trip = -1;
	int result = 0;

	if (tz->trips.critical_valid)
		trip++;

	if (tz->trips.hot_valid)
		trip++;

	acpi_trip = &tz->trips.passive.trip;
	if (acpi_thermal_trip_valid(acpi_trip)) {
		trip++;
		for (i = 0; i < acpi_trip->devices.count; i++) {
			handle = acpi_trip->devices.handles[i];
			dev = acpi_fetch_acpi_dev(handle);
			if (dev != device)
				continue;

			if (bind)
				result = thermal_zone_bind_cooling_device(
						thermal, trip, cdev,
						THERMAL_NO_LIMIT,
						THERMAL_NO_LIMIT,
						THERMAL_WEIGHT_DEFAULT);
			else
				result =
					thermal_zone_unbind_cooling_device(
						thermal, trip, cdev);

			if (result)
				goto failed;
		}
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		acpi_trip = &tz->trips.active[i].trip;
		if (!acpi_thermal_trip_valid(acpi_trip))
			break;

		trip++;
		for (j = 0; j < acpi_trip->devices.count; j++) {
			handle = acpi_trip->devices.handles[j];
			dev = acpi_fetch_acpi_dev(handle);
			if (dev != device)
				continue;

			if (bind)
				result = thermal_zone_bind_cooling_device(
						thermal, trip, cdev,
						THERMAL_NO_LIMIT,
						THERMAL_NO_LIMIT,
						THERMAL_WEIGHT_DEFAULT);
			else
				result = thermal_zone_unbind_cooling_device(
						thermal, trip, cdev);

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
	.get_trend = thermal_get_trend,
	.hot = acpi_thermal_zone_device_hot,
	.critical = acpi_thermal_zone_device_critical,
};

static int acpi_thermal_zone_sysfs_add(struct acpi_thermal *tz)
{
	struct device *tzdev = thermal_zone_device(tz->thermal_zone);
	int ret;

	ret = sysfs_create_link(&tz->device->dev.kobj,
				&tzdev->kobj, "thermal_zone");
	if (ret)
		return ret;

	ret = sysfs_create_link(&tzdev->kobj,
				   &tz->device->dev.kobj, "device");
	if (ret)
		sysfs_remove_link(&tz->device->dev.kobj, "thermal_zone");

	return ret;
}

static void acpi_thermal_zone_sysfs_remove(struct acpi_thermal *tz)
{
	struct device *tzdev = thermal_zone_device(tz->thermal_zone);

	sysfs_remove_link(&tz->device->dev.kobj, "thermal_zone");
	sysfs_remove_link(&tzdev->kobj, "device");
}

static int acpi_thermal_register_thermal_zone(struct acpi_thermal *tz,
					      unsigned int trip_count,
					      int passive_delay)
{
	int result;

	tz->thermal_zone = thermal_zone_device_register_with_trips("acpitz",
								   tz->trip_table,
								   trip_count,
								   0, tz,
								   &acpi_thermal_zone_ops,
								   NULL,
								   passive_delay,
								   tz->polling_frequency * 100);
	if (IS_ERR(tz->thermal_zone))
		return PTR_ERR(tz->thermal_zone);

	result = acpi_thermal_zone_sysfs_add(tz);
	if (result)
		goto unregister_tzd;

	result = thermal_zone_device_enable(tz->thermal_zone);
	if (result)
		goto remove_links;

	dev_info(&tz->device->dev, "registered as thermal_zone%d\n",
		 thermal_zone_device_id(tz->thermal_zone));

	return 0;

remove_links:
	acpi_thermal_zone_sysfs_remove(tz);
unregister_tzd:
	thermal_zone_device_unregister(tz->thermal_zone);

	return result;
}

static void acpi_thermal_unregister_thermal_zone(struct acpi_thermal *tz)
{
	acpi_thermal_zone_sysfs_remove(tz);
	thermal_zone_device_unregister(tz->thermal_zone);
	kfree(tz->trip_table);
	tz->thermal_zone = NULL;
}


/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_thermal_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = data;
	struct acpi_thermal *tz = acpi_driver_data(device);

	if (!tz)
		return;

	switch (event) {
	case ACPI_THERMAL_NOTIFY_TEMPERATURE:
		acpi_queue_thermal_check(tz);
		break;
	case ACPI_THERMAL_NOTIFY_THRESHOLDS:
	case ACPI_THERMAL_NOTIFY_DEVICES:
		acpi_thermal_trips_update(tz, event);
		break;
	default:
		acpi_handle_debug(device->handle, "Unsupported event [0x%x]\n",
				  event);
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
static void acpi_thermal_guess_offset(struct acpi_thermal *tz, long crit_temp)
{
	if (tz->trips.critical_valid && crit_temp % 5 == 1)
		tz->kelvin_offset = 273100;
	else
		tz->kelvin_offset = 273200;
}

static void acpi_thermal_check_fn(struct work_struct *work)
{
	struct acpi_thermal *tz = container_of(work, struct acpi_thermal,
					       thermal_check_work);

	/*
	 * In general, it is not sufficient to check the pending bit, because
	 * subsequent instances of this function may be queued after one of them
	 * has started running (e.g. if _TMP sleeps).  Avoid bailing out if just
	 * one of them is running, though, because it may have done the actual
	 * check some time ago, so allow at least one of them to block on the
	 * mutex while another one is running the update.
	 */
	if (!refcount_dec_not_one(&tz->thermal_check_count))
		return;

	mutex_lock(&tz->thermal_check_lock);

	thermal_zone_device_update(tz->thermal_zone, THERMAL_EVENT_UNSPECIFIED);

	refcount_inc(&tz->thermal_check_count);

	mutex_unlock(&tz->thermal_check_lock);
}

static void acpi_thermal_free_thermal_zone(struct acpi_thermal *tz)
{
	int i;

	acpi_handle_list_free(&tz->trips.passive.trip.devices);
	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++)
		acpi_handle_list_free(&tz->trips.active[i].trip.devices);

	kfree(tz);
}

static int acpi_thermal_add(struct acpi_device *device)
{
	struct acpi_thermal_trip *acpi_trip;
	struct thermal_trip *trip;
	struct acpi_thermal *tz;
	unsigned int trip_count;
	int crit_temp, hot_temp;
	int passive_delay = 0;
	int result;
	int i;

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

	acpi_thermal_aml_dependency_fix(tz);

	/* Get trip points [_CRT, _PSV, etc.] (required). */
	trip_count = acpi_thermal_get_trip_points(tz);

	crit_temp = acpi_thermal_get_critical_trip(tz);
	if (tz->trips.critical_valid)
		trip_count++;

	hot_temp = acpi_thermal_get_hot_trip(tz);
	if (tz->trips.hot_valid)
		trip_count++;

	if (!trip_count) {
		pr_warn(FW_BUG "No valid trip points!\n");
		result = -ENODEV;
		goto free_memory;
	}

	/* Get temperature [_TMP] (required). */
	result = acpi_thermal_get_temperature(tz);
	if (result)
		goto free_memory;

	/* Set the cooling mode [_SCP] to active cooling. */
	acpi_execute_simple_method(tz->device->handle, "_SCP",
				   ACPI_THERMAL_MODE_ACTIVE);

	/* Determine the default polling frequency [_TZP]. */
	if (tzp)
		tz->polling_frequency = tzp;
	else
		acpi_thermal_get_polling_frequency(tz);

	acpi_thermal_guess_offset(tz, crit_temp);

	trip = kcalloc(trip_count, sizeof(*trip), GFP_KERNEL);
	if (!trip) {
		result = -ENOMEM;
		goto free_memory;
	}

	tz->trip_table = trip;

	if (tz->trips.critical_valid) {
		trip->type = THERMAL_TRIP_CRITICAL;
		trip->temperature = acpi_thermal_temp(tz, crit_temp);
		trip++;
	}

	if (tz->trips.hot_valid) {
		trip->type = THERMAL_TRIP_HOT;
		trip->temperature = acpi_thermal_temp(tz, hot_temp);
		trip++;
	}

	acpi_trip = &tz->trips.passive.trip;
	if (acpi_thermal_trip_valid(acpi_trip)) {
		passive_delay = tz->trips.passive.tsp * 100;

		trip->type = THERMAL_TRIP_PASSIVE;
		trip->temperature = acpi_thermal_temp(tz, acpi_trip->temperature);
		trip->priv = acpi_trip;
		trip++;
	}

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		acpi_trip =  &tz->trips.active[i].trip;

		if (!acpi_thermal_trip_valid(acpi_trip))
			break;

		trip->type = THERMAL_TRIP_ACTIVE;
		trip->temperature = acpi_thermal_temp(tz, acpi_trip->temperature);
		trip->priv = acpi_trip;
		trip++;
	}

	result = acpi_thermal_register_thermal_zone(tz, trip_count, passive_delay);
	if (result)
		goto free_trips;

	refcount_set(&tz->thermal_check_count, 3);
	mutex_init(&tz->thermal_check_lock);
	INIT_WORK(&tz->thermal_check_work, acpi_thermal_check_fn);

	pr_info("%s [%s] (%ld C)\n", acpi_device_name(device),
		acpi_device_bid(device), deci_kelvin_to_celsius(tz->temperature));

	result = acpi_dev_install_notify_handler(device, ACPI_DEVICE_NOTIFY,
						 acpi_thermal_notify, device);
	if (result)
		goto flush_wq;

	return 0;

flush_wq:
	flush_workqueue(acpi_thermal_pm_queue);
	acpi_thermal_unregister_thermal_zone(tz);
free_trips:
	kfree(tz->trip_table);
free_memory:
	acpi_thermal_free_thermal_zone(tz);

	return result;
}

static void acpi_thermal_remove(struct acpi_device *device)
{
	struct acpi_thermal *tz;

	if (!device || !acpi_driver_data(device))
		return;

	tz = acpi_driver_data(device);

	acpi_dev_remove_notify_handler(device, ACPI_DEVICE_NOTIFY,
				       acpi_thermal_notify);

	flush_workqueue(acpi_thermal_pm_queue);
	acpi_thermal_unregister_thermal_zone(tz);

	acpi_thermal_free_thermal_zone(tz);
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
	int i, j, power_state;

	if (!dev)
		return -EINVAL;

	tz = acpi_driver_data(to_acpi_device(dev));
	if (!tz)
		return -EINVAL;

	for (i = 0; i < ACPI_THERMAL_MAX_ACTIVE; i++) {
		struct acpi_thermal_trip *acpi_trip = &tz->trips.active[i].trip;

		if (!acpi_thermal_trip_valid(acpi_trip))
			break;

		for (j = 0; j < acpi_trip->devices.count; j++) {
			acpi_bus_update_power(acpi_trip->devices.handles[j],
					      &power_state);
		}
	}

	acpi_queue_thermal_check(tz);

	return AE_OK;
}
#else
#define acpi_thermal_suspend	NULL
#define acpi_thermal_resume	NULL
#endif
static SIMPLE_DEV_PM_OPS(acpi_thermal_pm, acpi_thermal_suspend, acpi_thermal_resume);

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
		},
	.drv.pm = &acpi_thermal_pm,
};

static int thermal_act(const struct dmi_system_id *d) {
	if (act == 0) {
		pr_notice("%s detected: disabling all active thermal trip points\n",
			  d->ident);
		act = -1;
	}
	return 0;
}
static int thermal_nocrt(const struct dmi_system_id *d) {
	pr_notice("%s detected: disabling all critical thermal trip point actions.\n",
		  d->ident);
	crt = -1;
	return 0;
}
static int thermal_tzp(const struct dmi_system_id *d) {
	if (tzp == 0) {
		pr_notice("%s detected: enabling thermal zone polling\n",
			  d->ident);
		tzp = 300;	/* 300 dS = 30 Seconds */
	}
	return 0;
}
static int thermal_psv(const struct dmi_system_id *d) {
	if (psv == 0) {
		pr_notice("%s detected: disabling all passive thermal trip points\n",
			  d->ident);
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
	int result;

	dmi_check_system(thermal_dmi_table);

	if (off) {
		pr_notice("thermal control disabled\n");
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
}

module_init(acpi_thermal_init);
module_exit(acpi_thermal_exit);

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Thermal Zone Driver");
MODULE_LICENSE("GPL");

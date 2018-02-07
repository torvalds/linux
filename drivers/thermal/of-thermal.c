// SPDX-License-Identifier: GPL-2.0
/*
 *  of-thermal.c - Generic Thermal Management device tree support.
 *
 *  Copyright (C) 2013 Texas Instruments
 *  Copyright (C) 2013 Eduardo Valentin <eduardo.valentin@ti.com>
 */
#include <linux/thermal.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/list.h>

#define CREATE_TRACE_POINTS
#include <trace/events/thermal_virtual.h>

#include "thermal_core.h"

/***   Private data structures to represent thermal device tree data ***/

/**
 * struct __thermal_bind_param - a match between trip and cooling device
 * @cooling_device: a pointer to identify the referred cooling device
 * @trip_id: the trip point index
 * @usage: the percentage (from 0 to 100) of cooling contribution
 * @min: minimum cooling state used at this trip point
 * @max: maximum cooling state used at this trip point
 */

struct __thermal_bind_params {
	struct device_node *cooling_device;
	unsigned int trip_id;
	unsigned int usage;
	unsigned long min;
	unsigned long max;
};

/**
 * struct __sensor_param - Holds individual sensor data
 * @sensor_data: sensor driver private data passed as input argument
 * @ops: sensor driver ops
 * @trip_high: last trip high value programmed in the sensor driver
 * @trip_low: last trip low value programmed in the sensor driver
 * @lock: mutex lock acquired before updating the trip temperatures
 * @first_tz: list head pointing the first thermal zone
 */
struct __sensor_param {
	void *sensor_data;
	const struct thermal_zone_of_device_ops *ops;
	int trip_high, trip_low;
	struct mutex lock;
	struct list_head first_tz;
};

/**
 * struct __thermal_zone - internal representation of a thermal zone
 * @mode: current thermal zone device mode (enabled/disabled)
 * @passive_delay: polling interval while passive cooling is activated
 * @polling_delay: zone polling interval
 * @slope: slope of the temperature adjustment curve
 * @offset: offset of the temperature adjustment curve
 * @default_disable: Keep the thermal zone disabled by default
 * @is_wakeable: Ignore post suspend thermal zone re-evaluation
 * @tzd: thermal zone device pointer for this sensor
 * @ntrips: number of trip points
 * @trips: an array of trip points (0..ntrips - 1)
 * @num_tbps: number of thermal bind params
 * @tbps: an array of thermal bind params (0..num_tbps - 1)
 * @senps: sensor related parameters
 * @list: sibling thermal zone
 */

struct __thermal_zone {
	enum thermal_device_mode mode;
	int passive_delay;
	int polling_delay;
	int slope;
	int offset;
	struct thermal_zone_device *tzd;
	bool default_disable;
	bool is_wakeable;

	/* trip data */
	int ntrips;
	struct thermal_trip *trips;

	/* cooling binding data */
	int num_tbps;
	struct __thermal_bind_params *tbps;

	struct list_head list;
	/* sensor interface */
	struct __sensor_param *senps;
};

/**
 * struct virtual_sensor - internal representation of a virtual thermal zone
 * @num_sensors - number of sensors this virtual sensor will reference to
 *		  estimate temperature
 * @tz - Array of thermal zones of the sensors this virtual sensor will use
 *	 to estimate temperature
 * @virt_tz - Virtual thermal zone pointer
 * @logic - aggregation logic to be used to estimate the temperature
 * @last_reading - last estimated temperature
 * @coefficients - array of coefficients to be used for weighted aggregation
 *		       logic
 * @avg_offset - offset value to be used for the weighted aggregation logic
 * @avg_denominator - denominator value to be used for the weighted aggregation
 *			logic
 */
struct virtual_sensor {
	int                        num_sensors;
	struct thermal_zone_device *tz[THERMAL_MAX_VIRT_SENSORS];
	struct thermal_zone_device *virt_tz;
	enum aggregation_logic     logic;
	int                        last_reading;
	int                        coefficients[THERMAL_MAX_VIRT_SENSORS];
	int                        avg_offset;
	int                        avg_denominator;
};


/***   DT thermal zone device callbacks   ***/

static int virt_sensor_read_temp(void *data, int *val)
{
	struct virtual_sensor *sens = data;
	int idx, temp = 0, ret = 0;

	for (idx = 0; idx < sens->num_sensors; idx++) {
		int sens_temp = 0;

		ret = thermal_zone_get_temp(sens->tz[idx], &sens_temp);
		if (ret) {
			pr_err("virt zone: sensor[%s] read error:%d\n",
				sens->tz[idx]->type, ret);
			return ret;
		}
		switch (sens->logic) {
		case VIRT_COUNT_THRESHOLD:
			if ((sens->coefficients[idx] < 0 &&
			     sens_temp < -sens->coefficients[idx]) ||
			    (sens->coefficients[idx] > 0 &&
			     sens_temp >= sens->coefficients[idx]))
				temp += 1;
			break;
		case VIRT_WEIGHTED_AVG:
			temp += sens_temp * sens->coefficients[idx];
			if (idx == (sens->num_sensors - 1))
				temp = (temp + sens->avg_offset)
					/ sens->avg_denominator;
			break;
		case VIRT_MAXIMUM:
			if (idx == 0)
				temp = INT_MIN;
			if (sens_temp > temp)
				temp = sens_temp;
			break;
		case VIRT_MINIMUM:
			if (idx == 0)
				temp = INT_MAX;
			if (sens_temp < temp)
				temp = sens_temp;
			break;
		default:
			break;
		}
		trace_virtual_temperature(sens->virt_tz, sens->tz[idx],
					sens_temp, temp);
	}

	sens->last_reading = *val = temp;

	return 0;
}

static int of_thermal_get_temp(struct thermal_zone_device *tz,
			       int *temp)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data->senps || !data->senps->ops->get_temp)
		return -EINVAL;

	return data->senps->ops->get_temp(data->senps->sensor_data, temp);
}

static int of_thermal_set_trips(struct thermal_zone_device *tz,
				int low, int high)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data->senps || !data->senps->ops->set_trips)
		return -EINVAL;

	return data->senps->ops->set_trips(data->senps->sensor_data, low, high);
}

/**
 * of_thermal_get_ntrips - function to export number of available trip
 *			   points.
 * @tz: pointer to a thermal zone
 *
 * This function is a globally visible wrapper to get number of trip points
 * stored in the local struct __thermal_zone
 *
 * Return: number of available trip points, -ENODEV when data not available
 */
int of_thermal_get_ntrips(struct thermal_zone_device *tz)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data || IS_ERR(data))
		return -ENODEV;

	return data->ntrips;
}
EXPORT_SYMBOL_GPL(of_thermal_get_ntrips);

/**
 * of_thermal_is_trip_valid - function to check if trip point is valid
 *
 * @tz:	pointer to a thermal zone
 * @trip:	trip point to evaluate
 *
 * This function is responsible for checking if passed trip point is valid
 *
 * Return: true if trip point is valid, false otherwise
 */
bool of_thermal_is_trip_valid(struct thermal_zone_device *tz, int trip)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data || trip >= data->ntrips || trip < 0)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(of_thermal_is_trip_valid);

/**
 * of_thermal_get_trip_points - function to get access to a globally exported
 *				trip points
 *
 * @tz:	pointer to a thermal zone
 *
 * This function provides a pointer to trip points table
 *
 * Return: pointer to trip points table, NULL otherwise
 */
const struct thermal_trip *
of_thermal_get_trip_points(struct thermal_zone_device *tz)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data)
		return NULL;

	return data->trips;
}
EXPORT_SYMBOL_GPL(of_thermal_get_trip_points);

/**
 * of_thermal_set_emul_temp - function to set emulated temperature
 *
 * @tz:	pointer to a thermal zone
 * @temp:	temperature to set
 *
 * This function gives the ability to set emulated value of temperature,
 * which is handy for debugging
 *
 * Return: zero on success, error code otherwise
 */
static int of_thermal_set_emul_temp(struct thermal_zone_device *tz,
				    int temp)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data->senps || !data->senps->ops->set_emul_temp)
		return -EINVAL;

	return data->senps->ops->set_emul_temp(data->senps->sensor_data, temp);
}

static int of_thermal_get_trend(struct thermal_zone_device *tz, int trip,
				enum thermal_trend *trend)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data->senps || !data->senps->ops->get_trend)
		return -EINVAL;

	return data->senps->ops->get_trend(data->senps->sensor_data,
					   trip, trend);
}

static int of_thermal_bind(struct thermal_zone_device *thermal,
			   struct thermal_cooling_device *cdev)
{
	struct __thermal_zone *data = thermal->devdata;
	int i;

	if (!data || IS_ERR(data))
		return -ENODEV;

	/* find where to bind */
	for (i = 0; i < data->num_tbps; i++) {
		struct __thermal_bind_params *tbp = data->tbps + i;

		if (tbp->cooling_device == cdev->np) {
			int ret;

			ret = thermal_zone_bind_cooling_device(thermal,
						tbp->trip_id, cdev,
						tbp->max,
						tbp->min,
						tbp->usage);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int of_thermal_unbind(struct thermal_zone_device *thermal,
			     struct thermal_cooling_device *cdev)
{
	struct __thermal_zone *data = thermal->devdata;
	int i;

	if (!data || IS_ERR(data))
		return -ENODEV;

	/* find where to unbind */
	for (i = 0; i < data->num_tbps; i++) {
		struct __thermal_bind_params *tbp = data->tbps + i;

		if (tbp->cooling_device == cdev->np) {
			int ret;

			ret = thermal_zone_unbind_cooling_device(thermal,
						tbp->trip_id, cdev);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int of_thermal_get_mode(struct thermal_zone_device *tz,
			       enum thermal_device_mode *mode)
{
	struct __thermal_zone *data = tz->devdata;

	*mode = data->mode;

	return 0;
}

static int of_thermal_set_mode(struct thermal_zone_device *tz,
			       enum thermal_device_mode mode)
{
	struct __thermal_zone *data = tz->devdata;

	mutex_lock(&tz->lock);

	if (mode == THERMAL_DEVICE_ENABLED) {
		tz->polling_delay = data->polling_delay;
		tz->passive_delay = data->passive_delay;
	} else {
		tz->polling_delay = 0;
		tz->passive_delay = 0;
	}

	mutex_unlock(&tz->lock);

	data->mode = mode;
	thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);

	return 0;
}

static int of_thermal_get_trip_type(struct thermal_zone_device *tz, int trip,
				    enum thermal_trip_type *type)
{
	struct __thermal_zone *data = tz->devdata;

	if (trip >= data->ntrips || trip < 0)
		return -EDOM;

	*type = data->trips[trip].type;

	return 0;
}

static int of_thermal_get_trip_temp(struct thermal_zone_device *tz, int trip,
				    int *temp)
{
	struct __thermal_zone *data = tz->devdata;

	if (trip >= data->ntrips || trip < 0)
		return -EDOM;

	if (data->senps && data->senps->ops &&
	    data->senps->ops->get_trip_temp) {
		int ret;

		ret = data->senps->ops->get_trip_temp(data->senps->sensor_data,
						      trip, temp);
		if (ret)
			return ret;
	} else {
		*temp = data->trips[trip].temperature;
	}

	return 0;
}

static bool of_thermal_is_trips_triggered(struct thermal_zone_device *tz,
		int temp)
{
	int tt, th, trip, last_temp;
	struct __thermal_zone *data = tz->devdata;
	bool triggered = false;

	if (!tz->tzp)
		return triggered;

	mutex_lock(&tz->lock);
	last_temp = tz->temperature;
	for (trip = 0; trip < data->ntrips; trip++) {
		if (!tz->tzp->tracks_low) {
			tt = data->trips[trip].temperature;
			if (temp >= tt && last_temp < tt) {
				triggered = true;
				break;
			}
			th = tt - data->trips[trip].hysteresis;
			if (temp <= th && last_temp > th) {
				triggered = true;
				break;
			}
		} else {
			tt = data->trips[trip].temperature;
			if (temp <= tt && last_temp > tt) {
				triggered = true;
				break;
			}
			th = tt + data->trips[trip].hysteresis;
			if (temp >= th && last_temp < th) {
				triggered = true;
				break;
			}
		}
	}
	mutex_unlock(&tz->lock);

	return triggered;
}

static int of_thermal_set_trip_temp(struct thermal_zone_device *tz, int trip,
				    int temp)
{
	struct __thermal_zone *data = tz->devdata;

	if (trip >= data->ntrips || trip < 0)
		return -EDOM;

	if (data->senps && data->senps->ops->set_trip_temp) {
		int ret;

		ret = data->senps->ops->set_trip_temp(data->senps->sensor_data,
						      trip, temp);
		if (ret)
			return ret;
	}

	/* thermal framework should take care of data->mask & (1 << trip) */
	data->trips[trip].temperature = temp;

	return 0;
}

static int of_thermal_get_trip_hyst(struct thermal_zone_device *tz, int trip,
				    int *hyst)
{
	struct __thermal_zone *data = tz->devdata;

	if (trip >= data->ntrips || trip < 0)
		return -EDOM;

	*hyst = data->trips[trip].hysteresis;

	return 0;
}

static int of_thermal_set_trip_hyst(struct thermal_zone_device *tz, int trip,
				    int hyst)
{
	struct __thermal_zone *data = tz->devdata;

	if (trip >= data->ntrips || trip < 0)
		return -EDOM;

	/* thermal framework should take care of data->mask & (1 << trip) */
	data->trips[trip].hysteresis = hyst;

	return 0;
}

static int of_thermal_get_crit_temp(struct thermal_zone_device *tz,
				    int *temp)
{
	struct __thermal_zone *data = tz->devdata;
	int i;

	for (i = 0; i < data->ntrips; i++)
		if (data->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = data->trips[i].temperature;
			return 0;
		}

	return -EINVAL;
}

static bool of_thermal_is_wakeable(struct thermal_zone_device *tz)
{
	struct __thermal_zone *data = tz->devdata;

	return data->is_wakeable;
}

static void handle_thermal_trip(struct thermal_zone_device *tz,
		bool temp_valid, int trip_temp)
{
	struct thermal_zone_device *zone;
	struct __thermal_zone *data;
	struct list_head *head;

	if (!tz || !tz->devdata)
		return;

	data = tz->devdata;

	if (!data->senps)
		return;

	head = &data->senps->first_tz;

	list_for_each_entry(data, head, list) {
		zone = data->tzd;
		if (data->mode == THERMAL_DEVICE_DISABLED)
			continue;
		if (!temp_valid) {
			thermal_zone_device_update(zone,
				THERMAL_EVENT_UNSPECIFIED);
		} else {
			if (!of_thermal_is_trips_triggered(zone, trip_temp))
				continue;
			thermal_zone_device_update_temp(zone,
				THERMAL_EVENT_UNSPECIFIED, trip_temp);
		}
	}
}

/*
 * of_thermal_handle_trip_temp - Handle thermal trip from sensors
 *
 * @tz: pointer to the primary thermal zone.
 * @trip_temp: The temperature
 */
void of_thermal_handle_trip_temp(struct thermal_zone_device *tz,
		int trip_temp)
{
	return handle_thermal_trip(tz, true, trip_temp);
}
EXPORT_SYMBOL_GPL(of_thermal_handle_trip_temp);

/*
 * of_thermal_handle_trip - Handle thermal trip from sensors
 *
 * @tz: pointer to the primary thermal zone.
 */
void of_thermal_handle_trip(struct thermal_zone_device *tz)
{
	return handle_thermal_trip(tz, false, 0);
}
EXPORT_SYMBOL_GPL(of_thermal_handle_trip);

static struct thermal_zone_device_ops of_thermal_ops = {
	.get_mode = of_thermal_get_mode,
	.set_mode = of_thermal_set_mode,

	.get_trip_type = of_thermal_get_trip_type,
	.get_trip_temp = of_thermal_get_trip_temp,
	.set_trip_temp = of_thermal_set_trip_temp,
	.get_trip_hyst = of_thermal_get_trip_hyst,
	.set_trip_hyst = of_thermal_set_trip_hyst,
	.get_crit_temp = of_thermal_get_crit_temp,

	.bind = of_thermal_bind,
	.unbind = of_thermal_unbind,

	.is_wakeable = of_thermal_is_wakeable,
};

static struct thermal_zone_of_device_ops of_virt_ops = {
	.get_temp = virt_sensor_read_temp,
};

/***   sensor API   ***/

static struct thermal_zone_device *
thermal_zone_of_add_sensor(struct device_node *zone,
			   struct device_node *sensor,
			   struct __sensor_param *sens_param)
{
	struct thermal_zone_device *tzd;
	struct __thermal_zone *tz;

	tzd = thermal_zone_get_zone_by_name(zone->name);
	if (IS_ERR(tzd))
		return ERR_PTR(-EPROBE_DEFER);

	tz = tzd->devdata;

	if (!sens_param->ops)
		return ERR_PTR(-EINVAL);

	mutex_lock(&tzd->lock);
	tz->senps = sens_param;

	tzd->ops->get_temp = of_thermal_get_temp;
	tzd->ops->get_trend = of_thermal_get_trend;

	/*
	 * The thermal zone core will calculate the window if they have set the
	 * optional set_trips pointer.
	 */
	if (sens_param->ops->set_trips)
		tzd->ops->set_trips = of_thermal_set_trips;

	if (sens_param->ops->set_emul_temp)
		tzd->ops->set_emul_temp = of_thermal_set_emul_temp;

	mutex_unlock(&tzd->lock);

	return tzd;
}

/**
 * thermal_zone_of_sensor_register - registers a sensor to a DT thermal zone
 * @dev: a valid struct device pointer of a sensor device. Must contain
 *       a valid .of_node, for the sensor node.
 * @sensor_id: a sensor identifier, in case the sensor IP has more
 *             than one sensors
 * @data: a private pointer (owned by the caller) that will be passed
 *        back, when a temperature reading is needed.
 * @ops: struct thermal_zone_of_device_ops *. Must contain at least .get_temp.
 *
 * This function will search the list of thermal zones described in device
 * tree and look for the zone that refer to the sensor device pointed by
 * @dev->of_node as temperature providers. For the zone pointing to the
 * sensor node, the sensor will be added to the DT thermal zone device.
 *
 * The thermal zone temperature is provided by the @get_temp function
 * pointer. When called, it will have the private pointer @data back.
 *
 * The thermal zone temperature trend is provided by the @get_trend function
 * pointer. When called, it will have the private pointer @data back.
 *
 * TODO:
 * 01 - This function must enqueue the new sensor instead of using
 * it as the only source of temperature values.
 *
 * 02 - There must be a way to match the sensor with all thermal zones
 * that refer to it.
 *
 * Return: On success returns a valid struct thermal_zone_device,
 * otherwise, it returns a corresponding ERR_PTR(). Caller must
 * check the return value with help of IS_ERR() helper.
 */
struct thermal_zone_device *
thermal_zone_of_sensor_register(struct device *dev, int sensor_id, void *data,
				const struct thermal_zone_of_device_ops *ops)
{
	struct device_node *np, *child, *sensor_np;
	struct thermal_zone_device *tzd = ERR_PTR(-ENODEV);
	struct __sensor_param *sens_param = NULL;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np)
		return ERR_PTR(-ENODEV);

	if (!dev || !dev->of_node) {
		of_node_put(np);
		return ERR_PTR(-EINVAL);
	}

	sens_param = kzalloc(sizeof(*sens_param), GFP_KERNEL);
	if (!sens_param) {
		of_node_put(np);
		return ERR_PTR(-ENOMEM);
	}
	sens_param->sensor_data = data;
	sens_param->ops = ops;
	sensor_np = of_node_get(dev->of_node);

	for_each_available_child_of_node(np, child) {
		struct of_phandle_args sensor_specs;
		int ret, id;

		/* For now, thermal framework supports only 1 sensor per zone */
		ret = of_parse_phandle_with_args(child, "thermal-sensors",
						 "#thermal-sensor-cells",
						 0, &sensor_specs);
		if (ret)
			continue;

		if (sensor_specs.args_count >= 1) {
			id = sensor_specs.args[0];
			WARN(sensor_specs.args_count > 1,
			     "%s: too many cells in sensor specifier %d\n",
			     sensor_specs.np->name, sensor_specs.args_count);
		} else {
			id = 0;
		}

		if (sensor_specs.np == sensor_np && id == sensor_id) {
			tzd = thermal_zone_of_add_sensor(child, sensor_np,
							 sens_param);
			if (!IS_ERR(tzd))
				tzd->ops->set_mode(tzd, THERMAL_DEVICE_ENABLED);

			of_node_put(sensor_specs.np);
			of_node_put(child);
			goto exit;
		}
		of_node_put(sensor_specs.np);
	}
exit:
	of_node_put(sensor_np);
	of_node_put(np);

	if (tzd == ERR_PTR(-ENODEV))
		kfree(sens_param);
	return tzd;
}
EXPORT_SYMBOL_GPL(thermal_zone_of_sensor_register);

/**
 * thermal_zone_of_sensor_unregister - unregisters a sensor from a DT thermal zone
 * @dev: a valid struct device pointer of a sensor device. Must contain
 *       a valid .of_node, for the sensor node.
 * @tzd: a pointer to struct thermal_zone_device where the sensor is registered.
 *
 * This function removes the sensor callbacks and private data from the
 * thermal zone device registered with thermal_zone_of_sensor_register()
 * API. It will also silent the zone by remove the .get_temp() and .get_trend()
 * thermal zone device callbacks.
 *
 * TODO: When the support to several sensors per zone is added, this
 * function must search the sensor list based on @dev parameter.
 *
 */
void thermal_zone_of_sensor_unregister(struct device *dev,
				       struct thermal_zone_device *tzd)
{
	struct __thermal_zone *tz;

	if (!dev || !tzd || !tzd->devdata)
		return;

	tz = tzd->devdata;

	/* no __thermal_zone, nothing to be done */
	if (!tz)
		return;

	mutex_lock(&tzd->lock);
	tzd->ops->get_temp = NULL;
	tzd->ops->get_trend = NULL;
	tzd->ops->set_emul_temp = NULL;

	kfree(tz->senps);
	tz->senps = NULL;
	mutex_unlock(&tzd->lock);
}
EXPORT_SYMBOL_GPL(thermal_zone_of_sensor_unregister);

static void devm_thermal_zone_of_sensor_release(struct device *dev, void *res)
{
	thermal_zone_of_sensor_unregister(dev,
					  *(struct thermal_zone_device **)res);
}

static int devm_thermal_zone_of_sensor_match(struct device *dev, void *res,
					     void *data)
{
	struct thermal_zone_device **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

/**
 * devm_thermal_zone_of_sensor_register - Resource managed version of
 *				thermal_zone_of_sensor_register()
 * @dev: a valid struct device pointer of a sensor device. Must contain
 *       a valid .of_node, for the sensor node.
 * @sensor_id: a sensor identifier, in case the sensor IP has more
 *	       than one sensors
 * @data: a private pointer (owned by the caller) that will be passed
 *	  back, when a temperature reading is needed.
 * @ops: struct thermal_zone_of_device_ops *. Must contain at least .get_temp.
 *
 * Refer thermal_zone_of_sensor_register() for more details.
 *
 * Return: On success returns a valid struct thermal_zone_device,
 * otherwise, it returns a corresponding ERR_PTR(). Caller must
 * check the return value with help of IS_ERR() helper.
 * Registered thermal_zone_device device will automatically be
 * released when device is unbounded.
 */
struct thermal_zone_device *devm_thermal_zone_of_sensor_register(
	struct device *dev, int sensor_id,
	void *data, const struct thermal_zone_of_device_ops *ops)
{
	struct thermal_zone_device **ptr, *tzd;

	ptr = devres_alloc(devm_thermal_zone_of_sensor_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	tzd = thermal_zone_of_sensor_register(dev, sensor_id, data, ops);
	if (IS_ERR(tzd)) {
		devres_free(ptr);
		return tzd;
	}

	*ptr = tzd;
	devres_add(dev, ptr);

	return tzd;
}
EXPORT_SYMBOL_GPL(devm_thermal_zone_of_sensor_register);

/**
 * devm_thermal_zone_of_sensor_unregister - Resource managed version of
 *				thermal_zone_of_sensor_unregister().
 * @dev: Device for which which resource was allocated.
 * @tzd: a pointer to struct thermal_zone_device where the sensor is registered.
 *
 * This function removes the sensor callbacks and private data from the
 * thermal zone device registered with devm_thermal_zone_of_sensor_register()
 * API. It will also silent the zone by remove the .get_temp() and .get_trend()
 * thermal zone device callbacks.
 * Normally this function will not need to be called and the resource
 * management code will ensure that the resource is freed.
 */
void devm_thermal_zone_of_sensor_unregister(struct device *dev,
					    struct thermal_zone_device *tzd)
{
	WARN_ON(devres_release(dev, devm_thermal_zone_of_sensor_release,
			       devm_thermal_zone_of_sensor_match, tzd));
}
EXPORT_SYMBOL_GPL(devm_thermal_zone_of_sensor_unregister);

/**
 * devm_thermal_of_virtual_sensor_register - Register a virtual sensor.
 *	Three types of virtual sensors are supported.
 *	1. Weighted aggregation type:
 *		Virtual sensor of this type calculates the weighted aggregation
 *		of sensor temperatures using the below formula,
 *		temp = (sensor_1_temp * coeff_1 + ... + sensor_n_temp * coeff_n)
 *			+ avg_offset / avg_denominator
 *		So the sensor drivers has to specify n+2 coefficients.
 *	2. Maximum type:
 *		Virtual sensors of this type will report the maximum of all
 *		sensor temperatures.
 *	3. Minimum type:
 *		Virtual sensors of this type will report the minimum of all
 *		sensor temperatures.
 *
 * @input arguments:
 * @dev: Virtual sensor driver device pointer.
 * @sensor_data: Virtual sensor data supported for the device.
 *
 * @return: Returns a virtual thermal zone pointer. Returns error if thermal
 * zone is not created. Returns -EAGAIN, if the sensor that is required for
 * this virtual sensor temperature estimation is not registered yet. The
 * sensor driver can try again later.
 */
struct thermal_zone_device *devm_thermal_of_virtual_sensor_register(
		struct device *dev,
		const struct virtual_sensor_data *sensor_data)
{
	int sens_idx = 0;
	struct virtual_sensor *sens;
	struct __thermal_zone *tz;
	struct thermal_zone_device **ptr;
	struct thermal_zone_device *tzd;
	struct __sensor_param *sens_param = NULL;
	enum thermal_device_mode mode;

	if (!dev || !sensor_data)
		return ERR_PTR(-EINVAL);

	tzd = thermal_zone_get_zone_by_name(
				sensor_data->virt_zone_name);
	if (IS_ERR(tzd)) {
		dev_dbg(dev, "sens:%s not available err: %ld\n",
				sensor_data->virt_zone_name,
				PTR_ERR(tzd));
		return tzd;
	}

	mutex_lock(&tzd->lock);
	/*
	 * Check if the virtual zone is registered and enabled.
	 * If so return the registered thermal zone.
	 */
	tzd->ops->get_mode(tzd, &mode);
	mutex_unlock(&tzd->lock);
	if (mode == THERMAL_DEVICE_ENABLED)
		return tzd;

	sens = devm_kzalloc(dev, sizeof(*sens), GFP_KERNEL);
	if (!sens)
		return ERR_PTR(-ENOMEM);

	sens->virt_tz = tzd;
	sens->logic = sensor_data->logic;
	sens->num_sensors = sensor_data->num_sensors;
	if ((sens->logic == VIRT_WEIGHTED_AVG) ||
	    (sens->logic == VIRT_COUNT_THRESHOLD)) {
		int coeff_ct = sensor_data->coefficient_ct;

		/*
		 * For weighted aggregation, sensor drivers has to specify
		 * n+2 coefficients.
		 */
		if (coeff_ct != sens->num_sensors) {
			dev_err(dev, "sens:%s invalid coefficient\n",
					sensor_data->virt_zone_name);
			return ERR_PTR(-EINVAL);
		}
		memcpy(sens->coefficients, sensor_data->coefficients,
			       coeff_ct * sizeof(*sens->coefficients));
		sens->avg_offset = sensor_data->avg_offset;
		sens->avg_denominator = sensor_data->avg_denominator;
	}

	for (sens_idx = 0; sens_idx < sens->num_sensors; sens_idx++) {
		sens->tz[sens_idx] = thermal_zone_get_zone_by_name(
					sensor_data->sensor_names[sens_idx]);
		if (IS_ERR(sens->tz[sens_idx])) {
			dev_err(dev, "sens:%s sensor[%s] fetch err:%ld\n",
				     sensor_data->virt_zone_name,
				     sensor_data->sensor_names[sens_idx],
				     PTR_ERR(sens->tz[sens_idx]));
			break;
		}
	}
	if (sens->num_sensors != sens_idx)
		return ERR_PTR(-EAGAIN);

	sens_param = kzalloc(sizeof(*sens_param), GFP_KERNEL);
	if (!sens_param)
		return ERR_PTR(-ENOMEM);
	sens_param->sensor_data = sens;
	sens_param->ops = &of_virt_ops;
	INIT_LIST_HEAD(&sens_param->first_tz);
	sens_param->trip_high = INT_MAX;
	sens_param->trip_low = INT_MIN;
	mutex_init(&sens_param->lock);

	mutex_lock(&tzd->lock);
	tz = tzd->devdata;
	tz->senps = sens_param;
	tzd->ops->get_temp = of_thermal_get_temp;
	list_add_tail(&tz->list, &sens_param->first_tz);
	mutex_unlock(&tzd->lock);

	ptr = devres_alloc(devm_thermal_zone_of_sensor_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	*ptr = tzd;
	devres_add(dev, ptr);

	if (!tz->default_disable)
		tzd->ops->set_mode(tzd, THERMAL_DEVICE_ENABLED);

	return tzd;
}
EXPORT_SYMBOL(devm_thermal_of_virtual_sensor_register);


/***   functions parsing device tree nodes   ***/

/**
 * thermal_of_populate_bind_params - parse and fill cooling map data
 * @np: DT node containing a cooling-map node
 * @__tbp: data structure to be filled with cooling map info
 * @trips: array of thermal zone trip points
 * @ntrips: number of trip points inside trips.
 *
 * This function parses a cooling-map type of node represented by
 * @np parameter and fills the read data into @__tbp data structure.
 * It needs the already parsed array of trip points of the thermal zone
 * in consideration.
 *
 * Return: 0 on success, proper error code otherwise
 */
static int thermal_of_populate_bind_params(struct device_node *np,
					   struct __thermal_bind_params *__tbp,
					   struct thermal_trip *trips,
					   int ntrips)
{
	struct of_phandle_args cooling_spec;
	struct device_node *trip;
	int ret, i;
	u32 prop;

	/* Default weight. Usage is optional */
	__tbp->usage = THERMAL_WEIGHT_DEFAULT;
	ret = of_property_read_u32(np, "contribution", &prop);
	if (ret == 0)
		__tbp->usage = prop;

	trip = of_parse_phandle(np, "trip", 0);
	if (!trip) {
		pr_err("missing trip property\n");
		return -ENODEV;
	}

	/* match using device_node */
	for (i = 0; i < ntrips; i++)
		if (trip == trips[i].np) {
			__tbp->trip_id = i;
			break;
		}

	if (i == ntrips) {
		ret = -ENODEV;
		goto end;
	}

	ret = of_parse_phandle_with_args(np, "cooling-device", "#cooling-cells",
					 0, &cooling_spec);
	if (ret < 0) {
		pr_err("missing cooling_device property\n");
		goto end;
	}
	__tbp->cooling_device = cooling_spec.np;
	if (cooling_spec.args_count >= 2) { /* at least min and max */
		__tbp->min = cooling_spec.args[0];
		__tbp->max = cooling_spec.args[1];
	} else {
		pr_err("wrong reference to cooling device, missing limits\n");
	}

end:
	of_node_put(trip);

	return ret;
}

/**
 * It maps 'enum thermal_trip_type' found in include/linux/thermal.h
 * into the device tree binding of 'trip', property type.
 */
static const char * const trip_types[] = {
	[THERMAL_TRIP_ACTIVE]	= "active",
	[THERMAL_TRIP_PASSIVE]	= "passive",
	[THERMAL_TRIP_HOT]	= "hot",
	[THERMAL_TRIP_CRITICAL]	= "critical",
};

/**
 * thermal_of_get_trip_type - Get phy mode for given device_node
 * @np:	Pointer to the given device_node
 * @type: Pointer to resulting trip type
 *
 * The function gets trip type string from property 'type',
 * and store its index in trip_types table in @type,
 *
 * Return: 0 on success, or errno in error case.
 */
static int thermal_of_get_trip_type(struct device_node *np,
				    enum thermal_trip_type *type)
{
	const char *t;
	int err, i;

	err = of_property_read_string(np, "type", &t);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(trip_types); i++)
		if (!strcasecmp(t, trip_types[i])) {
			*type = i;
			return 0;
		}

	return -ENODEV;
}

/**
 * thermal_of_populate_trip - parse and fill one trip point data
 * @np: DT node containing a trip point node
 * @trip: trip point data structure to be filled up
 *
 * This function parses a trip point type of node represented by
 * @np parameter and fills the read data into @trip data structure.
 *
 * Return: 0 on success, proper error code otherwise
 */
static int thermal_of_populate_trip(struct device_node *np,
				    struct thermal_trip *trip)
{
	int prop;
	int ret;

	ret = of_property_read_u32(np, "temperature", &prop);
	if (ret < 0) {
		pr_err("missing temperature property\n");
		return ret;
	}
	trip->temperature = prop;

	ret = of_property_read_u32(np, "hysteresis", &prop);
	if (ret < 0) {
		pr_err("missing hysteresis property\n");
		return ret;
	}
	trip->hysteresis = prop;

	ret = thermal_of_get_trip_type(np, &trip->type);
	if (ret < 0) {
		pr_err("wrong trip type property\n");
		return ret;
	}

	/* Required for cooling map matching */
	trip->np = np;
	of_node_get(np);

	return 0;
}

/**
 * thermal_of_build_thermal_zone - parse and fill one thermal zone data
 * @np: DT node containing a thermal zone node
 *
 * This function parses a thermal zone type of node represented by
 * @np parameter and fills the read data into a __thermal_zone data structure
 * and return this pointer.
 *
 * TODO: Missing properties to parse: thermal-sensor-names
 *
 * Return: On success returns a valid struct __thermal_zone,
 * otherwise, it returns a corresponding ERR_PTR(). Caller must
 * check the return value with help of IS_ERR() helper.
 */
static struct __thermal_zone
__init *thermal_of_build_thermal_zone(struct device_node *np)
{
	struct device_node *child = NULL, *gchild;
	struct __thermal_zone *tz;
	int ret, i;
	u32 prop, coef[2];

	if (!np) {
		pr_err("no thermal zone np\n");
		return ERR_PTR(-EINVAL);
	}

	tz = kzalloc(sizeof(*tz), GFP_KERNEL);
	if (!tz)
		return ERR_PTR(-ENOMEM);

	ret = of_property_read_u32(np, "polling-delay-passive", &prop);
	if (ret < 0) {
		pr_err("missing polling-delay-passive property\n");
		goto free_tz;
	}
	tz->passive_delay = prop;

	ret = of_property_read_u32(np, "polling-delay", &prop);
	if (ret < 0) {
		pr_err("missing polling-delay property\n");
		goto free_tz;
	}
	tz->polling_delay = prop;

	tz->is_wakeable = of_property_read_bool(np,
					"wake-capable-sensor");
	/*
	 * REVIST: for now, the thermal framework supports only
	 * one sensor per thermal zone. Thus, we are considering
	 * only the first two values as slope and offset.
	 */
	ret = of_property_read_u32_array(np, "coefficients", coef, 2);
	if (ret == 0) {
		tz->slope = coef[0];
		tz->offset = coef[1];
	} else {
		tz->slope = 1;
		tz->offset = 0;
	}

	/* trips */
	child = of_get_child_by_name(np, "trips");

	/* No trips provided */
	if (!child)
		goto finish;

	tz->ntrips = of_get_child_count(child);
	if (tz->ntrips == 0) /* must have at least one child */
		goto finish;

	tz->trips = kcalloc(tz->ntrips, sizeof(*tz->trips), GFP_KERNEL);
	if (!tz->trips) {
		ret = -ENOMEM;
		goto free_tz;
	}

	i = 0;
	for_each_child_of_node(child, gchild) {
		ret = thermal_of_populate_trip(gchild, &tz->trips[i++]);
		if (ret)
			goto free_trips;
	}

	of_node_put(child);

	/* cooling-maps */
	child = of_get_child_by_name(np, "cooling-maps");

	/* cooling-maps not provided */
	if (!child)
		goto finish;

	tz->num_tbps = of_get_child_count(child);
	if (tz->num_tbps == 0)
		goto finish;

	tz->tbps = kcalloc(tz->num_tbps, sizeof(*tz->tbps), GFP_KERNEL);
	if (!tz->tbps) {
		ret = -ENOMEM;
		goto free_trips;
	}

	i = 0;
	for_each_child_of_node(child, gchild) {
		ret = thermal_of_populate_bind_params(gchild, &tz->tbps[i++],
						      tz->trips, tz->ntrips);
		if (ret)
			goto free_tbps;
	}

finish:
	of_node_put(child);
	tz->mode = THERMAL_DEVICE_DISABLED;

	return tz;

free_tbps:
	for (i = i - 1; i >= 0; i--)
		of_node_put(tz->tbps[i].cooling_device);
	kfree(tz->tbps);
free_trips:
	for (i = 0; i < tz->ntrips; i++)
		of_node_put(tz->trips[i].np);
	kfree(tz->trips);
	of_node_put(gchild);
free_tz:
	kfree(tz);
	of_node_put(child);

	return ERR_PTR(ret);
}

static inline void of_thermal_free_zone(struct __thermal_zone *tz)
{
	int i;

	for (i = 0; i < tz->num_tbps; i++)
		of_node_put(tz->tbps[i].cooling_device);
	kfree(tz->tbps);
	for (i = 0; i < tz->ntrips; i++)
		of_node_put(tz->trips[i].np);
	kfree(tz->trips);
	kfree(tz);
}

/**
 * of_parse_thermal_zones - parse device tree thermal data
 *
 * Initialization function that can be called by machine initialization
 * code to parse thermal data and populate the thermal framework
 * with hardware thermal zones info. This function only parses thermal zones.
 * Cooling devices and sensor devices nodes are supposed to be parsed
 * by their respective drivers.
 *
 * Return: 0 on success, proper error code otherwise
 *
 */
int __init of_parse_thermal_zones(void)
{
	struct device_node *np, *child;
	struct __thermal_zone *tz;
	struct thermal_zone_device_ops *ops;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np) {
		pr_debug("unable to find thermal zones\n");
		return 0; /* Run successfully on systems without thermal DT */
	}

	for_each_available_child_of_node(np, child) {
		struct thermal_zone_device *zone;
		struct thermal_zone_params *tzp;
		int i, mask = 0;
		u32 prop;

		tz = thermal_of_build_thermal_zone(child);
		if (IS_ERR(tz)) {
			pr_err("failed to build thermal zone %s: %ld\n",
			       child->name,
			       PTR_ERR(tz));
			continue;
		}

		ops = kmemdup(&of_thermal_ops, sizeof(*ops), GFP_KERNEL);
		if (!ops)
			goto exit_free;

		tzp = kzalloc(sizeof(*tzp), GFP_KERNEL);
		if (!tzp) {
			kfree(ops);
			goto exit_free;
		}

		/* No hwmon because there might be hwmon drivers registering */
		tzp->no_hwmon = true;

		if (!of_property_read_u32(child, "sustainable-power", &prop))
			tzp->sustainable_power = prop;

		for (i = 0; i < tz->ntrips; i++)
			mask |= 1 << i;

		/* these two are left for temperature drivers to use */
		tzp->slope = tz->slope;
		tzp->offset = tz->offset;

		if (of_property_read_bool(child, "tracks-low"))
			tzp->tracks_low = true;

		zone = thermal_zone_device_register(child->name, tz->ntrips,
						    mask, tz,
						    ops, tzp,
						    tz->passive_delay,
						    tz->polling_delay);
		if (IS_ERR(zone)) {
			pr_err("Failed to build %s zone %ld\n", child->name,
			       PTR_ERR(zone));
			kfree(tzp);
			kfree(ops);
			of_thermal_free_zone(tz);
			/* attempting to build remaining zones still */
		}
	}
	of_node_put(np);

	return 0;

exit_free:
	of_node_put(child);
	of_node_put(np);
	of_thermal_free_zone(tz);

	/* no memory available, so free what we have built */
	of_thermal_destroy_zones();

	return -ENOMEM;
}

/**
 * of_thermal_destroy_zones - remove all zones parsed and allocated resources
 *
 * Finds all zones parsed and added to the thermal framework and remove them
 * from the system, together with their resources.
 *
 */
void of_thermal_destroy_zones(void)
{
	struct device_node *np, *child;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np) {
		pr_debug("unable to find thermal zones\n");
		return;
	}

	for_each_available_child_of_node(np, child) {
		struct thermal_zone_device *zone;

		zone = thermal_zone_get_zone_by_name(child->name);
		if (IS_ERR(zone))
			continue;

		thermal_zone_device_unregister(zone);
		kfree(zone->tzp);
		kfree(zone->ops);
		of_thermal_free_zone(zone->devdata);
	}
	of_node_put(np);
}

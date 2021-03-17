// SPDX-License-Identifier: GPL-2.0
/*
 *  of-thermal.c - Generic Thermal Management device tree support.
 *
 *  Copyright (C) 2013 Texas Instruments
 *  Copyright (C) 2013 Eduardo Valentin <eduardo.valentin@ti.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/export.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/string.h>

#include "thermal_core.h"

/***   Private data structures to represent thermal device tree data ***/

/**
 * struct __thermal_cooling_bind_param - a cooling device for a trip point
 * @cooling_device: a pointer to identify the referred cooling device
 * @min: minimum cooling state used at this trip point
 * @max: maximum cooling state used at this trip point
 */

struct __thermal_cooling_bind_param {
	struct device_node *cooling_device;
	unsigned long min;
	unsigned long max;
};

/**
 * struct __thermal_bind_param - a match between trip and cooling device
 * @tcbp: a pointer to an array of cooling devices
 * @count: number of elements in array
 * @trip_id: the trip point index
 * @usage: the percentage (from 0 to 100) of cooling contribution
 */

struct __thermal_bind_params {
	struct __thermal_cooling_bind_param *tcbp;
	unsigned int count;
	unsigned int trip_id;
	unsigned int usage;
};

/**
 * struct __thermal_zone - internal representation of a thermal zone
 * @passive_delay: polling interval while passive cooling is activated
 * @polling_delay: zone polling interval
 * @slope: slope of the temperature adjustment curve
 * @offset: offset of the temperature adjustment curve
 * @ntrips: number of trip points
 * @trips: an array of trip points (0..ntrips - 1)
 * @num_tbps: number of thermal bind params
 * @tbps: an array of thermal bind params (0..num_tbps - 1)
 * @sensor_data: sensor private data used while reading temperature and trend
 * @ops: set of callbacks to handle the thermal zone based on DT
 */

struct __thermal_zone {
	int passive_delay;
	int polling_delay;
	int slope;
	int offset;

	/* trip data */
	int ntrips;
	struct thermal_trip *trips;

	/* cooling binding data */
	int num_tbps;
	struct __thermal_bind_params *tbps;

	/* sensor interface */
	void *sensor_data;
	const struct thermal_zone_of_device_ops *ops;
};

/***   DT thermal zone device callbacks   ***/

static int of_thermal_get_temp(struct thermal_zone_device *tz,
			       int *temp)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data->ops->get_temp)
		return -EINVAL;

	return data->ops->get_temp(data->sensor_data, temp);
}

static int of_thermal_set_trips(struct thermal_zone_device *tz,
				int low, int high)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data->ops || !data->ops->set_trips)
		return -EINVAL;

	return data->ops->set_trips(data->sensor_data, low, high);
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

	return data->ops->set_emul_temp(data->sensor_data, temp);
}

static int of_thermal_get_trend(struct thermal_zone_device *tz, int trip,
				enum thermal_trend *trend)
{
	struct __thermal_zone *data = tz->devdata;

	if (!data->ops->get_trend)
		return -EINVAL;

	return data->ops->get_trend(data->sensor_data, trip, trend);
}

static int of_thermal_bind(struct thermal_zone_device *thermal,
			   struct thermal_cooling_device *cdev)
{
	struct __thermal_zone *data = thermal->devdata;
	struct __thermal_bind_params *tbp;
	struct __thermal_cooling_bind_param *tcbp;
	int i, j;

	if (!data || IS_ERR(data))
		return -ENODEV;

	/* find where to bind */
	for (i = 0; i < data->num_tbps; i++) {
		tbp = data->tbps + i;

		for (j = 0; j < tbp->count; j++) {
			tcbp = tbp->tcbp + j;

			if (tcbp->cooling_device == cdev->np) {
				int ret;

				ret = thermal_zone_bind_cooling_device(thermal,
						tbp->trip_id, cdev,
						tcbp->max,
						tcbp->min,
						tbp->usage);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

static int of_thermal_unbind(struct thermal_zone_device *thermal,
			     struct thermal_cooling_device *cdev)
{
	struct __thermal_zone *data = thermal->devdata;
	struct __thermal_bind_params *tbp;
	struct __thermal_cooling_bind_param *tcbp;
	int i, j;

	if (!data || IS_ERR(data))
		return -ENODEV;

	/* find where to unbind */
	for (i = 0; i < data->num_tbps; i++) {
		tbp = data->tbps + i;

		for (j = 0; j < tbp->count; j++) {
			tcbp = tbp->tcbp + j;

			if (tcbp->cooling_device == cdev->np) {
				int ret;

				ret = thermal_zone_unbind_cooling_device(thermal,
							tbp->trip_id, cdev);
				if (ret)
					return ret;
			}
		}
	}

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

	*temp = data->trips[trip].temperature;

	return 0;
}

static int of_thermal_set_trip_temp(struct thermal_zone_device *tz, int trip,
				    int temp)
{
	struct __thermal_zone *data = tz->devdata;

	if (trip >= data->ntrips || trip < 0)
		return -EDOM;

	if (data->ops->set_trip_temp) {
		int ret;

		ret = data->ops->set_trip_temp(data->sensor_data, trip, temp);
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

static struct thermal_zone_device_ops of_thermal_ops = {
	.get_trip_type = of_thermal_get_trip_type,
	.get_trip_temp = of_thermal_get_trip_temp,
	.set_trip_temp = of_thermal_set_trip_temp,
	.get_trip_hyst = of_thermal_get_trip_hyst,
	.set_trip_hyst = of_thermal_set_trip_hyst,
	.get_crit_temp = of_thermal_get_crit_temp,

	.bind = of_thermal_bind,
	.unbind = of_thermal_unbind,
};

/***   sensor API   ***/

static struct thermal_zone_device *
thermal_zone_of_add_sensor(struct device_node *zone,
			   struct device_node *sensor, void *data,
			   const struct thermal_zone_of_device_ops *ops)
{
	struct thermal_zone_device *tzd;
	struct __thermal_zone *tz;

	tzd = thermal_zone_get_zone_by_name(zone->name);
	if (IS_ERR(tzd))
		return ERR_PTR(-EPROBE_DEFER);

	tz = tzd->devdata;

	if (!ops)
		return ERR_PTR(-EINVAL);

	mutex_lock(&tzd->lock);
	tz->ops = ops;
	tz->sensor_data = data;

	tzd->ops->get_temp = of_thermal_get_temp;
	tzd->ops->get_trend = of_thermal_get_trend;

	/*
	 * The thermal zone core will calculate the window if they have set the
	 * optional set_trips pointer.
	 */
	if (ops->set_trips)
		tzd->ops->set_trips = of_thermal_set_trips;

	if (ops->set_emul_temp)
		tzd->ops->set_emul_temp = of_thermal_set_emul_temp;

	mutex_unlock(&tzd->lock);

	return tzd;
}

/**
 * thermal_zone_of_get_sensor_id - get sensor ID from a DT thermal zone
 * @tz_np: a valid thermal zone device node.
 * @sensor_np: a sensor node of a valid sensor device.
 * @id: the sensor ID returned if success.
 *
 * This function will get sensor ID from a given thermal zone node and
 * the sensor node must match the temperature provider @sensor_np.
 *
 * Return: 0 on success, proper error code otherwise.
 */

int thermal_zone_of_get_sensor_id(struct device_node *tz_np,
				  struct device_node *sensor_np,
				  u32 *id)
{
	struct of_phandle_args sensor_specs;
	int ret;

	ret = of_parse_phandle_with_args(tz_np,
					 "thermal-sensors",
					 "#thermal-sensor-cells",
					 0,
					 &sensor_specs);
	if (ret)
		return ret;

	if (sensor_specs.np != sensor_np) {
		of_node_put(sensor_specs.np);
		return -ENODEV;
	}

	if (sensor_specs.args_count > 1)
		pr_warn("%pOFn: too many cells in sensor specifier %d\n",
		     sensor_specs.np, sensor_specs.args_count);

	*id = sensor_specs.args_count ? sensor_specs.args[0] : 0;

	of_node_put(sensor_specs.np);

	return 0;
}
EXPORT_SYMBOL_GPL(thermal_zone_of_get_sensor_id);

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

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np)
		return ERR_PTR(-ENODEV);

	if (!dev || !dev->of_node) {
		of_node_put(np);
		return ERR_PTR(-ENODEV);
	}

	sensor_np = of_node_get(dev->of_node);

	for_each_available_child_of_node(np, child) {
		int ret, id;

		/* For now, thermal framework supports only 1 sensor per zone */
		ret = thermal_zone_of_get_sensor_id(child, sensor_np, &id);
		if (ret)
			continue;

		if (id == sensor_id) {
			tzd = thermal_zone_of_add_sensor(child, sensor_np,
							 data, ops);
			if (!IS_ERR(tzd))
				thermal_zone_device_enable(tzd);

			of_node_put(child);
			goto exit;
		}
	}
exit:
	of_node_put(sensor_np);
	of_node_put(np);

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

	tz->ops = NULL;
	tz->sensor_data = NULL;
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
	struct __thermal_cooling_bind_param *__tcbp;
	struct device_node *trip;
	int ret, i, count;
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

	count = of_count_phandle_with_args(np, "cooling-device",
					   "#cooling-cells");
	if (!count) {
		pr_err("Add a cooling_device property with at least one device\n");
		goto end;
	}

	__tcbp = kcalloc(count, sizeof(*__tcbp), GFP_KERNEL);
	if (!__tcbp)
		goto end;

	for (i = 0; i < count; i++) {
		ret = of_parse_phandle_with_args(np, "cooling-device",
				"#cooling-cells", i, &cooling_spec);
		if (ret < 0) {
			pr_err("Invalid cooling-device entry\n");
			goto free_tcbp;
		}

		__tcbp[i].cooling_device = cooling_spec.np;

		if (cooling_spec.args_count >= 2) { /* at least min and max */
			__tcbp[i].min = cooling_spec.args[0];
			__tcbp[i].max = cooling_spec.args[1];
		} else {
			pr_err("wrong reference to cooling device, missing limits\n");
		}
	}

	__tbp->tcbp = __tcbp;
	__tbp->count = count;

	goto end;

free_tcbp:
	for (i = i - 1; i >= 0; i--)
		of_node_put(__tcbp[i].cooling_device);
	kfree(__tcbp);
end:
	of_node_put(trip);

	return ret;
}

/*
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
		pr_err("%pOFn: missing polling-delay-passive property\n", np);
		goto free_tz;
	}
	tz->passive_delay = prop;

	ret = of_property_read_u32(np, "polling-delay", &prop);
	if (ret < 0) {
		pr_err("%pOFn: missing polling-delay property\n", np);
		goto free_tz;
	}
	tz->polling_delay = prop;

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

	return tz;

free_tbps:
	for (i = i - 1; i >= 0; i--) {
		struct __thermal_bind_params *tbp = tz->tbps + i;
		int j;

		for (j = 0; j < tbp->count; j++)
			of_node_put(tbp->tcbp[j].cooling_device);

		kfree(tbp->tcbp);
	}

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

static __init void of_thermal_free_zone(struct __thermal_zone *tz)
{
	struct __thermal_bind_params *tbp;
	int i, j;

	for (i = 0; i < tz->num_tbps; i++) {
		tbp = tz->tbps + i;

		for (j = 0; j < tbp->count; j++)
			of_node_put(tbp->tcbp[j].cooling_device);

		kfree(tbp->tcbp);
	}

	kfree(tz->tbps);
	for (i = 0; i < tz->ntrips; i++)
		of_node_put(tz->trips[i].np);
	kfree(tz->trips);
	kfree(tz);
}

/**
 * of_thermal_destroy_zones - remove all zones parsed and allocated resources
 *
 * Finds all zones parsed and added to the thermal framework and remove them
 * from the system, together with their resources.
 *
 */
static __init void of_thermal_destroy_zones(void)
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
			pr_err("failed to build thermal zone %pOFn: %ld\n",
			       child,
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

		zone = thermal_zone_device_register(child->name, tz->ntrips,
						    mask, tz,
						    ops, tzp,
						    tz->passive_delay,
						    tz->polling_delay);
		if (IS_ERR(zone)) {
			pr_err("Failed to build %pOFn zone %ld\n", child,
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

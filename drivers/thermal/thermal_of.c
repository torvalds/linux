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
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/string.h>

#include "thermal_core.h"

/***   functions parsing device tree nodes   ***/

static int of_find_trip_id(struct device_node *np, struct device_node *trip)
{
	struct device_node *trips;
	struct device_node *t;
	int i = 0;

	trips = of_get_child_by_name(np, "trips");
	if (!trips) {
		pr_err("Failed to find 'trips' node\n");
		return -EINVAL;
	}

	/*
	 * Find the trip id point associated with the cooling device map
	 */
	for_each_child_of_node(trips, t) {

		if (t == trip) {
			of_node_put(t);
			goto out;
		}
		i++;
	}

	i = -ENXIO;
out:
	of_node_put(trips);

	return i;
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

	return 0;
}

static struct thermal_trip *thermal_of_trips_init(struct device_node *np, int *ntrips)
{
	struct thermal_trip *tt;
	struct device_node *trips, *trip;
	int ret, count;

	trips = of_get_child_by_name(np, "trips");
	if (!trips) {
		pr_err("Failed to find 'trips' node\n");
		return ERR_PTR(-EINVAL);
	}

	count = of_get_child_count(trips);
	if (!count) {
		pr_err("No trip point defined\n");
		ret = -EINVAL;
		goto out_of_node_put;
	}

	tt = kzalloc(sizeof(*tt) * count, GFP_KERNEL);
	if (!tt) {
		ret = -ENOMEM;
		goto out_of_node_put;
	}

	*ntrips = count;

	count = 0;
	for_each_child_of_node(trips, trip) {
		ret = thermal_of_populate_trip(trip, &tt[count++]);
		if (ret)
			goto out_kfree;
	}

	of_node_put(trips);

	return tt;

out_kfree:
	kfree(tt);
	*ntrips = 0;
out_of_node_put:
	of_node_put(trips);

	return ERR_PTR(ret);
}

static struct device_node *of_thermal_zone_find(struct device_node *sensor, int id)
{
	struct device_node *np, *tz;
	struct of_phandle_args sensor_specs;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np) {
		pr_debug("No thermal zones description\n");
		return ERR_PTR(-ENODEV);
	}

	/*
	 * Search for each thermal zone, a defined sensor
	 * corresponding to the one passed as parameter
	 */
	for_each_available_child_of_node(np, tz) {

		int count, i;

		count = of_count_phandle_with_args(tz, "thermal-sensors",
						   "#thermal-sensor-cells");
		if (count <= 0) {
			pr_err("%pOFn: missing thermal sensor\n", tz);
			tz = ERR_PTR(-EINVAL);
			goto out;
		}

		for (i = 0; i < count; i++) {

			int ret;

			ret = of_parse_phandle_with_args(tz, "thermal-sensors",
							 "#thermal-sensor-cells",
							 i, &sensor_specs);
			if (ret < 0) {
				pr_err("%pOFn: Failed to read thermal-sensors cells: %d\n", tz, ret);
				tz = ERR_PTR(ret);
				goto out;
			}

			if ((sensor == sensor_specs.np) && id == (sensor_specs.args_count ?
								  sensor_specs.args[0] : 0)) {
				pr_debug("sensor %pOFn id=%d belongs to %pOFn\n", sensor, id, tz);
				goto out;
			}
		}
	}
	tz = ERR_PTR(-ENODEV);
out:
	of_node_put(np);
	return tz;
}

static int thermal_of_monitor_init(struct device_node *np, int *delay, int *pdelay)
{
	int ret;

	ret = of_property_read_u32(np, "polling-delay-passive", pdelay);
	if (ret < 0) {
		pr_err("%pOFn: missing polling-delay-passive property\n", np);
		return ret;
	}

	ret = of_property_read_u32(np, "polling-delay", delay);
	if (ret < 0) {
		pr_err("%pOFn: missing polling-delay property\n", np);
		return ret;
	}

	return 0;
}

static void thermal_of_parameters_init(struct device_node *np,
				       struct thermal_zone_params *tzp)
{
	int coef[2];
	int ncoef = ARRAY_SIZE(coef);
	int prop, ret;

	tzp->no_hwmon = true;

	if (!of_property_read_u32(np, "sustainable-power", &prop))
		tzp->sustainable_power = prop;

	/*
	 * For now, the thermal framework supports only one sensor per
	 * thermal zone. Thus, we are considering only the first two
	 * values as slope and offset.
	 */
	ret = of_property_read_u32_array(np, "coefficients", coef, ncoef);
	if (ret) {
		coef[0] = 1;
		coef[1] = 0;
	}

	tzp->slope = coef[0];
	tzp->offset = coef[1];
}

static struct device_node *thermal_of_zone_get_by_name(struct thermal_zone_device *tz)
{
	struct device_node *np, *tz_np;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np)
		return ERR_PTR(-ENODEV);

	tz_np = of_get_child_by_name(np, tz->type);

	of_node_put(np);

	if (!tz_np)
		return ERR_PTR(-ENODEV);

	return tz_np;
}

static int __thermal_of_unbind(struct device_node *map_np, int index, int trip_id,
			       struct thermal_zone_device *tz, struct thermal_cooling_device *cdev)
{
	struct of_phandle_args cooling_spec;
	int ret;

	ret = of_parse_phandle_with_args(map_np, "cooling-device", "#cooling-cells",
					 index, &cooling_spec);

	if (ret < 0) {
		pr_err("Invalid cooling-device entry\n");
		return ret;
	}

	of_node_put(cooling_spec.np);

	if (cooling_spec.args_count < 2) {
		pr_err("wrong reference to cooling device, missing limits\n");
		return -EINVAL;
	}

	if (cooling_spec.np != cdev->np)
		return 0;

	ret = thermal_zone_unbind_cooling_device(tz, trip_id, cdev);
	if (ret)
		pr_err("Failed to unbind '%s' with '%s': %d\n", tz->type, cdev->type, ret);

	return ret;
}

static int __thermal_of_bind(struct device_node *map_np, int index, int trip_id,
			     struct thermal_zone_device *tz, struct thermal_cooling_device *cdev)
{
	struct of_phandle_args cooling_spec;
	int ret, weight = THERMAL_WEIGHT_DEFAULT;

	of_property_read_u32(map_np, "contribution", &weight);

	ret = of_parse_phandle_with_args(map_np, "cooling-device", "#cooling-cells",
					 index, &cooling_spec);

	if (ret < 0) {
		pr_err("Invalid cooling-device entry\n");
		return ret;
	}

	of_node_put(cooling_spec.np);

	if (cooling_spec.args_count < 2) {
		pr_err("wrong reference to cooling device, missing limits\n");
		return -EINVAL;
	}

	if (cooling_spec.np != cdev->np)
		return 0;

	ret = thermal_zone_bind_cooling_device(tz, trip_id, cdev, cooling_spec.args[1],
					       cooling_spec.args[0],
					       weight);
	if (ret)
		pr_err("Failed to bind '%s' with '%s': %d\n", tz->type, cdev->type, ret);

	return ret;
}

static int thermal_of_for_each_cooling_device(struct device_node *tz_np, struct device_node *map_np,
					      struct thermal_zone_device *tz, struct thermal_cooling_device *cdev,
					      int (*action)(struct device_node *, int, int,
							    struct thermal_zone_device *, struct thermal_cooling_device *))
{
	struct device_node *tr_np;
	int count, i, trip_id;

	tr_np = of_parse_phandle(map_np, "trip", 0);
	if (!tr_np)
		return -ENODEV;

	trip_id = of_find_trip_id(tz_np, tr_np);
	if (trip_id < 0)
		return trip_id;

	count = of_count_phandle_with_args(map_np, "cooling-device", "#cooling-cells");
	if (count <= 0) {
		pr_err("Add a cooling_device property with at least one device\n");
		return -ENOENT;
	}

	/*
	 * At this point, we don't want to bail out when there is an
	 * error, we will try to bind/unbind as many as possible
	 * cooling devices
	 */
	for (i = 0; i < count; i++)
		action(map_np, i, trip_id, tz, cdev);

	return 0;
}

static int thermal_of_for_each_cooling_maps(struct thermal_zone_device *tz,
					    struct thermal_cooling_device *cdev,
					    int (*action)(struct device_node *, int, int,
							  struct thermal_zone_device *, struct thermal_cooling_device *))
{
	struct device_node *tz_np, *cm_np, *child;
	int ret = 0;

	tz_np = thermal_of_zone_get_by_name(tz);
	if (IS_ERR(tz_np)) {
		pr_err("Failed to get node tz by name\n");
		return PTR_ERR(tz_np);
	}

	cm_np = of_get_child_by_name(tz_np, "cooling-maps");
	if (!cm_np)
		goto out;

	for_each_child_of_node(cm_np, child) {
		ret = thermal_of_for_each_cooling_device(tz_np, child, tz, cdev, action);
		if (ret) {
			of_node_put(child);
			break;
		}
	}

	of_node_put(cm_np);
out:
	of_node_put(tz_np);

	return ret;
}

static int thermal_of_bind(struct thermal_zone_device *tz,
			   struct thermal_cooling_device *cdev)
{
	return thermal_of_for_each_cooling_maps(tz, cdev, __thermal_of_bind);
}

static int thermal_of_unbind(struct thermal_zone_device *tz,
			     struct thermal_cooling_device *cdev)
{
	return thermal_of_for_each_cooling_maps(tz, cdev, __thermal_of_unbind);
}

/**
 * thermal_of_zone_unregister - Cleanup the specific allocated ressources
 *
 * This function disables the thermal zone and frees the different
 * ressources allocated specific to the thermal OF.
 *
 * @tz: a pointer to the thermal zone structure
 */
static void thermal_of_zone_unregister(struct thermal_zone_device *tz)
{
	struct thermal_trip *trips = tz->trips;
	struct thermal_zone_device_ops *ops = tz->ops;

	thermal_zone_device_disable(tz);
	thermal_zone_device_unregister(tz);
	kfree(trips);
	kfree(ops);
}

/**
 * thermal_of_zone_register - Register a thermal zone with device node
 * sensor
 *
 * The thermal_of_zone_register() parses a device tree given a device
 * node sensor and identifier. It searches for the thermal zone
 * associated to the couple sensor/id and retrieves all the thermal
 * zone properties and registers new thermal zone with those
 * properties.
 *
 * @sensor: A device node pointer corresponding to the sensor in the device tree
 * @id: An integer as sensor identifier
 * @data: A private data to be stored in the thermal zone dedicated private area
 * @ops: A set of thermal sensor ops
 *
 * Return: a valid thermal zone structure pointer on success.
 * 	- EINVAL: if the device tree thermal description is malformed
 *	- ENOMEM: if one structure can not be allocated
 *	- Other negative errors are returned by the underlying called functions
 */
static struct thermal_zone_device *thermal_of_zone_register(struct device_node *sensor, int id, void *data,
							    const struct thermal_zone_device_ops *ops)
{
	struct thermal_zone_device *tz;
	struct thermal_trip *trips;
	struct thermal_zone_params tzp = {};
	struct thermal_zone_device_ops *of_ops;
	struct device_node *np;
	int delay, pdelay;
	int ntrips, mask;
	int ret;

	of_ops = kmemdup(ops, sizeof(*ops), GFP_KERNEL);
	if (!of_ops)
		return ERR_PTR(-ENOMEM);

	np = of_thermal_zone_find(sensor, id);
	if (IS_ERR(np)) {
		if (PTR_ERR(np) != -ENODEV)
			pr_err("Failed to find thermal zone for %pOFn id=%d\n", sensor, id);
		ret = PTR_ERR(np);
		goto out_kfree_of_ops;
	}

	trips = thermal_of_trips_init(np, &ntrips);
	if (IS_ERR(trips)) {
		pr_err("Failed to find trip points for %pOFn id=%d\n", sensor, id);
		ret = PTR_ERR(trips);
		goto out_kfree_of_ops;
	}

	ret = thermal_of_monitor_init(np, &delay, &pdelay);
	if (ret) {
		pr_err("Failed to initialize monitoring delays from %pOFn\n", np);
		goto out_kfree_trips;
	}

	thermal_of_parameters_init(np, &tzp);

	of_ops->bind = thermal_of_bind;
	of_ops->unbind = thermal_of_unbind;

	mask = GENMASK_ULL((ntrips) - 1, 0);

	tz = thermal_zone_device_register_with_trips(np->name, trips, ntrips,
						     mask, data, of_ops, &tzp,
						     pdelay, delay);
	if (IS_ERR(tz)) {
		ret = PTR_ERR(tz);
		pr_err("Failed to register thermal zone %pOFn: %d\n", np, ret);
		goto out_kfree_trips;
	}

	ret = thermal_zone_device_enable(tz);
	if (ret) {
		pr_err("Failed to enabled thermal zone '%s', id=%d: %d\n",
		       tz->type, tz->id, ret);
		thermal_of_zone_unregister(tz);
		return ERR_PTR(ret);
	}

	return tz;

out_kfree_trips:
	kfree(trips);
out_kfree_of_ops:
	kfree(of_ops);

	return ERR_PTR(ret);
}

static void devm_thermal_of_zone_release(struct device *dev, void *res)
{
	thermal_of_zone_unregister(*(struct thermal_zone_device **)res);
}

static int devm_thermal_of_zone_match(struct device *dev, void *res,
				      void *data)
{
	struct thermal_zone_device **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

/**
 * devm_thermal_of_zone_register - register a thermal tied with the sensor life cycle
 *
 * This function is the device version of the thermal_of_zone_register() function.
 *
 * @dev: a device structure pointer to sensor to be tied with the thermal zone OF life cycle
 * @sensor_id: the sensor identifier
 * @data: a pointer to a private data to be stored in the thermal zone 'devdata' field
 * @ops: a pointer to the ops structure associated with the sensor
 */
struct thermal_zone_device *devm_thermal_of_zone_register(struct device *dev, int sensor_id, void *data,
							  const struct thermal_zone_device_ops *ops)
{
	struct thermal_zone_device **ptr, *tzd;

	ptr = devres_alloc(devm_thermal_of_zone_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	tzd = thermal_of_zone_register(dev->of_node, sensor_id, data, ops);
	if (IS_ERR(tzd)) {
		devres_free(ptr);
		return tzd;
	}

	*ptr = tzd;
	devres_add(dev, ptr);

	return tzd;
}
EXPORT_SYMBOL_GPL(devm_thermal_of_zone_register);

/**
 * devm_thermal_of_zone_unregister - Resource managed version of
 *				thermal_of_zone_unregister().
 * @dev: Device for which which resource was allocated.
 * @tz: a pointer to struct thermal_zone where the sensor is registered.
 *
 * This function removes the sensor callbacks and private data from the
 * thermal zone device registered with devm_thermal_zone_of_sensor_register()
 * API. It will also silent the zone by remove the .get_temp() and .get_trend()
 * thermal zone device callbacks.
 * Normally this function will not need to be called and the resource
 * management code will ensure that the resource is freed.
 */
void devm_thermal_of_zone_unregister(struct device *dev, struct thermal_zone_device *tz)
{
	WARN_ON(devres_release(dev, devm_thermal_of_zone_release,
			       devm_thermal_of_zone_match, tz));
}
EXPORT_SYMBOL_GPL(devm_thermal_of_zone_unregister);

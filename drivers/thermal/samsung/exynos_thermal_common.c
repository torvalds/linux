/*
 * exynos_thermal_common.c - Samsung EXYNOS common thermal file
 *
 *  Copyright (C) 2013 Samsung Electronics
 *  Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/cpu_cooling.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "exynos_thermal_common.h"

struct exynos_thermal_zone {
	enum thermal_device_mode mode;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cool_dev[MAX_COOLING_DEVICE];
	unsigned int cool_dev_size;
	struct platform_device *exynos4_dev;
	struct thermal_sensor_conf *sensor_conf;
	bool bind;
};

/* Get mode callback functions for thermal zone */
static int exynos_get_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode *mode)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	if (th_zone)
		*mode = th_zone->mode;
	return 0;
}

/* Set mode callback functions for thermal zone */
static int exynos_set_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode mode)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	if (!th_zone) {
		dev_err(&thermal->device,
			"thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&thermal->lock);

	if (mode == THERMAL_DEVICE_ENABLED &&
		!th_zone->sensor_conf->trip_data.trigger_falling)
		thermal->polling_delay = IDLE_INTERVAL;
	else
		thermal->polling_delay = 0;

	mutex_unlock(&thermal->lock);

	th_zone->mode = mode;
	thermal_zone_device_update(thermal);
	dev_dbg(th_zone->sensor_conf->dev,
		"thermal polling set for duration=%d msec\n",
		thermal->polling_delay);
	return 0;
}


/* Get trip type callback functions for thermal zone */
static int exynos_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	int max_trip = th_zone->sensor_conf->trip_data.trip_count;
	int trip_type;

	if (trip < 0 || trip >= max_trip)
		return -EINVAL;

	trip_type = th_zone->sensor_conf->trip_data.trip_type[trip];

	if (trip_type == SW_TRIP)
		*type = THERMAL_TRIP_CRITICAL;
	else if (trip_type == THROTTLE_ACTIVE)
		*type = THERMAL_TRIP_ACTIVE;
	else if (trip_type == THROTTLE_PASSIVE)
		*type = THERMAL_TRIP_PASSIVE;
	else
		return -EINVAL;

	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int exynos_get_trip_temp(struct thermal_zone_device *thermal, int trip,
				unsigned long *temp)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	int max_trip = th_zone->sensor_conf->trip_data.trip_count;

	if (trip < 0 || trip >= max_trip)
		return -EINVAL;

	*temp = th_zone->sensor_conf->trip_data.trip_val[trip];
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int exynos_get_crit_temp(struct thermal_zone_device *thermal,
				unsigned long *temp)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	int max_trip = th_zone->sensor_conf->trip_data.trip_count;
	/* Get the temp of highest trip*/
	return exynos_get_trip_temp(thermal, max_trip - 1, temp);
}

/* Bind callback functions for thermal zone */
static int exynos_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size, level;
	struct freq_clip_table *tab_ptr, *clip_data;
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;

	tab_ptr = (struct freq_clip_table *)data->cooling_data.freq_data;
	tab_size = data->cooling_data.freq_clip_count;

	if (tab_ptr == NULL || tab_size == 0)
		return 0;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		clip_data = (struct freq_clip_table *)&(tab_ptr[i]);
		level = cpufreq_cooling_get_level(0, clip_data->freq_clip_max);
		if (level == THERMAL_CSTATE_INVALID)
			return 0;
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_bind_cooling_device(thermal, i, cdev,
								level, 0)) {
				dev_err(data->dev,
					"error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = true;
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Unbind callback functions for thermal zone */
static int exynos_unbind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size;
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;

	if (th_zone->bind == false)
		return 0;

	tab_size = data->cooling_data.freq_clip_count;

	if (tab_size == 0)
		return 0;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_unbind_cooling_device(thermal, i,
								cdev)) {
				dev_err(data->dev,
					"error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = false;
			break;
		default:
			ret = -EINVAL;
		}
	}
	return ret;
}

/* Get temperature callback functions for thermal zone */
static int exynos_get_temp(struct thermal_zone_device *thermal,
			unsigned long *temp)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	void *data;

	if (!th_zone->sensor_conf) {
		dev_err(&thermal->device,
			"Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone->sensor_conf->driver_data;
	*temp = th_zone->sensor_conf->read_temperature(data);
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;
	return 0;
}

/* Get temperature callback functions for thermal zone */
static int exynos_set_emul_temp(struct thermal_zone_device *thermal,
						unsigned long temp)
{
	void *data;
	int ret = -EINVAL;
	struct exynos_thermal_zone *th_zone = thermal->devdata;

	if (!th_zone->sensor_conf) {
		dev_err(&thermal->device,
			"Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone->sensor_conf->driver_data;
	if (th_zone->sensor_conf->write_emul_temp)
		ret = th_zone->sensor_conf->write_emul_temp(data, temp);
	return ret;
}

/* Get the temperature trend */
static int exynos_get_trend(struct thermal_zone_device *thermal,
			int trip, enum thermal_trend *trend)
{
	int ret;
	unsigned long trip_temp;

	ret = exynos_get_trip_temp(thermal, trip, &trip_temp);
	if (ret < 0)
		return ret;

	if (thermal->temperature >= trip_temp)
		*trend = THERMAL_TREND_RAISE_FULL;
	else
		*trend = THERMAL_TREND_DROP_FULL;

	return 0;
}
/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops exynos_dev_ops = {
	.bind = exynos_bind,
	.unbind = exynos_unbind,
	.get_temp = exynos_get_temp,
	.set_emul_temp = exynos_set_emul_temp,
	.get_trend = exynos_get_trend,
	.get_mode = exynos_get_mode,
	.set_mode = exynos_set_mode,
	.get_trip_type = exynos_get_trip_type,
	.get_trip_temp = exynos_get_trip_temp,
	.get_crit_temp = exynos_get_crit_temp,
};

/*
 * This function may be called from interrupt based temperature sensor
 * when threshold is changed.
 */
void exynos_report_trigger(struct thermal_sensor_conf *conf)
{
	unsigned int i;
	char data[10];
	char *envp[] = { data, NULL };
	struct exynos_thermal_zone *th_zone;

	if (!conf || !conf->pzone_data) {
		pr_err("Invalid temperature sensor configuration data\n");
		return;
	}

	th_zone = conf->pzone_data;

	if (th_zone->bind == false) {
		for (i = 0; i < th_zone->cool_dev_size; i++) {
			if (!th_zone->cool_dev[i])
				continue;
			exynos_bind(th_zone->therm_dev,
					th_zone->cool_dev[i]);
		}
	}

	thermal_zone_device_update(th_zone->therm_dev);

	mutex_lock(&th_zone->therm_dev->lock);
	/* Find the level for which trip happened */
	for (i = 0; i < th_zone->sensor_conf->trip_data.trip_count; i++) {
		if (th_zone->therm_dev->last_temperature <
			th_zone->sensor_conf->trip_data.trip_val[i] * MCELSIUS)
			break;
	}

	if (th_zone->mode == THERMAL_DEVICE_ENABLED &&
		!th_zone->sensor_conf->trip_data.trigger_falling) {
		if (i > 0)
			th_zone->therm_dev->polling_delay = ACTIVE_INTERVAL;
		else
			th_zone->therm_dev->polling_delay = IDLE_INTERVAL;
	}

	snprintf(data, sizeof(data), "%u", i);
	kobject_uevent_env(&th_zone->therm_dev->device.kobj, KOBJ_CHANGE, envp);
	mutex_unlock(&th_zone->therm_dev->lock);
}

/* Register with the in-kernel thermal management */
int exynos_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret;
	struct cpumask mask_val;
	struct exynos_thermal_zone *th_zone;

	if (!sensor_conf || !sensor_conf->read_temperature) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone = devm_kzalloc(sensor_conf->dev,
				sizeof(struct exynos_thermal_zone), GFP_KERNEL);
	if (!th_zone)
		return -ENOMEM;

	th_zone->sensor_conf = sensor_conf;
	/*
	 * TODO: 1) Handle multiple cooling devices in a thermal zone
	 *	 2) Add a flag/name in cooling info to map to specific
	 *	 sensor
	 */
	if (sensor_conf->cooling_data.freq_clip_count > 0) {
		cpumask_set_cpu(0, &mask_val);
		th_zone->cool_dev[th_zone->cool_dev_size] =
					cpufreq_cooling_register(&mask_val);
		if (IS_ERR(th_zone->cool_dev[th_zone->cool_dev_size])) {
			dev_err(sensor_conf->dev,
				"Failed to register cpufreq cooling device\n");
			ret = -EINVAL;
			goto err_unregister;
		}
		th_zone->cool_dev_size++;
	}

	th_zone->therm_dev = thermal_zone_device_register(
			sensor_conf->name, sensor_conf->trip_data.trip_count,
			0, th_zone, &exynos_dev_ops, NULL, 0,
			sensor_conf->trip_data.trigger_falling ? 0 :
			IDLE_INTERVAL);

	if (IS_ERR(th_zone->therm_dev)) {
		dev_err(sensor_conf->dev,
			"Failed to register thermal zone device\n");
		ret = PTR_ERR(th_zone->therm_dev);
		goto err_unregister;
	}
	th_zone->mode = THERMAL_DEVICE_ENABLED;
	sensor_conf->pzone_data = th_zone;

	dev_info(sensor_conf->dev,
		"Exynos: Thermal zone(%s) registered\n", sensor_conf->name);

	return 0;

err_unregister:
	exynos_unregister_thermal(sensor_conf);
	return ret;
}

/* Un-Register with the in-kernel thermal management */
void exynos_unregister_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int i;
	struct exynos_thermal_zone *th_zone;

	if (!sensor_conf || !sensor_conf->pzone_data) {
		pr_err("Invalid temperature sensor configuration data\n");
		return;
	}

	th_zone = sensor_conf->pzone_data;

	if (th_zone->therm_dev)
		thermal_zone_device_unregister(th_zone->therm_dev);

	for (i = 0; i < th_zone->cool_dev_size; i++) {
		if (th_zone->cool_dev[i])
			cpufreq_cooling_unregister(th_zone->cool_dev[i]);
	}

	dev_info(sensor_conf->dev,
		"Exynos: Kernel Thermal management unregistered\n");
}

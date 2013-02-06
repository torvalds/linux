/* linux/drivers/thermal/exynos_thermal.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpu_cooling.h>
#include <linux/platform_data/exynos4_tmu.h>
#include <linux/exynos_thermal.h>

struct exynos4_thermal_zone {
	unsigned int idle_interval;
	unsigned int active_interval;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cool_dev;
	struct platform_device *exynos4_dev;
	struct thermal_sensor_conf *sensor_conf;
	struct exynos4_tmu_platform_data *sensor_data;
};

static struct exynos4_thermal_zone *th_zone;

static int exynos4_get_mode(struct thermal_zone_device *thermal,
			    enum thermal_device_mode *mode)
{
	if (th_zone->sensor_conf) {
		pr_info("Temperature sensor not initialised\n");
		*mode = THERMAL_DEVICE_DISABLED;
	} else
		*mode = THERMAL_DEVICE_ENABLED;
	return 0;
}

static int exynos4_set_mode(struct thermal_zone_device *thermal,
			    enum thermal_device_mode mode)
{
	if (!th_zone->therm_dev) {
		pr_notice("thermal zone not registered\n");
		return 0;
	}
	if (mode == THERMAL_DEVICE_ENABLED)
		th_zone->therm_dev->polling_delay =
				th_zone->active_interval*1000;
	else
		th_zone->therm_dev->polling_delay =
				th_zone->idle_interval*1000;

	thermal_zone_device_update(th_zone->therm_dev);
	pr_info("thermal polling set for duration=%d sec\n",
				th_zone->therm_dev->polling_delay/1000);
	return 0;
}

/*This may be called from interrupt based temperature sensor*/
void exynos4_report_trigger(void)
{
	unsigned int th_temp = th_zone->sensor_data->threshold;
	unsigned int monitor_temp = th_temp +
			th_zone->sensor_data->trigger_levels[1];

	thermal_zone_device_update(th_zone->therm_dev);

	if (th_zone->therm_dev->last_temperature > monitor_temp)
		th_zone->therm_dev->polling_delay =
					th_zone->active_interval*1000;
	else
		th_zone->therm_dev->polling_delay =
					th_zone->idle_interval*1000;
}

static int exynos4_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	if (trip == 0 || trip == 1)
		*type = THERMAL_TRIP_STATE_ACTIVE;
	else if (trip == 2)
		*type = THERMAL_TRIP_HOT;
	else if (trip == 3)
		*type = THERMAL_TRIP_CRITICAL;
	else
		return -EINVAL;

	return 0;
}

static int exynos4_get_trip_temp(struct thermal_zone_device *thermal, int trip,
				 unsigned long *temp)
{
	unsigned int th_temp = th_zone->sensor_data->threshold;

	/*Monitor zone*/
	if (trip == 0)
		*temp = th_temp + th_zone->sensor_data->trigger_levels[0];
	/*Warn zone*/
	else if (trip == 1)
		*temp = th_temp + th_zone->sensor_data->trigger_levels[1];
	else if (trip == 2)
		*temp = th_temp + th_zone->sensor_data->trigger_levels[2];
	/*Panic zone*/
	else if (trip == 3)
		*temp = th_temp + th_zone->sensor_data->trigger_levels[3];
	else
		return -EINVAL;
	/*convert the temperature into millicelsius*/
	*temp = *temp * 1000;
	return 0;
}

static int exynos4_get_crit_temp(struct thermal_zone_device *thermal,
				 unsigned long *temp)
{
	unsigned int th_temp = th_zone->sensor_data->threshold;
	/*Panic zone*/
	*temp = th_temp + th_zone->sensor_data->trigger_levels[3];
	/*convert the temperature into millicelsius*/
	*temp = *temp * 1000;
	return 0;
}

static int exynos4_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	/* if the cooling device is the one from exynos4 bind it */
	if (cdev != th_zone->cool_dev)
		return 0;

	if (thermal_zone_bind_cooling_device(thermal, 0, cdev)) {
		pr_err("error binding cooling dev\n");
		return -EINVAL;
	}
	if (thermal_zone_bind_cooling_device(thermal, 1, cdev)) {
		pr_err("error binding cooling dev\n");
		return -EINVAL;
	}

	return 0;
}

static int exynos4_unbind(struct thermal_zone_device *thermal,
			  struct thermal_cooling_device *cdev)
{
	if (cdev != th_zone->cool_dev)
		return 0;
	if (thermal_zone_unbind_cooling_device(thermal, 0, cdev)) {
		pr_err("error unbinding cooling dev\n");
		return -EINVAL;
	}
	if (thermal_zone_unbind_cooling_device(thermal, 1, cdev)) {
		pr_err("error unbinding cooling dev\n");
		return -EINVAL;
	}
	return 0;
}

static int exynos4_get_temp(struct thermal_zone_device *thermal,
			       unsigned long *temp)
{
	void *data;

	if (!th_zone->sensor_conf) {
		pr_info("Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone->sensor_conf->private_data;
	*temp = th_zone->sensor_conf->read_temperature(data);
	/*convert the temperature into millicelsius*/
	*temp = *temp * 1000;
	return 0;
}

/* bind callback functions to thermalzone */
static struct thermal_zone_device_ops exynos4_dev_ops = {
	.bind = exynos4_bind,
	.unbind = exynos4_unbind,
	.get_temp = exynos4_get_temp,
	.get_mode = exynos4_get_mode,
	.set_mode = exynos4_set_mode,
	.get_trip_type = exynos4_get_trip_type,
	.get_trip_temp = exynos4_get_trip_temp,
	.get_crit_temp = exynos4_get_crit_temp,
};

int exynos4_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret;

	if (!sensor_conf) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone = kzalloc(sizeof(struct exynos4_thermal_zone), GFP_KERNEL);
	if (!th_zone) {
		ret = -ENOMEM;
		goto err_unregister;
	}

	th_zone->sensor_conf = sensor_conf;

	th_zone->sensor_data = sensor_conf->sensor_data;
	if (!th_zone->sensor_data) {
		pr_err("Temperature sensor data not initialised\n");
		ret = -EINVAL;
		goto err_unregister;
	}

	th_zone->cool_dev = cpufreq_cooling_register(
		(struct freq_pctg_table *)th_zone->sensor_data->freq_tab,
		th_zone->sensor_data->freq_tab_count, cpumask_of(0));

	if (IS_ERR(th_zone->cool_dev)) {
		pr_err("Failed to register cpufreq cooling device\n");
		ret = -EINVAL;
		goto err_unregister;
	}

	th_zone->therm_dev = thermal_zone_device_register(sensor_conf->name,
				4, NULL, &exynos4_dev_ops, 0, 0, 0, 1000);
	if (IS_ERR(th_zone->therm_dev)) {
		pr_err("Failed to register thermal zone device\n");
		ret = -EINVAL;
		goto err_unregister;
	}

	th_zone->active_interval = 5;
	th_zone->idle_interval = 10;

	exynos4_set_mode(th_zone->therm_dev, THERMAL_DEVICE_DISABLED);

	pr_info("Exynos: Kernel Thermal management registered\n");

	return 0;

err_unregister:
	exynos4_unregister_thermal();
	return ret;
}
EXPORT_SYMBOL(exynos4_register_thermal);

void exynos4_unregister_thermal(void)
{
	if (th_zone && th_zone->cool_dev)
		cpufreq_cooling_unregister();

	if (th_zone && th_zone->therm_dev)
		thermal_zone_device_unregister(th_zone->therm_dev);

	kfree(th_zone);

	pr_info("Exynos: Kernel Thermal management unregistered\n");
}
EXPORT_SYMBOL(exynos4_unregister_thermal);

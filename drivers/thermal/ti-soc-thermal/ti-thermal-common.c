/*
 * OMAP thermal driver interface
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 * Contact:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/thermal.h>
#include <linux/cpumask.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>

#include "ti-thermal.h"
#include "ti-bandgap.h"

/* common data structures */
struct ti_thermal_data {
	struct thermal_zone_device *ti_thermal;
	struct thermal_zone_device *pcb_tz;
	struct thermal_cooling_device *cool_dev;
	struct ti_bandgap *bgp;
	enum thermal_device_mode mode;
	struct work_struct thermal_wq;
	int sensor_id;
	bool our_zone;
};

static void ti_thermal_work(struct work_struct *work)
{
	struct ti_thermal_data *data = container_of(work,
					struct ti_thermal_data, thermal_wq);

	thermal_zone_device_update(data->ti_thermal);

	dev_dbg(&data->ti_thermal->device, "updated thermal zone %s\n",
		data->ti_thermal->type);
}

/**
 * ti_thermal_hotspot_temperature - returns sensor extrapolated temperature
 * @t:	omap sensor temperature
 * @s:	omap sensor slope value
 * @c:	omap sensor const value
 */
static inline int ti_thermal_hotspot_temperature(int t, int s, int c)
{
	int delta = t * s / 1000 + c;

	if (delta < 0)
		delta = 0;

	return t + delta;
}

/* thermal zone ops */
/* Get temperature callback function for thermal zone*/
static inline int __ti_thermal_get_temp(void *devdata, long *temp)
{
	struct thermal_zone_device *pcb_tz = NULL;
	struct ti_thermal_data *data = devdata;
	struct ti_bandgap *bgp;
	const struct ti_temp_sensor *s;
	int ret, tmp, slope, constant;
	unsigned long pcb_temp;

	if (!data)
		return 0;

	bgp = data->bgp;
	s = &bgp->conf->sensors[data->sensor_id];

	ret = ti_bandgap_read_temperature(bgp, data->sensor_id, &tmp);
	if (ret)
		return ret;

	/* Default constants */
	slope = s->slope;
	constant = s->constant;

	pcb_tz = data->pcb_tz;
	/* In case pcb zone is available, use the extrapolation rule with it */
	if (!IS_ERR(pcb_tz)) {
		ret = thermal_zone_get_temp(pcb_tz, &pcb_temp);
		if (!ret) {
			tmp -= pcb_temp; /* got a valid PCB temp */
			slope = s->slope_pcb;
			constant = s->constant_pcb;
		} else {
			dev_err(bgp->dev,
				"Failed to read PCB state. Using defaults\n");
			ret = 0;
		}
	}
	*temp = ti_thermal_hotspot_temperature(tmp, slope, constant);

	return ret;
}

static inline int ti_thermal_get_temp(struct thermal_zone_device *thermal,
				      unsigned long *temp)
{
	struct ti_thermal_data *data = thermal->devdata;

	return __ti_thermal_get_temp(data, temp);
}

/* Bind callback functions for thermal zone */
static int ti_thermal_bind(struct thermal_zone_device *thermal,
			   struct thermal_cooling_device *cdev)
{
	struct ti_thermal_data *data = thermal->devdata;
	int id;

	if (!data || IS_ERR(data))
		return -ENODEV;

	/* check if this is the cooling device we registered */
	if (data->cool_dev != cdev)
		return 0;

	id = data->sensor_id;

	/* Simple thing, two trips, one passive another critical */
	return thermal_zone_bind_cooling_device(thermal, 0, cdev,
	/* bind with min and max states defined by cpu_cooling */
						THERMAL_NO_LIMIT,
						THERMAL_NO_LIMIT);
}

/* Unbind callback functions for thermal zone */
static int ti_thermal_unbind(struct thermal_zone_device *thermal,
			     struct thermal_cooling_device *cdev)
{
	struct ti_thermal_data *data = thermal->devdata;

	if (!data || IS_ERR(data))
		return -ENODEV;

	/* check if this is the cooling device we registered */
	if (data->cool_dev != cdev)
		return 0;

	/* Simple thing, two trips, one passive another critical */
	return thermal_zone_unbind_cooling_device(thermal, 0, cdev);
}

/* Get mode callback functions for thermal zone */
static int ti_thermal_get_mode(struct thermal_zone_device *thermal,
			       enum thermal_device_mode *mode)
{
	struct ti_thermal_data *data = thermal->devdata;

	if (data)
		*mode = data->mode;

	return 0;
}

/* Set mode callback functions for thermal zone */
static int ti_thermal_set_mode(struct thermal_zone_device *thermal,
			       enum thermal_device_mode mode)
{
	struct ti_thermal_data *data = thermal->devdata;
	struct ti_bandgap *bgp;

	bgp = data->bgp;

	if (!data->ti_thermal) {
		dev_notice(&thermal->device, "thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&data->ti_thermal->lock);

	if (mode == THERMAL_DEVICE_ENABLED)
		data->ti_thermal->polling_delay = FAST_TEMP_MONITORING_RATE;
	else
		data->ti_thermal->polling_delay = 0;

	mutex_unlock(&data->ti_thermal->lock);

	data->mode = mode;
	ti_bandgap_write_update_interval(bgp, data->sensor_id,
					data->ti_thermal->polling_delay);
	thermal_zone_device_update(data->ti_thermal);
	dev_dbg(&thermal->device, "thermal polling set for duration=%d msec\n",
		data->ti_thermal->polling_delay);

	return 0;
}

/* Get trip type callback functions for thermal zone */
static int ti_thermal_get_trip_type(struct thermal_zone_device *thermal,
				    int trip, enum thermal_trip_type *type)
{
	if (!ti_thermal_is_valid_trip(trip))
		return -EINVAL;

	if (trip + 1 == OMAP_TRIP_NUMBER)
		*type = THERMAL_TRIP_CRITICAL;
	else
		*type = THERMAL_TRIP_PASSIVE;

	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int ti_thermal_get_trip_temp(struct thermal_zone_device *thermal,
				    int trip, unsigned long *temp)
{
	if (!ti_thermal_is_valid_trip(trip))
		return -EINVAL;

	*temp = ti_thermal_get_trip_value(trip);

	return 0;
}

static int __ti_thermal_get_trend(void *p, long *trend)
{
	struct ti_thermal_data *data = p;
	struct ti_bandgap *bgp;
	int id, tr, ret = 0;

	bgp = data->bgp;
	id = data->sensor_id;

	ret = ti_bandgap_get_trend(bgp, id, &tr);
	if (ret)
		return ret;

	*trend = tr;

	return 0;
}

/* Get the temperature trend callback functions for thermal zone */
static int ti_thermal_get_trend(struct thermal_zone_device *thermal,
				int trip, enum thermal_trend *trend)
{
	int ret;
	long tr;

	ret = __ti_thermal_get_trend(thermal->devdata, &tr);
	if (ret)
		return ret;

	if (tr > 0)
		*trend = THERMAL_TREND_RAISING;
	else if (tr < 0)
		*trend = THERMAL_TREND_DROPPING;
	else
		*trend = THERMAL_TREND_STABLE;

	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int ti_thermal_get_crit_temp(struct thermal_zone_device *thermal,
				    unsigned long *temp)
{
	/* shutdown zone */
	return ti_thermal_get_trip_temp(thermal, OMAP_TRIP_NUMBER - 1, temp);
}

static struct thermal_zone_device_ops ti_thermal_ops = {
	.get_temp = ti_thermal_get_temp,
	.get_trend = ti_thermal_get_trend,
	.bind = ti_thermal_bind,
	.unbind = ti_thermal_unbind,
	.get_mode = ti_thermal_get_mode,
	.set_mode = ti_thermal_set_mode,
	.get_trip_type = ti_thermal_get_trip_type,
	.get_trip_temp = ti_thermal_get_trip_temp,
	.get_crit_temp = ti_thermal_get_crit_temp,
};

static struct ti_thermal_data
*ti_thermal_build_data(struct ti_bandgap *bgp, int id)
{
	struct ti_thermal_data *data;

	data = devm_kzalloc(bgp->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(bgp->dev, "kzalloc fail\n");
		return NULL;
	}
	data->sensor_id = id;
	data->bgp = bgp;
	data->mode = THERMAL_DEVICE_ENABLED;
	/* pcb_tz will be either valid or PTR_ERR() */
	data->pcb_tz = thermal_zone_get_zone_by_name("pcb");
	INIT_WORK(&data->thermal_wq, ti_thermal_work);

	return data;
}

int ti_thermal_expose_sensor(struct ti_bandgap *bgp, int id,
			     char *domain)
{
	struct ti_thermal_data *data;

	data = ti_bandgap_get_sensor_data(bgp, id);

	if (!data || IS_ERR(data))
		data = ti_thermal_build_data(bgp, id);

	if (!data)
		return -EINVAL;

	/* in case this is specified by DT */
	data->ti_thermal = thermal_zone_of_sensor_register(bgp->dev, id,
					data, __ti_thermal_get_temp,
					__ti_thermal_get_trend);
	if (IS_ERR(data->ti_thermal)) {
		/* Create thermal zone */
		data->ti_thermal = thermal_zone_device_register(domain,
				OMAP_TRIP_NUMBER, 0, data, &ti_thermal_ops,
				NULL, FAST_TEMP_MONITORING_RATE,
				FAST_TEMP_MONITORING_RATE);
		if (IS_ERR(data->ti_thermal)) {
			dev_err(bgp->dev, "thermal zone device is NULL\n");
			return PTR_ERR(data->ti_thermal);
		}
		data->ti_thermal->polling_delay = FAST_TEMP_MONITORING_RATE;
		data->our_zone = true;
	}
	ti_bandgap_set_sensor_data(bgp, id, data);
	ti_bandgap_write_update_interval(bgp, data->sensor_id,
					data->ti_thermal->polling_delay);

	return 0;
}

int ti_thermal_remove_sensor(struct ti_bandgap *bgp, int id)
{
	struct ti_thermal_data *data;

	data = ti_bandgap_get_sensor_data(bgp, id);

	if (data && data->ti_thermal) {
		if (data->our_zone)
			thermal_zone_device_unregister(data->ti_thermal);
		else
			thermal_zone_of_sensor_unregister(bgp->dev,
							  data->ti_thermal);
	}

	return 0;
}

int ti_thermal_report_sensor_temperature(struct ti_bandgap *bgp, int id)
{
	struct ti_thermal_data *data;

	data = ti_bandgap_get_sensor_data(bgp, id);

	schedule_work(&data->thermal_wq);

	return 0;
}

int ti_thermal_register_cpu_cooling(struct ti_bandgap *bgp, int id)
{
	struct ti_thermal_data *data;
	struct device_node *np = bgp->dev->of_node;

	/*
	 * We are assuming here that if one deploys the zone
	 * using DT, then it must be aware that the cooling device
	 * loading has to happen via cpufreq driver.
	 */
	if (of_find_property(np, "#thermal-sensor-cells", NULL))
		return 0;

	data = ti_bandgap_get_sensor_data(bgp, id);
	if (!data || IS_ERR(data))
		data = ti_thermal_build_data(bgp, id);

	if (!data)
		return -EINVAL;

	/* Register cooling device */
	data->cool_dev = cpufreq_cooling_register(cpu_present_mask);
	if (IS_ERR(data->cool_dev)) {
		int ret = PTR_ERR(data->cool_dev);

		if (ret != -EPROBE_DEFER)
			dev_err(bgp->dev,
				"Failed to register cpu cooling device %d\n",
				ret);

		return ret;
	}
	ti_bandgap_set_sensor_data(bgp, id, data);

	return 0;
}

int ti_thermal_unregister_cpu_cooling(struct ti_bandgap *bgp, int id)
{
	struct ti_thermal_data *data;

	data = ti_bandgap_get_sensor_data(bgp, id);

	if (data && data->cool_dev)
		cpufreq_cooling_unregister(data->cool_dev);

	return 0;
}

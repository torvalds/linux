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
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/cpu_cooling.h>

#include "omap-thermal.h"
#include "omap-bandgap.h"

/* common data structures */
struct omap_thermal_data {
	struct thermal_zone_device *omap_thermal;
	struct thermal_cooling_device *cool_dev;
	struct omap_bandgap *bg_ptr;
	enum thermal_device_mode mode;
	struct work_struct thermal_wq;
	int sensor_id;
};

static void omap_thermal_work(struct work_struct *work)
{
	struct omap_thermal_data *data = container_of(work,
					struct omap_thermal_data, thermal_wq);

	thermal_zone_device_update(data->omap_thermal);

	dev_dbg(&data->omap_thermal->device, "updated thermal zone %s\n",
		data->omap_thermal->type);
}

/**
 * omap_thermal_hotspot_temperature - returns sensor extrapolated temperature
 * @t:	omap sensor temperature
 * @s:	omap sensor slope value
 * @c:	omap sensor const value
 */
static inline int omap_thermal_hotspot_temperature(int t, int s, int c)
{
	int delta = t * s / 1000 + c;

	if (delta < 0)
		delta = 0;

	return t + delta;
}

/* thermal zone ops */
/* Get temperature callback function for thermal zone*/
static inline int omap_thermal_get_temp(struct thermal_zone_device *thermal,
					 unsigned long *temp)
{
	struct omap_thermal_data *data = thermal->devdata;
	struct omap_bandgap *bg_ptr;
	struct omap_temp_sensor *s;
	int ret, tmp, pcb_temp, slope, constant;

	if (!data)
		return 0;

	bg_ptr = data->bg_ptr;
	s = &bg_ptr->conf->sensors[data->sensor_id];

	ret = omap_bandgap_read_temperature(bg_ptr, data->sensor_id, &tmp);
	if (ret)
		return ret;

	pcb_temp = 0;
	/* TODO: Introduce pcb temperature lookup */
	/* In case pcb zone is available, use the extrapolation rule with it */
	if (pcb_temp) {
		tmp -= pcb_temp;
		slope = s->slope_pcb;
		constant = s->constant_pcb;
	} else {
		slope = s->slope;
		constant = s->constant;
	}
	*temp = omap_thermal_hotspot_temperature(tmp, slope, constant);

	return ret;
}

/* Bind callback functions for thermal zone */
static int omap_thermal_bind(struct thermal_zone_device *thermal,
			      struct thermal_cooling_device *cdev)
{
	struct omap_thermal_data *data = thermal->devdata;
	int id;

	if (IS_ERR_OR_NULL(data))
		return -ENODEV;

	/* check if this is the cooling device we registered */
	if (data->cool_dev != cdev)
		return 0;

	id = data->sensor_id;

	/* TODO: bind with min and max states */
	/* Simple thing, two trips, one passive another critical */
	return thermal_zone_bind_cooling_device(thermal, 0, cdev,
						THERMAL_NO_LIMIT,
						THERMAL_NO_LIMIT);
}

/* Unbind callback functions for thermal zone */
static int omap_thermal_unbind(struct thermal_zone_device *thermal,
				struct thermal_cooling_device *cdev)
{
	struct omap_thermal_data *data = thermal->devdata;

	if (IS_ERR_OR_NULL(data))
		return -ENODEV;

	/* check if this is the cooling device we registered */
	if (data->cool_dev != cdev)
		return 0;

	/* Simple thing, two trips, one passive another critical */
	return thermal_zone_unbind_cooling_device(thermal, 0, cdev);
}

/* Get mode callback functions for thermal zone */
static int omap_thermal_get_mode(struct thermal_zone_device *thermal,
				  enum thermal_device_mode *mode)
{
	struct omap_thermal_data *data = thermal->devdata;

	if (data)
		*mode = data->mode;

	return 0;
}

/* Set mode callback functions for thermal zone */
static int omap_thermal_set_mode(struct thermal_zone_device *thermal,
				  enum thermal_device_mode mode)
{
	struct omap_thermal_data *data = thermal->devdata;

	if (!data->omap_thermal) {
		dev_notice(&thermal->device, "thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&data->omap_thermal->lock);

	if (mode == THERMAL_DEVICE_ENABLED)
		data->omap_thermal->polling_delay = FAST_TEMP_MONITORING_RATE;
	else
		data->omap_thermal->polling_delay = 0;

	mutex_unlock(&data->omap_thermal->lock);

	data->mode = mode;
	thermal_zone_device_update(data->omap_thermal);
	dev_dbg(&thermal->device, "thermal polling set for duration=%d msec\n",
		data->omap_thermal->polling_delay);

	return 0;
}

/* Get trip type callback functions for thermal zone */
static int omap_thermal_get_trip_type(struct thermal_zone_device *thermal,
				       int trip, enum thermal_trip_type *type)
{
	if (!omap_thermal_is_valid_trip(trip))
		return -EINVAL;

	if (trip + 1 == OMAP_TRIP_NUMBER)
		*type = THERMAL_TRIP_CRITICAL;
	else
		*type = THERMAL_TRIP_PASSIVE;

	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int omap_thermal_get_trip_temp(struct thermal_zone_device *thermal,
				       int trip, unsigned long *temp)
{
	if (!omap_thermal_is_valid_trip(trip))
		return -EINVAL;

	*temp = omap_thermal_get_trip_value(trip);

	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int omap_thermal_get_crit_temp(struct thermal_zone_device *thermal,
				       unsigned long *temp)
{
	/* shutdown zone */
	return omap_thermal_get_trip_temp(thermal, OMAP_TRIP_NUMBER - 1, temp);
}

static struct thermal_zone_device_ops omap_thermal_ops = {
	.get_temp = omap_thermal_get_temp,
	/* TODO: add .get_trend */
	.bind = omap_thermal_bind,
	.unbind = omap_thermal_unbind,
	.get_mode = omap_thermal_get_mode,
	.set_mode = omap_thermal_set_mode,
	.get_trip_type = omap_thermal_get_trip_type,
	.get_trip_temp = omap_thermal_get_trip_temp,
	.get_crit_temp = omap_thermal_get_crit_temp,
};

static struct omap_thermal_data
*omap_thermal_build_data(struct omap_bandgap *bg_ptr, int id)
{
	struct omap_thermal_data *data;

	data = devm_kzalloc(bg_ptr->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(bg_ptr->dev, "kzalloc fail\n");
		return NULL;
	}
	data->sensor_id = id;
	data->bg_ptr = bg_ptr;
	data->mode = THERMAL_DEVICE_ENABLED;
	INIT_WORK(&data->thermal_wq, omap_thermal_work);

	return data;
}

int omap_thermal_expose_sensor(struct omap_bandgap *bg_ptr, int id,
			       char *domain)
{
	struct omap_thermal_data *data;

	data = omap_bandgap_get_sensor_data(bg_ptr, id);

	if (IS_ERR(data))
		data = omap_thermal_build_data(bg_ptr, id);

	if (!data)
		return -EINVAL;

	/* TODO: remove TC1 TC2 */
	/* Create thermal zone */
	data->omap_thermal = thermal_zone_device_register(domain,
				OMAP_TRIP_NUMBER, 0, data, &omap_thermal_ops,
				NULL, FAST_TEMP_MONITORING_RATE,
				FAST_TEMP_MONITORING_RATE);
	if (IS_ERR_OR_NULL(data->omap_thermal)) {
		dev_err(bg_ptr->dev, "thermal zone device is NULL\n");
		return PTR_ERR(data->omap_thermal);
	}
	data->omap_thermal->polling_delay = FAST_TEMP_MONITORING_RATE;
	omap_bandgap_set_sensor_data(bg_ptr, id, data);

	return 0;
}

int omap_thermal_remove_sensor(struct omap_bandgap *bg_ptr, int id)
{
	struct omap_thermal_data *data;

	data = omap_bandgap_get_sensor_data(bg_ptr, id);

	thermal_zone_device_unregister(data->omap_thermal);

	return 0;
}

int omap_thermal_report_sensor_temperature(struct omap_bandgap *bg_ptr, int id)
{
	struct omap_thermal_data *data;

	data = omap_bandgap_get_sensor_data(bg_ptr, id);

	schedule_work(&data->thermal_wq);

	return 0;
}

int omap_thermal_register_cpu_cooling(struct omap_bandgap *bg_ptr, int id)
{
	struct omap_thermal_data *data;

	data = omap_bandgap_get_sensor_data(bg_ptr, id);
	if (IS_ERR(data))
		data = omap_thermal_build_data(bg_ptr, id);

	if (!data)
		return -EINVAL;

	/* Register cooling device */
	data->cool_dev = cpufreq_cooling_register(cpu_present_mask);
	if (IS_ERR_OR_NULL(data->cool_dev)) {
		dev_err(bg_ptr->dev,
			"Failed to register cpufreq cooling device\n");
		return PTR_ERR(data->cool_dev);
	}
	omap_bandgap_set_sensor_data(bg_ptr, id, data);

	return 0;
}

int omap_thermal_unregister_cpu_cooling(struct omap_bandgap *bg_ptr, int id)
{
	struct omap_thermal_data *data;

	data = omap_bandgap_get_sensor_data(bg_ptr, id);
	cpufreq_cooling_unregister(data->cool_dev);

	return 0;
}

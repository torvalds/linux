/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2019 Intel Corporation */

#ifndef __PECI_HWMON_H
#define __PECI_HWMON_H

#include <linux/peci.h>

#define TEMP_TYPE_PECI			6 /* Sensor type 6: Intel PECI */
#define UPDATE_INTERVAL			HZ

#define PECI_HWMON_LABEL_STR_LEN	10

/**
 * struct peci_sensor_data - PECI sensor information
 * @valid: flag to indicate the sensor value is valid
 * @value: sensor value in millidegree Celsius
 * @last_updated: time of the last update in jiffies
 */
struct peci_sensor_data {
	uint  valid;
	s32   value;
	ulong last_updated;
};

/**
 * peci_sensor_need_update - check whether sensor update is needed or not
 * @sensor: pointer to sensor data struct
 *
 * Return: true if update is needed, false if not.
 */
static inline bool peci_sensor_need_update(struct peci_sensor_data *sensor)
{
	return !sensor->valid ||
	       time_after(jiffies, sensor->last_updated + UPDATE_INTERVAL);
}

/**
 * peci_sensor_mark_updated - mark the sensor is updated
 * @sensor: pointer to sensor data struct
 */
static inline void peci_sensor_mark_updated(struct peci_sensor_data *sensor)
{
	sensor->valid = 1;
	sensor->last_updated = jiffies;
}

#endif /* __PECI_HWMON_H */

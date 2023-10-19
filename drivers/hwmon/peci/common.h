/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021 Intel Corporation */

#include <linux/mutex.h>
#include <linux/types.h>

#ifndef __PECI_HWMON_COMMON_H
#define __PECI_HWMON_COMMON_H

#define PECI_HWMON_UPDATE_INTERVAL	HZ

/**
 * struct peci_sensor_state - PECI state information
 * @valid: flag to indicate the sensor value is valid
 * @last_updated: time of the last update in jiffies
 * @lock: mutex to protect sensor access
 */
struct peci_sensor_state {
	bool valid;
	unsigned long last_updated;
	struct mutex lock; /* protect sensor access */
};

/**
 * struct peci_sensor_data - PECI sensor information
 * @value: sensor value in milli units
 * @state: sensor update state
 */

struct peci_sensor_data {
	s32 value;
	struct peci_sensor_state state;
};

/**
 * peci_sensor_need_update() - check whether sensor update is needed or not
 * @sensor: pointer to sensor data struct
 *
 * Return: true if update is needed, false if not.
 */

static inline bool peci_sensor_need_update(struct peci_sensor_state *state)
{
	return !state->valid ||
	       time_after(jiffies, state->last_updated + PECI_HWMON_UPDATE_INTERVAL);
}

/**
 * peci_sensor_mark_updated() - mark the sensor is updated
 * @sensor: pointer to sensor data struct
 */
static inline void peci_sensor_mark_updated(struct peci_sensor_state *state)
{
	state->valid = true;
	state->last_updated = jiffies;
}

#endif /* __PECI_HWMON_COMMON_H */

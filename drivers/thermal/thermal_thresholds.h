/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __THERMAL_THRESHOLDS_H__
#define __THERMAL_THRESHOLDS_H__

struct user_threshold {
	struct list_head list_node;
	int temperature;
	int direction;
};

int thermal_thresholds_init(struct thermal_zone_device *tz);
void thermal_thresholds_exit(struct thermal_zone_device *tz);
void thermal_thresholds_handle(struct thermal_zone_device *tz, int *low, int *high);
void thermal_thresholds_flush(struct thermal_zone_device *tz);
int thermal_thresholds_add(struct thermal_zone_device *tz, int temperature, int direction);
int thermal_thresholds_delete(struct thermal_zone_device *tz, int temperature, int direction);
int thermal_thresholds_for_each(struct thermal_zone_device *tz,
				int (*cb)(struct user_threshold *, void *arg), void *arg);
#endif

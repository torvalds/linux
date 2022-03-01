/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) Linaro Ltd 2020
 *  Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 */

/* Netlink notification function */
#ifdef CONFIG_THERMAL_NETLINK
int __init thermal_netlink_init(void);
int thermal_notify_tz_create(int tz_id, const char *name);
int thermal_notify_tz_delete(int tz_id);
int thermal_notify_tz_enable(int tz_id);
int thermal_notify_tz_disable(int tz_id);
int thermal_notify_tz_trip_down(int tz_id, int id, int temp);
int thermal_notify_tz_trip_up(int tz_id, int id, int temp);
int thermal_notify_tz_trip_delete(int tz_id, int id);
int thermal_notify_tz_trip_add(int tz_id, int id, int type,
			       int temp, int hyst);
int thermal_notify_tz_trip_change(int tz_id, int id, int type,
				  int temp, int hyst);
int thermal_notify_cdev_state_update(int cdev_id, int state);
int thermal_notify_cdev_add(int cdev_id, const char *name, int max_state);
int thermal_notify_cdev_delete(int cdev_id);
int thermal_notify_tz_gov_change(int tz_id, const char *name);
int thermal_genl_sampling_temp(int id, int temp);
#else
static inline int thermal_netlink_init(void)
{
	return 0;
}

static inline int thermal_notify_tz_create(int tz_id, const char *name)
{
	return 0;
}

static inline int thermal_notify_tz_delete(int tz_id)
{
	return 0;
}

static inline int thermal_notify_tz_enable(int tz_id)
{
	return 0;
}

static inline int thermal_notify_tz_disable(int tz_id)
{
	return 0;
}

static inline int thermal_notify_tz_trip_down(int tz_id, int id, int temp)
{
	return 0;
}

static inline int thermal_notify_tz_trip_up(int tz_id, int id, int temp)
{
	return 0;
}

static inline int thermal_notify_tz_trip_delete(int tz_id, int id)
{
	return 0;
}

static inline int thermal_notify_tz_trip_add(int tz_id, int id, int type,
					     int temp, int hyst)
{
	return 0;
}

static inline int thermal_notify_tz_trip_change(int tz_id, int id, int type,
						int temp, int hyst)
{
	return 0;
}

static inline int thermal_notify_cdev_state_update(int cdev_id, int state)
{
	return 0;
}

static inline int thermal_notify_cdev_add(int cdev_id, const char *name,
					  int max_state)
{
	return 0;
}

static inline int thermal_notify_cdev_delete(int cdev_id)
{
	return 0;
}

static inline int thermal_notify_tz_gov_change(int tz_id, const char *name)
{
	return 0;
}

static inline int thermal_genl_sampling_temp(int id, int temp)
{
	return 0;
}
#endif /* CONFIG_THERMAL_NETLINK */

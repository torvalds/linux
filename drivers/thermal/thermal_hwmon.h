/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  thermal_hwmon.h - Generic Thermal Management hwmon support.
 *
 *  Code based on Intel thermal_core.c. Copyrights of the original code:
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 *
 *  Copyright (C) 2013 Texas Instruments
 *  Copyright (C) 2013 Eduardo Valentin <eduardo.valentin@ti.com>
 */
#ifndef __THERMAL_HWMON_H__
#define __THERMAL_HWMON_H__

#include <linux/thermal.h>

#ifdef CONFIG_THERMAL_HWMON
int thermal_add_hwmon_sysfs(struct thermal_zone_device *tz);
void thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz);
#else
static int
thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
{
	return 0;
}

static void
thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
{
}
#endif

#endif /* __THERMAL_HWMON_H__ */

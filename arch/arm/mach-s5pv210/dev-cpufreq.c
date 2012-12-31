/*
 * linux/arch/arm/mach-s5pv210/dev-cpufreq.c
 *
 *  Copyright (c) 2008-2010 Samsung Electronics
 *  Taekki Kim <taekki.kim@samsung.com>
 *
 * S5PV210 series device definition for cpufreq devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <mach/cpu-freq-v210.h>

struct platform_device s5pv210_device_cpufreq = {
	.name	= "s5pv210-cpufreq",
	.id		= -1,
};

void s5pv210_cpufreq_set_platdata(struct s5pv210_cpufreq_data *pdata)
{
	s5pv210_device_cpufreq.dev.platform_data = pdata;
}


/*
 * SH-Mobile Timer
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2002 - 2009  Paul Mundt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/platform_device.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/of_address.h>

#include "common.h"

void __init shmobile_init_delay(void)
{
	struct device_node *np, *cpus;
	u32 max_freq = 0;

	cpus = of_find_node_by_path("/cpus");
	if (!cpus)
		return;

	for_each_child_of_node(cpus, np) {
		u32 freq;

		if (!of_property_read_u32(np, "clock-frequency", &freq))
			max_freq = max(max_freq, freq);
	}

	of_node_put(cpus);

	if (!max_freq)
		return;

	/*
	 * Calculate a worst-case loops-per-jiffy value
	 * based on maximum cpu core hz setting and the
	 * __delay() implementation in arch/arm/lib/delay.S.
	 *
	 * This will result in a longer delay than expected
	 * when the cpu core runs on lower frequencies.
	 */

	if (!preset_lpj)
		preset_lpj = max_freq / HZ;
}

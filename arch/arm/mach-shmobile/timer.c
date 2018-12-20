// SPDX-License-Identifier: GPL-2.0
/*
 * SH-Mobile Timer
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2002 - 2009  Paul Mundt
 */
#include <linux/platform_device.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/of_address.h>

#include "common.h"

void __init shmobile_init_delay(void)
{
	struct device_node *np;
	u32 max_freq = 0;

	for_each_of_cpu_node(np) {
		u32 freq;

		if (!of_property_read_u32(np, "clock-frequency", &freq))
			max_freq = max(max_freq, freq);
	}

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

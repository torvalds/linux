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

static void __init shmobile_setup_delay_hz(unsigned int max_cpu_core_hz,
					   unsigned int mult, unsigned int div)
{
	/* calculate a worst-case loops-per-jiffy value
	 * based on maximum cpu core hz setting and the
	 * __delay() implementation in arch/arm/lib/delay.S
	 *
	 * this will result in a longer delay than expected
	 * when the cpu core runs on lower frequencies.
	 */

	unsigned int value = HZ * div / mult;

	if (!preset_lpj)
		preset_lpj = max_cpu_core_hz / value;
}

void __init shmobile_init_delay(void)
{
	struct device_node *np, *cpus;
	bool is_a7_a8_a9 = false;
	bool is_a15 = false;
	u32 max_freq = 0;

	cpus = of_find_node_by_path("/cpus");
	if (!cpus)
		return;

	for_each_child_of_node(cpus, np) {
		u32 freq;

		if (!of_property_read_u32(np, "clock-frequency", &freq))
			max_freq = max(max_freq, freq);

		if (of_device_is_compatible(np, "arm,cortex-a7") ||
		    of_device_is_compatible(np, "arm,cortex-a8") ||
		    of_device_is_compatible(np, "arm,cortex-a9"))
			is_a7_a8_a9 = true;
		else if (of_device_is_compatible(np, "arm,cortex-a15"))
			is_a15 = true;
	}

	of_node_put(cpus);

	if (!max_freq)
		return;

	if (is_a7_a8_a9)
		shmobile_setup_delay_hz(max_freq, 1, 3);
	else if (is_a15 && !IS_ENABLED(CONFIG_ARM_ARCH_TIMER))
		shmobile_setup_delay_hz(max_freq, 2, 4);
}

static void __init shmobile_late_time_init(void)
{
	/*
	 * Make sure all compiled-in early timers register themselves.
	 *
	 * Run probe() for two "earlytimer" devices, these will be the
	 * clockevents and clocksource devices respectively. In the event
	 * that only a clockevents device is available, we -ENODEV on the
	 * clocksource and the jiffies clocksource is used transparently
	 * instead. No error handling is necessary here.
	 */
	early_platform_driver_register_all("earlytimer");
	early_platform_driver_probe("earlytimer", 2, 0);
}

void __init shmobile_earlytimer_init(void)
{
	late_time_init = shmobile_late_time_init;
}


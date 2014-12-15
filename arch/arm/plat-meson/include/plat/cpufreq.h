/*
 * Amlogic CPUFreq platform support.
 *
 * Copyright (C) 2012 Amlogic, Inc. http://www.amlogic.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PLAT_MESON_CPUFREQ_H
#define __PLAT_MESON_CPUFREQ_H

#include <linux/cpufreq.h>

struct meson_cpufreq_config {
	struct cpufreq_frequency_table *freq_table;
	unsigned int (*cur_volt_max_freq)(void);
	int (*voltage_scale)(unsigned int frequency);
	int (*init)(void);
};

extern int meson_cpufreq_boost(unsigned int freq);

#endif

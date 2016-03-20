/*
 * Utility functions for parsing Tegra CVB voltage tables
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __DRIVERS_CLK_TEGRA_CVB_H
#define __DRIVERS_CLK_TEGRA_CVB_H

#include <linux/types.h>

struct device;

#define MAX_DVFS_FREQS	40

struct rail_alignment {
	int offset_uv;
	int step_uv;
};

struct cvb_coefficients {
	int c0;
	int c1;
	int c2;
};

struct cvb_table_freq_entry {
	unsigned long freq;
	struct cvb_coefficients coefficients;
};

struct cvb_cpu_dfll_data {
	u32 tune0_low;
	u32 tune0_high;
	u32 tune1;
};

struct cvb_table {
	int speedo_id;
	int process_id;

	int min_millivolts;
	int max_millivolts;
	struct rail_alignment alignment;

	int speedo_scale;
	int voltage_scale;
	struct cvb_table_freq_entry cvb_table[MAX_DVFS_FREQS];
	struct cvb_cpu_dfll_data cpu_dfll_data;
};

const struct cvb_table *tegra_cvb_build_opp_table(
		const struct cvb_table *cvb_tables,
		size_t sz, int process_id,
		int speedo_id, int speedo_value,
		unsigned long max_rate,
		struct device *opp_dev);

#endif

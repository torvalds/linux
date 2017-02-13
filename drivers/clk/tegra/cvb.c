/*
 * Utility functions for parsing Tegra CVB voltage tables
 *
 * Copyright (C) 2012-2014 NVIDIA Corporation.  All rights reserved.
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
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/pm_opp.h>

#include "cvb.h"

/* cvb_mv = ((c2 * speedo / s_scale + c1) * speedo / s_scale + c0) */
static inline int get_cvb_voltage(int speedo, int s_scale,
				  const struct cvb_coefficients *cvb)
{
	int mv;

	/* apply only speedo scale: output mv = cvb_mv * v_scale */
	mv = DIV_ROUND_CLOSEST(cvb->c2 * speedo, s_scale);
	mv = DIV_ROUND_CLOSEST((mv + cvb->c1) * speedo, s_scale) + cvb->c0;
	return mv;
}

static int round_cvb_voltage(int mv, int v_scale,
			     const struct rail_alignment *align)
{
	/* combined: apply voltage scale and round to cvb alignment step */
	int uv;
	int step = (align->step_uv ? : 1000) * v_scale;
	int offset = align->offset_uv * v_scale;

	uv = max(mv * 1000, offset) - offset;
	uv = DIV_ROUND_UP(uv, step) * align->step_uv + align->offset_uv;
	return uv / 1000;
}

enum {
	DOWN,
	UP
};

static int round_voltage(int mv, const struct rail_alignment *align, int up)
{
	if (align->step_uv) {
		int uv;

		uv = max(mv * 1000, align->offset_uv) - align->offset_uv;
		uv = (uv + (up ? align->step_uv - 1 : 0)) / align->step_uv;
		return (uv * align->step_uv + align->offset_uv) / 1000;
	}
	return mv;
}

static int build_opp_table(struct device *dev, const struct cvb_table *table,
			   int speedo_value, unsigned long max_freq)
{
	const struct rail_alignment *align = &table->alignment;
	int i, ret, dfll_mv, min_mv, max_mv;

	min_mv = round_voltage(table->min_millivolts, align, UP);
	max_mv = round_voltage(table->max_millivolts, align, DOWN);

	for (i = 0; i < MAX_DVFS_FREQS; i++) {
		const struct cvb_table_freq_entry *entry = &table->entries[i];

		if (!entry->freq || (entry->freq > max_freq))
			break;

		dfll_mv = get_cvb_voltage(speedo_value, table->speedo_scale,
					  &entry->coefficients);
		dfll_mv = round_cvb_voltage(dfll_mv, table->voltage_scale,
					    align);
		dfll_mv = clamp(dfll_mv, min_mv, max_mv);

		ret = dev_pm_opp_add(dev, entry->freq, dfll_mv * 1000);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * tegra_cvb_add_opp_table - build OPP table from Tegra CVB tables
 * @dev: the struct device * for which the OPP table is built
 * @tables: array of CVB tables
 * @count: size of the previously mentioned array
 * @process_id: process id of the HW module
 * @speedo_id: speedo id of the HW module
 * @speedo_value: speedo value of the HW module
 * @max_freq: highest safe clock rate
 *
 * On Tegra, a CVB table encodes the relationship between operating voltage
 * and safe maximal frequency for a given module (e.g. GPU or CPU). This
 * function calculates the optimal voltage-frequency operating points
 * for the given arguments and exports them via the OPP library for the
 * given @dev. Returns a pointer to the struct cvb_table that matched
 * or an ERR_PTR on failure.
 */
const struct cvb_table *
tegra_cvb_add_opp_table(struct device *dev, const struct cvb_table *tables,
			size_t count, int process_id, int speedo_id,
			int speedo_value, unsigned long max_freq)
{
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		const struct cvb_table *table = &tables[i];

		if (table->speedo_id != -1 && table->speedo_id != speedo_id)
			continue;

		if (table->process_id != -1 && table->process_id != process_id)
			continue;

		ret = build_opp_table(dev, table, speedo_value, max_freq);
		return ret ? ERR_PTR(ret) : table;
	}

	return ERR_PTR(-EINVAL);
}

void tegra_cvb_remove_opp_table(struct device *dev,
				const struct cvb_table *table,
				unsigned long max_freq)
{
	unsigned int i;

	for (i = 0; i < MAX_DVFS_FREQS; i++) {
		const struct cvb_table_freq_entry *entry = &table->entries[i];

		if (!entry->freq || (entry->freq > max_freq))
			break;

		dev_pm_opp_remove(dev, entry->freq);
	}
}

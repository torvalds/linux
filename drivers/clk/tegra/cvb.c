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

static int build_opp_table(const struct cvb_table *d,
			   int speedo_value,
			   unsigned long max_freq,
			   struct device *opp_dev)
{
	int i, ret, dfll_mv, min_mv, max_mv;
	const struct cvb_table_freq_entry *table = NULL;
	const struct rail_alignment *align = &d->alignment;

	min_mv = round_voltage(d->min_millivolts, align, UP);
	max_mv = round_voltage(d->max_millivolts, align, DOWN);

	for (i = 0; i < MAX_DVFS_FREQS; i++) {
		table = &d->cvb_table[i];
		if (!table->freq || (table->freq > max_freq))
			break;

		dfll_mv = get_cvb_voltage(
			speedo_value, d->speedo_scale, &table->coefficients);
		dfll_mv = round_cvb_voltage(dfll_mv, d->voltage_scale, align);
		dfll_mv = clamp(dfll_mv, min_mv, max_mv);

		ret = dev_pm_opp_add(opp_dev, table->freq, dfll_mv * 1000);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * tegra_cvb_build_opp_table - build OPP table from Tegra CVB tables
 * @cvb_tables: array of CVB tables
 * @sz: size of the previously mentioned array
 * @process_id: process id of the HW module
 * @speedo_id: speedo id of the HW module
 * @speedo_value: speedo value of the HW module
 * @max_rate: highest safe clock rate
 * @opp_dev: the struct device * for which the OPP table is built
 *
 * On Tegra, a CVB table encodes the relationship between operating voltage
 * and safe maximal frequency for a given module (e.g. GPU or CPU). This
 * function calculates the optimal voltage-frequency operating points
 * for the given arguments and exports them via the OPP library for the
 * given @opp_dev. Returns a pointer to the struct cvb_table that matched
 * or an ERR_PTR on failure.
 */
const struct cvb_table *tegra_cvb_build_opp_table(
		const struct cvb_table *cvb_tables,
		size_t sz, int process_id,
		int speedo_id, int speedo_value,
		unsigned long max_rate,
		struct device *opp_dev)
{
	int i, ret;

	for (i = 0; i < sz; i++) {
		const struct cvb_table *d = &cvb_tables[i];

		if (d->speedo_id != -1 && d->speedo_id != speedo_id)
			continue;
		if (d->process_id != -1 && d->process_id != process_id)
			continue;

		ret = build_opp_table(d, speedo_value, max_rate, opp_dev);
		return ret ? ERR_PTR(ret) : d;
	}

	return ERR_PTR(-EINVAL);
}

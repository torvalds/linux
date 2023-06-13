// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include "qg-profile-lib.h"
#include "qg-defs.h"

int qg_linear_interpolate(int y0, int x0, int y1, int x1, int x)
{
	if (y0 == y1 || x == x0)
		return y0;
	if (x1 == x0 || x == x1)
		return y1;

	return y0 + ((y1 - y0) * (x - x0) / (x1 - x0));
}

int qg_interpolate_single_row_lut(struct profile_table_data *lut,
						int x, int scale)
{
	int i, result;
	int cols = lut->cols;

	if (x < lut->col_entries[0] * scale) {
		pr_debug("x %d less than known range return y = %d lut = %s\n",
					x, lut->data[0][0], lut->name);
		return lut->data[0][0];
	}

	if (x > lut->col_entries[cols-1] * scale) {
		pr_debug("x %d more than known range return y = %d lut = %s\n",
					x, lut->data[0][cols-1], lut->name);
		return lut->data[0][cols-1];
	}

	for (i = 0; i < cols; i++) {
		if (x <= lut->col_entries[i] * scale)
			break;
	}

	if (x == lut->col_entries[i] * scale) {
		result = lut->data[0][i];
	} else {
		result = qg_linear_interpolate(
			lut->data[0][i-1],
			lut->col_entries[i-1] * scale,
			lut->data[0][i],
			lut->col_entries[i] * scale,
			x);
	}

	return result;
}

int qg_interpolate_soc(struct profile_table_data *lut,
				int batt_temp, int ocv)
{
	int i, j, soc_high, soc_low, soc;
	int rows = lut->rows;
	int cols = lut->cols;

	if (batt_temp < lut->col_entries[0] * DEGC_SCALE) {
		pr_debug("batt_temp %d < known temp range\n", batt_temp);
		batt_temp = lut->col_entries[0] * DEGC_SCALE;
	}

	if (batt_temp > lut->col_entries[cols - 1] * DEGC_SCALE) {
		pr_debug("batt_temp %d > known temp range\n", batt_temp);
		batt_temp = lut->col_entries[cols - 1] * DEGC_SCALE;
	}

	for (j = 0; j < cols; j++)
		if (batt_temp <= lut->col_entries[j] * DEGC_SCALE)
			break;

	if (batt_temp == lut->col_entries[j] * DEGC_SCALE) {
		/* found an exact match for temp in the table */
		if (ocv >= lut->data[0][j])
			return lut->row_entries[0];
		if (ocv <= lut->data[rows - 1][j])
			return lut->row_entries[rows - 1];
		for (i = 0; i < rows; i++) {
			if (ocv >= lut->data[i][j]) {
				if (ocv == lut->data[i][j])
					return lut->row_entries[i];
				soc = qg_linear_interpolate(
					lut->row_entries[i],
					lut->data[i][j],
					lut->row_entries[i - 1],
					lut->data[i - 1][j],
					ocv);
				return soc;
			}
		}
	}

	/* batt_temp is within temperature for column j-1 and j */
	if (ocv >= lut->data[0][j])
		return lut->row_entries[0];
	if (ocv <= lut->data[rows - 1][j - 1])
		return lut->row_entries[rows - 1];

	soc_low = soc_high = 0;
	for (i = 0; i < rows-1; i++) {
		if (soc_high == 0 && is_between(lut->data[i][j],
				lut->data[i+1][j], ocv)) {
			soc_high = qg_linear_interpolate(
				lut->row_entries[i],
				lut->data[i][j],
				lut->row_entries[i + 1],
				lut->data[i+1][j],
				ocv);
		}

		if (soc_low == 0 && is_between(lut->data[i][j-1],
				lut->data[i+1][j-1], ocv)) {
			soc_low = qg_linear_interpolate(
				lut->row_entries[i],
				lut->data[i][j-1],
				lut->row_entries[i + 1],
				lut->data[i+1][j-1],
				ocv);
		}

		if (soc_high && soc_low) {
			soc = qg_linear_interpolate(
				soc_low,
				lut->col_entries[j-1] * DEGC_SCALE,
				soc_high,
				lut->col_entries[j] * DEGC_SCALE,
				batt_temp);
			return soc;
		}
	}

	if (soc_high)
		return soc_high;

	if (soc_low)
		return soc_low;

	pr_debug("%d ocv wasn't found for temp %d in the LUT %s returning 100%%\n",
						ocv, batt_temp, lut->name);
	return 10000;
}

int qg_interpolate_var(struct profile_table_data *lut,
				int batt_temp, int soc)
{
	int i, var1, var2, var, rows, cols;
	int row1 = 0;
	int row2 = 0;

	rows = lut->rows;
	cols = lut->cols;
	if (soc > lut->row_entries[0]) {
		pr_debug("soc %d greater than known soc ranges for %s lut\n",
							soc, lut->name);
		row1 = 0;
		row2 = 0;
	} else if (soc < lut->row_entries[rows - 1]) {
		pr_debug("soc %d less than known soc ranges for %s lut\n",
							soc, lut->name);
		row1 = rows - 1;
		row2 = rows - 1;
	} else {
		for (i = 0; i < rows; i++) {
			if (soc == lut->row_entries[i]) {
				row1 = i;
				row2 = i;
				break;
			}
			if (soc > lut->row_entries[i]) {
				row1 = i - 1;
				row2 = i;
				break;
			}
		}
	}

	if (batt_temp < lut->col_entries[0] * DEGC_SCALE)
		batt_temp = lut->col_entries[0] * DEGC_SCALE;
	if (batt_temp > lut->col_entries[cols - 1] * DEGC_SCALE)
		batt_temp = lut->col_entries[cols - 1] * DEGC_SCALE;

	for (i = 0; i < cols; i++)
		if (batt_temp <= lut->col_entries[i] * DEGC_SCALE)
			break;

	if (batt_temp == lut->col_entries[i] * DEGC_SCALE) {
		var = qg_linear_interpolate(
				lut->data[row1][i],
				lut->row_entries[row1],
				lut->data[row2][i],
				lut->row_entries[row2],
				soc);
		return var;
	}

	var1 = qg_linear_interpolate(
				lut->data[row1][i - 1],
				lut->col_entries[i - 1] * DEGC_SCALE,
				lut->data[row1][i],
				lut->col_entries[i] * DEGC_SCALE,
				batt_temp);

	var2 = qg_linear_interpolate(
				lut->data[row2][i - 1],
				lut->col_entries[i - 1] * DEGC_SCALE,
				lut->data[row2][i],
				lut->col_entries[i] * DEGC_SCALE,
				batt_temp);

	var = qg_linear_interpolate(
				var1,
				lut->row_entries[row1],
				var2,
				lut->row_entries[row2],
				soc);

	return var;
}

int qg_interpolate_slope(struct profile_table_data *lut,
					int batt_temp, int soc)
{
	int i, ocvrow1, ocvrow2, rows, cols;
	int row1 = 0;
	int row2 = 0;
	int slope;

	rows = lut->rows;
	cols = lut->cols;
	if (soc >= lut->row_entries[0]) {
		pr_debug("soc %d >= max soc range - use the slope at soc=%d for lut %s\n",
					soc, lut->row_entries[0], lut->name);
		row1 = 0;
		row2 = 1;
	} else if (soc <= lut->row_entries[rows - 1]) {
		pr_debug("soc %d is <= min soc range - use the slope at soc=%d for lut %s\n",
				soc, lut->row_entries[rows - 1], lut->name);
		row1 = rows - 2;
		row2 = rows - 1;
	} else {
		for (i = 0; i < rows; i++) {
			if (soc >= lut->row_entries[i]) {
				row1 = i - 1;
				row2 = i;
				break;
			}
		}
	}

	if (batt_temp < lut->col_entries[0] * DEGC_SCALE)
		batt_temp = lut->col_entries[0] * DEGC_SCALE;
	if (batt_temp > lut->col_entries[cols - 1] * DEGC_SCALE)
		batt_temp = lut->col_entries[cols - 1] * DEGC_SCALE;

	for (i = 0; i < cols; i++) {
		if (batt_temp <= lut->col_entries[i] * DEGC_SCALE)
			break;
	}

	if (batt_temp == lut->col_entries[i] * DEGC_SCALE) {
		slope = (lut->data[row1][i] - lut->data[row2][i]);
		if (slope <= 0) {
			pr_warn_ratelimited("Slope=%d for soc=%d, using 1\n",
							slope, soc);
			slope = 1;
		}
		slope *= 10000;
		slope /= (lut->row_entries[row1] -
			lut->row_entries[row2]);
		return slope;
	}
	ocvrow1 = qg_linear_interpolate(
			lut->data[row1][i - 1],
			lut->col_entries[i - 1] * DEGC_SCALE,
			lut->data[row1][i],
			lut->col_entries[i] * DEGC_SCALE,
			batt_temp);

	ocvrow2 = qg_linear_interpolate(
			lut->data[row2][i - 1],
			lut->col_entries[i - 1] * DEGC_SCALE,
			lut->data[row2][i],
			lut->col_entries[i] * DEGC_SCALE,
			batt_temp);

	slope = (ocvrow1 - ocvrow2);
	if (slope <= 0) {
		pr_warn_ratelimited("Slope=%d for soc=%d, using 1\n",
							slope, soc);
		slope = 1;
	}
	slope *= 10000;
	slope /= (lut->row_entries[row1] - lut->row_entries[row2]);

	return slope;
}

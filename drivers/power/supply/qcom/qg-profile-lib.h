/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QG_PROFILE_LIB_H__
#define __QG_PROFILE_LIB_H__

struct profile_table_data {
	char		*name;
	int		rows;
	int		cols;
	int		*row_entries;
	int		*col_entries;
	int		**data;
};

int qg_linear_interpolate(int y0, int x0, int y1, int x1, int x);
int qg_interpolate_single_row_lut(struct profile_table_data *lut,
						int x, int scale);
int qg_interpolate_soc(struct profile_table_data *lut,
				int batt_temp, int ocv);
int qg_interpolate_var(struct profile_table_data *lut,
				int batt_temp, int soc);
int qg_interpolate_slope(struct profile_table_data *lut,
				int batt_temp, int soc);

#endif /*__QG_PROFILE_LIB_H__ */

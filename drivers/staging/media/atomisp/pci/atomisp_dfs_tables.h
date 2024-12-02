/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 */
#ifndef	__ATOMISP_DFS_TABLES_H__
#define	__ATOMISP_DFS_TABLES_H__

#include <linux/kernel.h>

struct atomisp_freq_scaling_rule {
	unsigned int width;
	unsigned int height;
	unsigned short fps;
	unsigned int isp_freq;
	unsigned int run_mode;
};

struct atomisp_dfs_config {
	unsigned int lowest_freq;
	unsigned int max_freq_at_vmin;
	unsigned int highest_freq;
	const struct atomisp_freq_scaling_rule *dfs_table;
	unsigned int dfs_table_size;
};

extern const struct atomisp_dfs_config dfs_config_cht_soc;

#endif /* __ATOMISP_DFS_TABLES_H__ */

/*
 * Support for Intel Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
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

static const struct atomisp_freq_scaling_rule dfs_rules_merr[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_457MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
};

/* Merrifield and Moorefield DFS rules */
static const struct atomisp_dfs_config dfs_config_merr = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_400MHZ,
	.highest_freq = ISP_FREQ_457MHZ,
	.dfs_table = dfs_rules_merr,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_merr),
};

static const struct atomisp_freq_scaling_rule dfs_rules_merr_1179[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
};

static const struct atomisp_dfs_config dfs_config_merr_1179 = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_400MHZ,
	.highest_freq = ISP_FREQ_400MHZ,
	.dfs_table = dfs_rules_merr_1179,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_merr_1179),
};

static const struct atomisp_freq_scaling_rule dfs_rules_merr_117a[] = {
	{
		.width = 1920,
		.height = 1080,
		.fps = 30,
		.isp_freq = ISP_FREQ_266MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = 1080,
		.height = 1920,
		.fps = 30,
#ifndef ISP2401
		.isp_freq = ISP_FREQ_266MHZ,
#else
		.isp_freq = ISP_FREQ_400MHZ,
#endif
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = 1920,
		.height = 1080,
		.fps = 45,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = 1080,
		.height = 1920,
		.fps = 45,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = 60,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_200MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_200MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
};

static const struct atomisp_dfs_config dfs_config_merr_117a = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_200MHZ,
	.highest_freq = ISP_FREQ_400MHZ,
	.dfs_table = dfs_rules_merr_117a,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_merr_117a),
};

static const struct atomisp_freq_scaling_rule dfs_rules_byt[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
};

static const struct atomisp_dfs_config dfs_config_byt = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_400MHZ,
	.highest_freq = ISP_FREQ_400MHZ,
	.dfs_table = dfs_rules_byt,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_byt),
};

static const struct atomisp_freq_scaling_rule dfs_rules_byt_cr[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
};

static const struct atomisp_dfs_config dfs_config_byt_cr = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_320MHZ,
	.highest_freq = ISP_FREQ_320MHZ,
	.dfs_table = dfs_rules_byt_cr,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_byt_cr),
};

static const struct atomisp_freq_scaling_rule dfs_rules_cht[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
	{
		.width = 1280,
		.height = 720,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
};

static const struct atomisp_freq_scaling_rule dfs_rules_cht_soc[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_SDV,
	},
};

static const struct atomisp_dfs_config dfs_config_cht = {
	.lowest_freq = ISP_FREQ_100MHZ,
	.max_freq_at_vmin = ISP_FREQ_356MHZ,
	.highest_freq = ISP_FREQ_356MHZ,
	.dfs_table = dfs_rules_cht,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_cht),
};

static const struct atomisp_dfs_config dfs_config_cht_soc = {
	.lowest_freq = ISP_FREQ_100MHZ,
	.max_freq_at_vmin = ISP_FREQ_356MHZ,
	.highest_freq = ISP_FREQ_356MHZ,
	.dfs_table = dfs_rules_cht_soc,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_cht_soc),
};

#endif /* __ATOMISP_DFS_TABLES_H__ */

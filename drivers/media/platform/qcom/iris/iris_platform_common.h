/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_PLATFORM_COMMON_H__
#define __IRIS_PLATFORM_COMMON_H__

extern struct iris_platform_data sm8550_data;

enum platform_clk_type {
	IRIS_AXI_CLK,
	IRIS_CTRL_CLK,
	IRIS_HW_CLK,
};

struct platform_clk_data {
	enum platform_clk_type clk_type;
	const char *clk_name;
};

struct iris_platform_data {
	struct iris_inst *(*get_instance)(void);
	const struct icc_info *icc_tbl;
	unsigned int icc_tbl_size;
	const char * const *pmdomain_tbl;
	unsigned int pmdomain_tbl_size;
	const char * const *opp_pd_tbl;
	unsigned int opp_pd_tbl_size;
	const struct platform_clk_data *clk_tbl;
	unsigned int clk_tbl_size;
	const char * const *clk_rst_tbl;
	unsigned int clk_rst_tbl_size;
};

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_platform_common.h"

static const struct icc_info sm8550_icc_table[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const char * const sm8550_clk_reset_table[] = { "bus" };

static const char * const sm8550_pmdomain_table[] = { "venus", "vcodec0" };

static const char * const sm8550_opp_pd_table[] = { "mxc", "mmcx" };

static const struct platform_clk_data sm8550_clk_table[] = {
	{IRIS_AXI_CLK,  "iface"        },
	{IRIS_CTRL_CLK, "core"         },
	{IRIS_HW_CLK,   "vcodec0_core" },
};

struct iris_platform_data sm8550_data = {
	.icc_tbl = sm8550_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8550_icc_table),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
	.pmdomain_tbl = sm8550_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8550_pmdomain_table),
	.opp_pd_tbl = sm8550_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8550_opp_pd_table),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
};

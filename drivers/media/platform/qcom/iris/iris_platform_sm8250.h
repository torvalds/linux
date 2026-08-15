/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __IRIS_PLATFORM_SM8250_H__
#define __IRIS_PLATFORM_SM8250_H__

static const struct bw_info sm8250_bw_table_dec[] = {
	{ ((4096 * 2160) / 256) * 60, 2403000 },
	{ ((4096 * 2160) / 256) * 30, 1224000 },
	{ ((1920 * 1080) / 256) * 60,  812000 },
	{ ((1920 * 1080) / 256) * 30,  416000 },
};

static const char * const sm8250_opp_pd_table[] = { "mx", "mmcx" };

static const struct platform_clk_data sm8250_clk_table[] = {
	{IRIS_AXI_CLK,  "iface"        },
	{IRIS_CTRL_CLK, "core"         },
	{IRIS_HW_CLK,   "vcodec0_core" },
};

static const char * const sm8250_opp_clk_table[] = {
	"vcodec0_core",
	NULL,
};

#endif

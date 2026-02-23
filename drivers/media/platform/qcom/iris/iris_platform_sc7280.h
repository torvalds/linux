/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __IRIS_PLATFORM_SC7280_H__
#define __IRIS_PLATFORM_SC7280_H__

static const struct bw_info sc7280_bw_table_dec[] = {
	{ ((3840 * 2160) / 256) * 60, 1896000, },
	{ ((3840 * 2160) / 256) * 30,  968000, },
	{ ((1920 * 1080) / 256) * 60,  618000, },
	{ ((1920 * 1080) / 256) * 30,  318000, },
};

static const char * const sc7280_opp_pd_table[] = { "cx" };

static const struct platform_clk_data sc7280_clk_table[] = {
	{IRIS_CTRL_CLK,    "core"         },
	{IRIS_AXI_CLK,     "iface"        },
	{IRIS_AHB_CLK,     "bus"          },
	{IRIS_HW_CLK,      "vcodec_core"  },
	{IRIS_HW_AHB_CLK,  "vcodec_bus"   },
};

static const char * const sc7280_opp_clk_table[] = {
	"vcodec_core",
	NULL,
};

#endif

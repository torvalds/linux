/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __IRIS_PLATFORM_X1P42100_H__
#define __IRIS_PLATFORM_X1P42100_H__

static const struct platform_clk_data x1p42100_clk_table[] = {
	{IRIS_AXI_CLK,		"iface"			},
	{IRIS_CTRL_CLK,		"core"			},
	{IRIS_HW_CLK,		"vcodec0_core"		},
	{IRIS_BSE_HW_CLK,	"vcodec0_bse"		},
};

static const char *const x1p42100_opp_clk_table[] = {
	"vcodec0_core",
	"vcodec0_bse",
	NULL,
};

#endif

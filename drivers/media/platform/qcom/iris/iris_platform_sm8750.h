/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Linaro Ltd
 */

#ifndef __MEDIA_IRIS_PLATFORM_SM8750_H__
#define __MEDIA_IRIS_PLATFORM_SM8750_H__

static const char * const sm8750_clk_reset_table[] = {
	"bus0", "bus1", "core", "vcodec0_core"
};

static const struct platform_clk_data sm8750_clk_table[] = {
	{IRIS_AXI_CLK,		"iface"			},
	{IRIS_CTRL_CLK,		"core"			},
	{IRIS_HW_CLK,		"vcodec0_core"		},
	{IRIS_AXI1_CLK,		"iface1"		},
	{IRIS_CTRL_FREERUN_CLK,	"core_freerun"		},
	{IRIS_HW_FREERUN_CLK,	"vcodec0_core_freerun"	},
};

#endif

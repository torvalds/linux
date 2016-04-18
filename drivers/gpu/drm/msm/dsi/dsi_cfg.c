/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "dsi_cfg.h"

static const char * const dsi_v2_bus_clk_names[] = {
	"core_mmss_clk", "iface_clk", "bus_clk",
};

static const struct msm_dsi_config apq8064_dsi_cfg = {
	.io_offset = 0,
	.reg_cfg = {
		.num = 3,
		.regs = {
			{"vdda", 1200000, 1200000, 100000, 100},
			{"avdd", 3000000, 3000000, 110000, 100},
			{"vddio", 1800000, 1800000, 100000, 100},
		},
	},
	.bus_clk_names = dsi_v2_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_bus_clk_names),
};

static const char * const dsi_6g_bus_clk_names[] = {
	"mdp_core_clk", "iface_clk", "bus_clk", "core_mmss_clk",
};

static const struct msm_dsi_config msm8974_apq8084_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 4,
		.regs = {
			{"gdsc", -1, -1, -1, -1},
			{"vdd", 3000000, 3000000, 150000, 100},
			{"vdda", 1200000, 1200000, 100000, 100},
			{"vddio", 1800000, 1800000, 100000, 100},
		},
	},
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
};

static const char * const dsi_8916_bus_clk_names[] = {
	"mdp_core_clk", "iface_clk", "bus_clk",
};

static const struct msm_dsi_config msm8916_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 3,
		.regs = {
			{"gdsc", -1, -1, -1, -1},
			{"vdda", 1200000, 1200000, 100000, 100},
			{"vddio", 1800000, 1800000, 100000, 100},
		},
	},
	.bus_clk_names = dsi_8916_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_8916_bus_clk_names),
};

static const struct msm_dsi_config msm8994_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 7,
		.regs = {
			{"gdsc", -1, -1, -1, -1},
			{"vdda", 1250000, 1250000, 100000, 100},
			{"vddio", 1800000, 1800000, 100000, 100},
			{"vcca", 1000000, 1000000, 10000, 100},
			{"vdd", 1800000, 1800000, 100000, 100},
			{"lab_reg", -1, -1, -1, -1},
			{"ibb_reg", -1, -1, -1, -1},
		},
	},
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
};

static const struct msm_dsi_cfg_handler dsi_cfg_handlers[] = {
	{MSM_DSI_VER_MAJOR_V2, MSM_DSI_V2_VER_MINOR_8064, &apq8064_dsi_cfg},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_0,
						&msm8974_apq8084_dsi_cfg},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_1,
						&msm8974_apq8084_dsi_cfg},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_1_1,
						&msm8974_apq8084_dsi_cfg},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_2,
						&msm8974_apq8084_dsi_cfg},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_3, &msm8994_dsi_cfg},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_3_1, &msm8916_dsi_cfg},
};

const struct msm_dsi_cfg_handler *msm_dsi_cfg_get(u32 major, u32 minor)
{
	const struct msm_dsi_cfg_handler *cfg_hnd = NULL;
	int i;

	for (i = ARRAY_SIZE(dsi_cfg_handlers) - 1; i >= 0; i--) {
		if ((dsi_cfg_handlers[i].major == major) &&
			(dsi_cfg_handlers[i].minor == minor)) {
			cfg_hnd = &dsi_cfg_handlers[i];
			break;
		}
	}

	return cfg_hnd;
}


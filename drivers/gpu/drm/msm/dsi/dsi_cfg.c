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
	"core_mmss", "iface", "bus",
};

static const struct msm_dsi_config apq8064_dsi_cfg = {
	.io_offset = 0,
	.reg_cfg = {
		.num = 3,
		.regs = {
			{"vdda", 100000, 100},	/* 1.2 V */
			{"avdd", 10000, 100},	/* 3.0 V */
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_v2_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_bus_clk_names),
	.io_start = { 0x4700000, 0x5800000 },
	.num_dsi = 2,
};

static const char * const dsi_6g_bus_clk_names[] = {
	"mdp_core", "iface", "bus", "core_mmss",
};

static const struct msm_dsi_config msm8974_apq8084_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 4,
		.regs = {
			{"gdsc", -1, -1},
			{"vdd", 150000, 100},	/* 3.0 V */
			{"vdda", 100000, 100},	/* 1.2 V */
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
	.io_start = { 0xfd922800, 0xfd922b00 },
	.num_dsi = 2,
};

static const char * const dsi_8916_bus_clk_names[] = {
	"mdp_core", "iface", "bus",
};

static const struct msm_dsi_config msm8916_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 3,
		.regs = {
			{"gdsc", -1, -1},
			{"vdda", 100000, 100},	/* 1.2 V */
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_8916_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_8916_bus_clk_names),
	.io_start = { 0x1a98000 },
	.num_dsi = 1,
};

static const struct msm_dsi_config msm8994_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 7,
		.regs = {
			{"gdsc", -1, -1},
			{"vdda", 100000, 100},	/* 1.25 V */
			{"vddio", 100000, 100},	/* 1.8 V */
			{"vcca", 10000, 100},	/* 1.0 V */
			{"vdd", 100000, 100},	/* 1.8 V */
			{"lab_reg", -1, -1},
			{"ibb_reg", -1, -1},
		},
	},
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
	.io_start = { 0xfd998000, 0xfd9a0000 },
	.num_dsi = 2,
};

/*
 * TODO: core_mmss_clk fails to enable for some reason, but things work fine
 * without it too. Figure out why it doesn't enable and uncomment below
 */
static const char * const dsi_8996_bus_clk_names[] = {
	"mdp_core", "iface", "bus", /* "core_mmss", */
};

static const struct msm_dsi_config msm8996_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 2,
		.regs = {
			{"vdda", 18160, 1 },	/* 1.25 V */
			{"vcca", 17000, 32 },	/* 0.925 V */
			{"vddio", 100000, 100 },/* 1.8 V */
		},
	},
	.bus_clk_names = dsi_8996_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_8996_bus_clk_names),
	.io_start = { 0x994000, 0x996000 },
	.num_dsi = 2,
};

static const char * const dsi_sdm845_bus_clk_names[] = {
	"iface", "bus",
};

static const struct msm_dsi_config sdm845_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdda", 21800, 4 },	/* 1.2 V */
		},
	},
	.bus_clk_names = dsi_sdm845_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_sdm845_bus_clk_names),
	.io_start = { 0xae94000, 0xae96000 },
	.num_dsi = 2,
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
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_4_1, &msm8996_dsi_cfg},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_2_1, &sdm845_dsi_cfg},
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


// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include "dsi_cfg.h"

static const char * const dsi_v2_bus_clk_names[] = {
	"core_mmss", "iface", "bus",
};

static const struct regulator_bulk_data apq8064_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 100000 },	/* 1.2 V */
	{ .supply = "avdd", .init_load_uA = 10000 },	/* 3.0 V */
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
};

static const struct msm_dsi_config apq8064_dsi_cfg = {
	.io_offset = 0,
	.regulator_data = apq8064_dsi_regulators,
	.num_regulators = ARRAY_SIZE(apq8064_dsi_regulators),
	.bus_clk_names = dsi_v2_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_bus_clk_names),
	.io_start = {
		{ 0x4700000, 0x5800000 },
	},
};

static const char * const dsi_6g_bus_clk_names[] = {
	"mdp_core", "iface", "bus", "core_mmss",
};

static const struct regulator_bulk_data msm8974_apq8084_regulators[] = {
	{ .supply = "vdd", .init_load_uA = 150000 },	/* 3.0 V */
	{ .supply = "vdda", .init_load_uA = 100000 },	/* 1.2 V */
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
};

static const struct msm_dsi_config msm8974_apq8084_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = msm8974_apq8084_regulators,
	.num_regulators = ARRAY_SIZE(msm8974_apq8084_regulators),
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
	.io_start = {
		{ 0xfd922800, 0xfd922b00 },
	},
};

static const char * const dsi_v1_3_1_clk_names[] = {
	"mdp_core", "iface", "bus",
};

static const struct regulator_bulk_data dsi_v1_3_1_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 100000 },	/* 1.2 V */
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
};

static const struct msm_dsi_config msm8916_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = dsi_v1_3_1_regulators,
	.num_regulators = ARRAY_SIZE(dsi_v1_3_1_regulators),
	.bus_clk_names = dsi_v1_3_1_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v1_3_1_clk_names),
	.io_start = {
		{ 0x1a98000 },
	},
};

static const struct msm_dsi_config msm8976_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = dsi_v1_3_1_regulators,
	.num_regulators = ARRAY_SIZE(dsi_v1_3_1_regulators),
	.bus_clk_names = dsi_v1_3_1_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v1_3_1_clk_names),
	.io_start = {
		{ 0x1a94000, 0x1a96000 },
	},
};

static const struct regulator_bulk_data msm8994_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 100000 },	/* 1.25 V */
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
	{ .supply = "vcca", .init_load_uA = 10000 },	/* 1.0 V */
	{ .supply = "vdd", .init_load_uA = 100000 },	/* 1.8 V */
	{ .supply = "lab_reg", .init_load_uA = -1 },
	{ .supply = "ibb_reg", .init_load_uA = -1 },
};

static const struct msm_dsi_config msm8994_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = msm8994_dsi_regulators,
	.num_regulators = ARRAY_SIZE(msm8994_dsi_regulators),
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
	.io_start = {
		{ 0xfd998000, 0xfd9a0000 },
	},
};

static const struct regulator_bulk_data msm8996_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 18160 },	/* 1.25 V */
	{ .supply = "vcca", .init_load_uA = 17000 },	/* 0.925 V */
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
};

static const struct msm_dsi_config msm8996_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = msm8996_dsi_regulators,
	.num_regulators = ARRAY_SIZE(msm8996_dsi_regulators),
	.bus_clk_names = dsi_6g_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_6g_bus_clk_names),
	.io_start = {
		{ 0x994000, 0x996000 },
	},
};

static const char * const dsi_msm8998_bus_clk_names[] = {
	"iface", "bus", "core",
};

static const struct regulator_bulk_data msm8998_dsi_regulators[] = {
	{ .supply = "vdd", .init_load_uA = 367000 },	/* 0.9 V */
	{ .supply = "vdda", .init_load_uA = 62800 },	/* 1.2 V */
};

static const struct msm_dsi_config msm8998_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = msm8998_dsi_regulators,
	.num_regulators = ARRAY_SIZE(msm8998_dsi_regulators),
	.bus_clk_names = dsi_msm8998_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_msm8998_bus_clk_names),
	.io_start = {
		{ 0xc994000, 0xc996000 },
	},
};

static const char * const dsi_sdm660_bus_clk_names[] = {
	"iface", "bus", "core", "core_mmss",
};

static const struct regulator_bulk_data sdm660_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 12560 },	/* 1.2 V */
};

static const struct msm_dsi_config sdm660_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = sdm660_dsi_regulators,
	.num_regulators = ARRAY_SIZE(sdm660_dsi_regulators),
	.bus_clk_names = dsi_sdm660_bus_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_sdm660_bus_clk_names),
	.io_start = {
		{ 0xc994000, 0xc996000 },
	},
};

static const char * const dsi_v2_4_clk_names[] = {
	"iface", "bus",
};

static const struct regulator_bulk_data dsi_v2_4_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 21800 },	/* 1.2 V */
	{ .supply = "refgen" },
};

static const struct msm_dsi_config sdm845_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = dsi_v2_4_regulators,
	.num_regulators = ARRAY_SIZE(dsi_v2_4_regulators),
	.bus_clk_names = dsi_v2_4_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_4_clk_names),
	.io_start = {
		{ 0xae94000, 0xae96000 }, /* SDM845 / SDM670 */
		{ 0x5e94000 }, /* QCM2290 / SM6115 / SM6125 / SM6375 */
	},
};

static const struct regulator_bulk_data sm8550_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 16800 },	/* 1.2 V */
};

static const struct msm_dsi_config sm8550_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = sm8550_dsi_regulators,
	.num_regulators = ARRAY_SIZE(sm8550_dsi_regulators),
	.bus_clk_names = dsi_v2_4_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_4_clk_names),
	.io_start = {
		{ 0xae94000, 0xae96000 },
	},
};

static const struct regulator_bulk_data sm8650_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 16600 },	/* 1.2 V */
};

static const struct msm_dsi_config sm8650_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = sm8650_dsi_regulators,
	.num_regulators = ARRAY_SIZE(sm8650_dsi_regulators),
	.bus_clk_names = dsi_v2_4_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_4_clk_names),
	.io_start = {
		{ 0xae94000, 0xae96000 },
	},
};

static const struct regulator_bulk_data sc7280_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 8350 },	/* 1.2 V */
	{ .supply = "refgen" },
};

static const struct msm_dsi_config sc7280_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = sc7280_dsi_regulators,
	.num_regulators = ARRAY_SIZE(sc7280_dsi_regulators),
	.bus_clk_names = dsi_v2_4_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_4_clk_names),
	.io_start = {
		{ 0xae94000, 0xae96000 },
	},
};

static const struct regulator_bulk_data sa8775p_dsi_regulators[] = {
	{ .supply = "vdda", .init_load_uA = 8300 },    /* 1.2 V */
	{ .supply = "refgen" },
};

static const struct msm_dsi_config sa8775p_dsi_cfg = {
	.io_offset = DSI_6G_REG_SHIFT,
	.regulator_data = sa8775p_dsi_regulators,
	.num_regulators = ARRAY_SIZE(sa8775p_dsi_regulators),
	.bus_clk_names = dsi_v2_4_clk_names,
	.num_bus_clks = ARRAY_SIZE(dsi_v2_4_clk_names),
	.io_start = {
		{ 0xae94000, 0xae96000 },
	},
};

static const struct msm_dsi_host_cfg_ops msm_dsi_v2_host_ops = {
	.link_clk_set_rate = dsi_link_clk_set_rate_v2,
	.link_clk_enable = dsi_link_clk_enable_v2,
	.link_clk_disable = dsi_link_clk_disable_v2,
	.clk_init_ver = dsi_clk_init_v2,
	.tx_buf_alloc = dsi_tx_buf_alloc_v2,
	.tx_buf_get = dsi_tx_buf_get_v2,
	.tx_buf_put = NULL,
	.dma_base_get = dsi_dma_base_get_v2,
	.calc_clk_rate = dsi_calc_clk_rate_v2,
};

static const struct msm_dsi_host_cfg_ops msm_dsi_6g_host_ops = {
	.link_clk_set_rate = dsi_link_clk_set_rate_6g,
	.link_clk_enable = dsi_link_clk_enable_6g,
	.link_clk_disable = dsi_link_clk_disable_6g,
	.clk_init_ver = NULL,
	.tx_buf_alloc = dsi_tx_buf_alloc_6g,
	.tx_buf_get = dsi_tx_buf_get_6g,
	.tx_buf_put = dsi_tx_buf_put_6g,
	.dma_base_get = dsi_dma_base_get_6g,
	.calc_clk_rate = dsi_calc_clk_rate_6g,
};

static const struct msm_dsi_host_cfg_ops msm_dsi_6g_v2_host_ops = {
	.link_clk_set_rate = dsi_link_clk_set_rate_6g,
	.link_clk_enable = dsi_link_clk_enable_6g,
	.link_clk_disable = dsi_link_clk_disable_6g,
	.clk_init_ver = dsi_clk_init_6g_v2,
	.tx_buf_alloc = dsi_tx_buf_alloc_6g,
	.tx_buf_get = dsi_tx_buf_get_6g,
	.tx_buf_put = dsi_tx_buf_put_6g,
	.dma_base_get = dsi_dma_base_get_6g,
	.calc_clk_rate = dsi_calc_clk_rate_6g,
};

static const struct msm_dsi_host_cfg_ops msm_dsi_6g_v2_9_host_ops = {
	.link_clk_set_rate = dsi_link_clk_set_rate_6g_v2_9,
	.link_clk_enable = dsi_link_clk_enable_6g,
	.link_clk_disable = dsi_link_clk_disable_6g,
	.clk_init_ver = dsi_clk_init_6g_v2_9,
	.tx_buf_alloc = dsi_tx_buf_alloc_6g,
	.tx_buf_get = dsi_tx_buf_get_6g,
	.tx_buf_put = dsi_tx_buf_put_6g,
	.dma_base_get = dsi_dma_base_get_6g,
	.calc_clk_rate = dsi_calc_clk_rate_6g,
};

static const struct msm_dsi_cfg_handler dsi_cfg_handlers[] = {
	{MSM_DSI_VER_MAJOR_V2, MSM_DSI_V2_VER_MINOR_8064,
		&apq8064_dsi_cfg, &msm_dsi_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_0,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_0_2,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_1,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_1_1,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_2,
		&msm8974_apq8084_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_3,
		&msm8994_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_3_1,
		&msm8916_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_4_1,
		&msm8996_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_4_2,
		&msm8976_dsi_cfg, &msm_dsi_6g_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_1_0,
		&sdm660_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_2_0,
		&msm8998_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_2_1,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_3_0,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_3_1,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_4_0,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_4_1,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_5_0,
		&sc7280_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_5_1,
		&sa8775p_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_6_0,
		&sdm845_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_7_0,
		&sm8550_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_8_0,
		&sm8650_dsi_cfg, &msm_dsi_6g_v2_host_ops},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V2_9_0,
		&sm8650_dsi_cfg, &msm_dsi_6g_v2_9_host_ops},
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
